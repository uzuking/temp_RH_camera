#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "credentials.h"

// Seedling Monitor - ESP32 Firmware
// See docs/design.md for full system design.
// Development step 8: Camera JPEG capture + HTTP POST to server

#define CAMERA_RX 16
#define CAMERA_TX 17
#define CAMERA_BAUD 115200

// NTP
#define NTP_SERVER_PRIMARY "ntp.nict.jp"
#define NTP_SERVER_SECONDARY "pool.ntp.org"
#define GMT_OFFSET_SEC 32400
#define DST_OFFSET_SEC 0
#define WIFI_TIMEOUT_MS 10000
#define NTP_TIMEOUT_MS 10000
#define NTP_VALID_EPOCH 1700000000

// Camera
#define MAX_SYNC_ATTEMPTS 60
#define RESPONSE_TIMEOUT_MS 500
#define PIC_PKT_LEN 512
#define PIC_DATA_LEN 506
#define JPEG_BUF_SIZE 64000

// HTTP
#define HTTP_TIMEOUT_MS 10000  // Longer timeout for image upload

#define TIMESTAMP_FMT "%Y-%m-%dT%H:%M:%S+09:00"

// OV528 commands
const uint8_t CMD_SYNC[]     = {0xAA, 0x0D, 0x00, 0x00, 0x00, 0x00};
const uint8_t CMD_ACK_SYNC[] = {0xAA, 0x0E, 0x0D, 0x00, 0x00, 0x00};
const uint8_t CMD_INIT[]     = {0xAA, 0x01, 0x00, 0x07, 0x00, 0x07}; // JPEG 640x480
const uint8_t CMD_SET_PKT[]  = {0xAA, 0x06, 0x08, PIC_PKT_LEN & 0xFF, (PIC_PKT_LEN >> 8) & 0xFF, 0x00};
const uint8_t CMD_SNAPSHOT[] = {0xAA, 0x05, 0x00, 0x00, 0x00, 0x00};
const uint8_t CMD_GET_PIC[]  = {0xAA, 0x04, 0x01, 0x00, 0x00, 0x00};

uint8_t jpegBuf[JPEG_BUF_SIZE];
uint32_t jpegSize = 0;

// --- Utility functions ---

void printHex(const char* label, const uint8_t* data, int len) {
  Serial.print(label);
  for (int i = 0; i < len; i++) {
    if (data[i] < 0x10) Serial.print("0");
    Serial.print(data[i], HEX);
    if (i < len - 1) Serial.print(" ");
  }
  Serial.println();
}

int readResponse(uint8_t* buf, int len, unsigned long timeoutMs) {
  unsigned long start = millis();
  int count = 0;
  while (count < len && (millis() - start) < timeoutMs) {
    if (Serial2.available()) {
      buf[count++] = Serial2.read();
    } else {
      delay(1);
    }
  }
  return count;
}

bool sendCommand(const uint8_t* cmd, uint8_t cmdId, const char* name) {
  while (Serial2.available()) Serial2.read();
  Serial2.write(cmd, 6);
  Serial2.flush();

  uint8_t resp[6] = {0};
  int received = readResponse(resp, 6, RESPONSE_TIMEOUT_MS);

  if (received >= 6 && resp[0] == 0xAA && resp[1] == 0x0E && resp[2] == cmdId) {
    Serial.printf("  %s: ACK OK\n", name);
    return true;
  }
  Serial.printf("  %s: ACK FAILED\n", name);
  return false;
}

void sendDataAck(uint16_t packetId) {
  uint8_t ack[6] = {0xAA, 0x0E, 0x00, 0x00, 0x00, 0x00};
  ack[4] = packetId & 0xFF;
  ack[5] = (packetId >> 8) & 0xFF;
  Serial2.write(ack, 6);
  Serial2.flush();
}

// --- WiFi + NTP ---

bool connectWiFi() {
  Serial.printf("Connecting to WiFi: %s ", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_TIMEOUT_MS) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("  IP: %s  RSSI: %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
  }
  Serial.println("FAILED: WiFi timeout");
  return false;
}

bool syncNTP() {
  Serial.printf("Syncing NTP from %s ...", NTP_SERVER_PRIMARY);
  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER_PRIMARY, NTP_SERVER_SECONDARY);

  unsigned long start = millis();
  time_t now;
  while ((millis() - start) < NTP_TIMEOUT_MS) {
    time(&now);
    if (now > NTP_VALID_EPOCH) {
      struct tm timeinfo;
      localtime_r(&now, &timeinfo);
      char buf[30];
      strftime(buf, sizeof(buf), TIMESTAMP_FMT, &timeinfo);
      Serial.printf(" OK: %s\n", buf);
      return true;
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println(" FAILED");
  return false;
}

void getTimestamp(char* buf, size_t len) {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    strftime(buf, len, TIMESTAMP_FMT, &timeinfo);
  } else {
    snprintf(buf, len, "unknown");
  }
}

// --- Camera SYNC + JPEG ---

