#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include "credentials.h"

// Seedling Monitor - ESP32 Firmware
// See docs/design.md for full system design.
// Development step 9: Full integration with periodic sending

// --- Intervals (change for testing) ---
#define SENSOR_INTERVAL_MS  30000    // 30秒（テスト用。本番: 600000 = 10分）
#define IMAGE_INTERVAL_MS   120000   // 2分（テスト用。本番: 3600000 = 1時間）

// DHT22
#define DHT_PIN 25
#define DHT_TYPE DHT22
#define DHT_MAX_RETRIES 3
#define DHT_RETRY_DELAY_MS 2500

// Camera
#define CAMERA_RX 16
#define CAMERA_TX 17
#define CAMERA_BAUD 115200
#define MAX_SYNC_ATTEMPTS 60
#define RESPONSE_TIMEOUT_MS 500
#define PIC_PKT_LEN 512
#define PIC_DATA_LEN 506
#define JPEG_BUF_SIZE 64000

// NTP
#define NTP_SERVER_PRIMARY "ntp.nict.jp"
#define NTP_SERVER_SECONDARY "pool.ntp.org"
#define GMT_OFFSET_SEC 32400
#define DST_OFFSET_SEC 0
#define WIFI_TIMEOUT_MS 10000
#define NTP_TIMEOUT_MS 10000
#define NTP_VALID_EPOCH 1700000000

// HTTP
#define HTTP_TIMEOUT_MS 10000

#define TIMESTAMP_FMT "%Y-%m-%dT%H:%M:%S+09:00"

// OV528 commands
const uint8_t CMD_SYNC[]     = {0xAA, 0x0D, 0x00, 0x00, 0x00, 0x00};
const uint8_t CMD_ACK_SYNC[] = {0xAA, 0x0E, 0x0D, 0x00, 0x00, 0x00};
const uint8_t CMD_INIT[]     = {0xAA, 0x01, 0x00, 0x07, 0x00, 0x07}; // JPEG 640x480
const uint8_t CMD_SET_PKT[]  = {0xAA, 0x06, 0x08, PIC_PKT_LEN & 0xFF, (PIC_PKT_LEN >> 8) & 0xFF, 0x00};
const uint8_t CMD_SNAPSHOT[] = {0xAA, 0x05, 0x00, 0x00, 0x00, 0x00};
const uint8_t CMD_GET_PIC[]  = {0xAA, 0x04, 0x01, 0x00, 0x00, 0x00};

// Global state
DHT dht(DHT_PIN, DHT_TYPE);
uint8_t jpegBuf[JPEG_BUF_SIZE];
uint32_t jpegSize = 0;
bool cameraReady = false;
int cameraFailCount = 0;
unsigned long lastSensorTime = 0;
unsigned long lastImageTime = 0;

// ===== Utility =====

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

void getTimestamp(char* buf, size_t len) {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    strftime(buf, len, TIMESTAMP_FMT, &timeinfo);
  } else {
    snprintf(buf, len, "unknown");
  }
}

// ===== WiFi + NTP =====

bool connectWiFi() {
  Serial.printf("WiFi: %s ", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
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
  Serial.println("  FAILED");
  return false;
}

bool syncNTP() {
  Serial.printf("NTP: %s ", NTP_SERVER_PRIMARY);
  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER_PRIMARY, NTP_SERVER_SECONDARY);

  unsigned long start = millis();
  time_t now;
  while ((millis() - start) < NTP_TIMEOUT_MS) {
    time(&now);
    if (now > NTP_VALID_EPOCH) {
      char buf[30];
      struct tm timeinfo;
      localtime_r(&now, &timeinfo);
      strftime(buf, sizeof(buf), TIMESTAMP_FMT, &timeinfo);
      Serial.printf("OK: %s\n", buf);
      return true;
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println("FAILED");
  return false;
}

// ===== Camera =====

bool doSync() {
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
        Serial.printf("  Camera SYNC OK (attempt %d)\n", attempt);
        return true;
      }
      continue;
    }
    if (attempt % 10 == 0) Serial.printf("  Camera SYNC [%d]...\n", attempt);
    delay(100);
  }
  Serial.println("  Camera SYNC FAILED");
  return false;
}