bool doSync() {
  Serial.println("Camera SYNC...");
  for (int attempt = 1; attempt <= MAX_SYNC_ATTEMPTS; attempt++) {
    while (Serial2.available()) Serial2.read();
    Serial2.write(CMD_SYNC, 6);
    Serial2.flush();

    uint8_t resp[6] = {0};
    int received = readResponse(resp, 6, RESPONSE_TIMEOUT_MS);

    if (received >= 6 && resp[0] == 0xAA && resp[1] == 0x0E && resp[2] == 0x0D) {
      uint8_t camSync[6] = {0};
      int syncReceived = readResponse(camSync, 6, RESPONSE_TIMEOUT_MS);
      if (syncReceived >= 6 && camSync[0] == 0xAA && camSync[1] == 0x0D) {
        Serial2.write(CMD_ACK_SYNC, 6);
        Serial2.flush();
        Serial.printf("  SYNC OK (attempt %d)\n", attempt);
        return true;
      }
      continue;
    }
    if (attempt % 10 == 0) Serial.printf("  [%d] No response\n", attempt);
    delay(100);
  }
  Serial.println("  SYNC FAILED");
  return false;
}

bool captureJPEG() {
  if (!sendCommand(CMD_INIT, 0x01, "INIT")) return false;
  delay(100);
  if (!sendCommand(CMD_SET_PKT, 0x06, "SET_PKT")) return false;
  delay(100);
  if (!sendCommand(CMD_SNAPSHOT, 0x05, "SNAPSHOT")) return false;
  delay(500);

  // GET_PICTURE
  while (Serial2.available()) Serial2.read();
  Serial2.write(CMD_GET_PIC, 6);
  Serial2.flush();

  uint8_t resp[6] = {0};
  int received = readResponse(resp, 6, 2000);
  if (received < 6 || resp[0] != 0xAA || resp[1] != 0x0E || resp[2] != 0x04) {
    Serial.println("  GET_PICTURE: ACK FAILED");
    return false;
  }

  uint8_t dataResp[6] = {0};
  received = readResponse(dataResp, 6, RESPONSE_TIMEOUT_MS);
  if (received < 6 || dataResp[0] != 0xAA || dataResp[1] != 0x0A) {
    Serial.println("  GET_PICTURE: DATA FAILED");
    return false;
  }

  uint32_t picSize = dataResp[3] | (dataResp[4] << 8) | (dataResp[5] << 16);
  Serial.printf("  Picture size: %u bytes\n", picSize);

  if (picSize == 0 || picSize > JPEG_BUF_SIZE) {
    Serial.printf("  Invalid size (max %d)\n", JPEG_BUF_SIZE);
    return false;
  }

  // Receive packets
  int totalPackets = (picSize + PIC_DATA_LEN - 1) / PIC_DATA_LEN;
  jpegSize = 0;
  uint8_t pktBuf[PIC_PKT_LEN];

  for (int pkt = 0; pkt < totalPackets; pkt++) {
    sendDataAck(pkt);
    int pktReceived = readResponse(pktBuf, PIC_PKT_LEN, 1000);

    if (pktReceived < 6) {
      Serial.printf("  Packet %d: too short (%d)\n", pkt, pktReceived);
      return false;
    }

    uint16_t dataSize = pktBuf[2] | (pktBuf[3] << 8);
    if (dataSize > PIC_DATA_LEN || jpegSize + dataSize > JPEG_BUF_SIZE) {
      Serial.printf("  Packet %d: invalid size %d\n", pkt, dataSize);
      return false;
    }

    memcpy(jpegBuf + jpegSize, pktBuf + 4, dataSize);
    jpegSize += dataSize;
  }

  sendDataAck(0xF0F0);  // End of transfer

  // Verify JPEG markers
  if (jpegSize >= 4 &&
      jpegBuf[0] == 0xFF && jpegBuf[1] == 0xD8 &&
      jpegBuf[jpegSize - 2] == 0xFF && jpegBuf[jpegSize - 1] == 0xD9) {
    Serial.printf("  JPEG OK: %u bytes (FFD8...FFD9)\n", jpegSize);
    return true;
  }

  Serial.println("  JPEG markers not found");
  return false;
}

// --- HTTP POST image ---

void sendImage() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("  WiFi disconnected, skipping");
    return;
  }

  char timestamp[30];
  getTimestamp(timestamp, sizeof(timestamp));

  char url[256];
  snprintf(url, sizeof(url), "http://%s:%d/image", SERVER_HOST, SERVER_PORT);

  Serial.printf("POST %s (%u bytes)\n", url, jpegSize);

  HTTPClient http;
  http.begin(url);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.addHeader("Content-Type", "image/jpeg");
  http.addHeader("X-Timestamp", timestamp);

  int httpCode = http.POST(jpegBuf, jpegSize);

  if (httpCode == 200) {
    Serial.println("  Image uploaded OK (200)");
  } else if (httpCode > 0) {
    Serial.printf("  Server error: %d\n", httpCode);
  } else {
    Serial.printf("  Connection error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
}

// --- Main ---

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== Seedling Monitor - Image POST Test ===");

  // WiFi + NTP
  if (!connectWiFi()) { delay(10000); ESP.restart(); }
  if (!syncNTP()) { delay(10000); ESP.restart(); }

  // Camera
  Serial2.begin(CAMERA_BAUD, SERIAL_8N1, CAMERA_RX, CAMERA_TX);
  delay(500);

  if (!doSync()) {
    Serial.println("FAILED: Camera SYNC. Rebooting...");
    delay(10000);
    ESP.restart();
  }

  delay(200);

  if (!captureJPEG()) {
    Serial.println("FAILED: JPEG capture. Rebooting...");
    delay(10000);
    ESP.restart();
  }

  // Send to server
  sendImage();

  Serial.println("\nDone. Reset to capture again.");
}

void loop() {
  delay(10000);
}