bool initCamera() {
  if (!sendCommand(CMD_INIT, 0x01, "INIT")) return false;
  delay(100);
  if (!sendCommand(CMD_SET_PKT, 0x06, "SET_PKT")) return false;
  return true;
}

bool captureAndReadJPEG() {
  // SNAPSHOT
  if (!sendCommand(CMD_SNAPSHOT, 0x05, "SNAPSHOT")) return false;
  delay(500);

  // GET_PICTURE
  while (Serial2.available()) Serial2.read();
  Serial2.write(CMD_GET_PIC, 6);
  Serial2.flush();

  uint8_t resp[6] = {0};
  int received = readResponse(resp, 6, 2000);
  if (received < 6 || resp[0] != 0xAA || resp[1] != 0x0E || resp[2] != 0x04) return false;

  uint8_t dataResp[6] = {0};
  received = readResponse(dataResp, 6, RESPONSE_TIMEOUT_MS);
  if (received < 6 || dataResp[0] != 0xAA || dataResp[1] != 0x0A) return false;

  uint32_t picSize = dataResp[3] | (dataResp[4] << 8) | (dataResp[5] << 16);
  if (picSize == 0 || picSize > JPEG_BUF_SIZE) {
    Serial.printf("  Invalid pic size: %u\n", picSize);
    return false;
  }

  // Receive packets
  int totalPackets = (picSize + PIC_DATA_LEN - 1) / PIC_DATA_LEN;
  jpegSize = 0;
  uint8_t pktBuf[PIC_PKT_LEN];

  for (int pkt = 0; pkt < totalPackets; pkt++) {
    sendDataAck(pkt);
    int pktReceived = readResponse(pktBuf, PIC_PKT_LEN, 1000);
    if (pktReceived < 6) return false;

    uint16_t dataSize = pktBuf[2] | (pktBuf[3] << 8);
    if (dataSize > PIC_DATA_LEN || jpegSize + dataSize > JPEG_BUF_SIZE) return false;

    memcpy(jpegBuf + jpegSize, pktBuf + 4, dataSize);
    jpegSize += dataSize;
  }

  sendDataAck(0xF0F0);

  return (jpegSize >= 4 &&
          jpegBuf[0] == 0xFF && jpegBuf[1] == 0xD8 &&
          jpegBuf[jpegSize - 2] == 0xFF && jpegBuf[jpegSize - 1] == 0xD9);
}

// ===== HTTP POST =====

void postSensorData(float temperature, float humidity) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("  WiFi disconnected, skipping sensor POST");
    return;
  }

  char timestamp[30];
  getTimestamp(timestamp, sizeof(timestamp));

  char json[256];
  snprintf(json, sizeof(json),
    "{\"temperature\":%.1f,\"humidity\":%.1f,\"timestamp\":\"%s\"}",
    temperature, humidity, timestamp);

  char url[256];
  snprintf(url, sizeof(url), "http://%s:%d/sensor", SERVER_HOST, SERVER_PORT);

  HTTPClient http;
  http.begin(url);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST(json);

  if (httpCode == 200) {
    Serial.printf("  Sensor POST OK: T=%.1f H=%.1f\n", temperature, humidity);
  } else {
    Serial.printf("  Sensor POST error: %d\n", httpCode);
  }
  http.end();
}

void postImage() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("  WiFi disconnected, skipping image POST");
    return;
  }

  char timestamp[30];
  getTimestamp(timestamp, sizeof(timestamp));

  char url[256];
  snprintf(url, sizeof(url), "http://%s:%d/image", SERVER_HOST, SERVER_PORT);

  HTTPClient http;
  http.begin(url);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.addHeader("Content-Type", "image/jpeg");
  http.addHeader("X-Timestamp", timestamp);
  int httpCode = http.POST(jpegBuf, jpegSize);

  if (httpCode == 200) {
    Serial.printf("  Image POST OK: %u bytes\n", jpegSize);
  } else {
    Serial.printf("  Image POST error: %d\n", httpCode);
  }
  http.end();
}

// ===== Sensor task =====

void doSensorTask() {
  float temperature, humidity;

  for (int i = 0; i < DHT_MAX_RETRIES; i++) {
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();
    if (!isnan(temperature) && !isnan(humidity)) {
      postSensorData(temperature, humidity);
      return;
    }
    if (i < DHT_MAX_RETRIES - 1) delay(DHT_RETRY_DELAY_MS);
  }
  Serial.println("  DHT22 read failed after retries");
}

// ===== Image task =====

void doImageTask() {
  if (!cameraReady) {
    Serial.println("  Camera not ready, skipping");
    return;
  }

  Serial.println("  Capturing...");
  if (captureAndReadJPEG()) {
    Serial.printf("  JPEG: %u bytes\n", jpegSize);
    postImage();
    cameraFailCount = 0;
  } else {
    cameraFailCount++;
    Serial.printf("  JPEG capture failed (fail count: %d)\n", cameraFailCount);

    // Drain Serial2 buffer to clear stale camera data
    unsigned long drainStart = millis();
    while ((millis() - drainStart) < 2000) {
      while (Serial2.available()) Serial2.read();
      delay(100);
    }

    // Re-SYNC after 3 consecutive failures
    if (cameraFailCount >= 3) {
      Serial.println("  Re-initializing camera...");
      cameraReady = false;
      if (doSync() && initCamera()) {
        cameraReady = true;
        cameraFailCount = 0;
        Serial.println("  Camera re-initialized OK");
      } else {
        Serial.println("  Camera re-init FAILED");
      }
    }
  }
}

// ===== Main =====

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== Seedling Monitor v1.0 ===");
  Serial.printf("Sensor interval: %ds, Image interval: %ds\n",
    SENSOR_INTERVAL_MS / 1000, IMAGE_INTERVAL_MS / 1000);

  // DHT22
  dht.begin();

  // Camera setup
  Serial2.begin(CAMERA_BAUD, SERIAL_8N1, CAMERA_RX, CAMERA_TX);
  delay(500);

  if (doSync() && initCamera()) {
    cameraReady = true;
    Serial.println("Camera: ready (640x480 JPEG)");
  } else {
    Serial.println("Camera: FAILED (sensor-only mode)");
  }

  // WiFi + NTP
  if (!connectWiFi()) { delay(10000); ESP.restart(); }
  if (!syncNTP()) { delay(10000); ESP.restart(); }

  Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());

  // Initial tasks
  Serial.println("\n--- Initial send ---");
  doSensorTask();
  if (cameraReady) doImageTask();

  Serial.printf("Free heap after initial tasks: %u bytes\n", ESP.getFreeHeap());

  lastSensorTime = millis();
  lastImageTime = millis();

  Serial.println("\n--- Periodic mode started ---");
  Serial.printf("Server: %s:%d\n\n", SERVER_HOST, SERVER_PORT);
}

void checkWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Disconnected, attempting reconnect...");
    WiFi.reconnect();
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_TIMEOUT_MS) {
      delay(500);
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[WiFi] Reconnected. IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
      Serial.println("[WiFi] Reconnect failed, will retry next loop");
    }
  }
}

void loop() {
  unsigned long now = millis();

  // WiFi health check (every sensor interval)
  if (now - lastSensorTime >= SENSOR_INTERVAL_MS) {
    checkWiFi();

    lastSensorTime = now;
    char ts[30];
    getTimestamp(ts, sizeof(ts));
    Serial.printf("[%s] Sensor task\n", ts);
    doSensorTask();
  }

  // Image task
  if (cameraReady && now - lastImageTime >= IMAGE_INTERVAL_MS) {
    lastImageTime = now;
    char ts[30];
    getTimestamp(ts, sizeof(ts));
    Serial.printf("[%s] Image task\n", ts);
    doImageTask();
  }

  delay(100);  // WDT feed
}
