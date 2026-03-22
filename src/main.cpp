#include <Arduino.h>

// Seedling Monitor - ESP32 Firmware
// See docs/design.md for full system design.
// Development step 7: Grove Serial Camera (OV528) JPEG capture test

#define CAMERA_RX 16  // ESP32 GPIO16 (RX2) ← カメラTX (黄、直結)
#define CAMERA_TX 17  // ESP32 GPIO17 (TX2) → カメラRX (白)
#define CAMERA_BAUD 115200
#define MAX_SYNC_ATTEMPTS 60
#define RESPONSE_TIMEOUT_MS 500
#define PIC_PKT_LEN 512       // Packet size for image transfer
#define PIC_DATA_LEN 506      // Data bytes per packet (512 - 2 ID - 2 size - 2 checksum)
#define JPEG_BUF_SIZE 64000   // Max JPEG buffer (320x240 typically 15-30KB)

// OV528 commands (camera address = 0x00)
const uint8_t CMD_SYNC[]     = {0xAA, 0x0D, 0x00, 0x00, 0x00, 0x00};
const uint8_t CMD_ACK_SYNC[] = {0xAA, 0x0E, 0x0D, 0x00, 0x00, 0x00};
const uint8_t CMD_INIT[]     = {0xAA, 0x01, 0x00, 0x07, 0x00, 0x05}; // JPEG, 320x240
const uint8_t CMD_SET_PKT[]  = {0xAA, 0x06, 0x08, PIC_PKT_LEN & 0xFF, (PIC_PKT_LEN >> 8) & 0xFF, 0x00};
const uint8_t CMD_SNAPSHOT[] = {0xAA, 0x05, 0x00, 0x00, 0x00, 0x00};
const uint8_t CMD_GET_PIC[]  = {0xAA, 0x04, 0x01, 0x00, 0x00, 0x00};

uint8_t jpegBuf[JPEG_BUF_SIZE];
uint32_t jpegSize = 0;

// Print bytes as hex
void printHex(const char* label, const uint8_t* data, int len) {
  Serial.print(label);
  for (int i = 0; i < len; i++) {
    if (data[i] < 0x10) Serial.print("0");
    Serial.print(data[i], HEX);
    if (i < len - 1) Serial.print(" ");
  }
  Serial.println();
}

// Read up to `len` bytes from Serial2 with timeout
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

// Send command and wait for ACK. Returns true if ACK received for given cmdId.
bool sendCommand(const uint8_t* cmd, uint8_t cmdId, const char* name) {
  while (Serial2.available()) Serial2.read();  // clear RX

  Serial2.write(cmd, 6);
  Serial2.flush();

  uint8_t resp[6] = {0};
  int received = readResponse(resp, 6, RESPONSE_TIMEOUT_MS);

  if (received >= 6 && resp[0] == 0xAA && resp[1] == 0x0E && resp[2] == cmdId) {
    Serial.printf("  %s: ACK OK\n", name);
    return true;
  }

  Serial.printf("  %s: ACK FAILED (received %d bytes)\n", name, received);
  if (received > 0) printHex("    RX: ", resp, received);
  return false;
}

// Send ACK for a data packet
void sendDataAck(uint16_t packetId) {
  uint8_t ack[6] = {0xAA, 0x0E, 0x00, 0x00, 0x00, 0x00};
  ack[4] = packetId & 0xFF;
  ack[5] = (packetId >> 8) & 0xFF;
  Serial2.write(ack, 6);
  Serial2.flush();
}

// Perform 4-step SYNC handshake
bool doSync() {
  Serial.println("Starting SYNC...");

  for (int attempt = 1; attempt <= MAX_SYNC_ATTEMPTS; attempt++) {
    while (Serial2.available()) Serial2.read();

    Serial2.write(CMD_SYNC, 6);
    Serial2.flush();

    uint8_t resp[6] = {0};
    int received = readResponse(resp, 6, RESPONSE_TIMEOUT_MS);

    if (received >= 6 && resp[0] == 0xAA && resp[1] == 0x0E && resp[2] == 0x0D) {
      // Read camera's SYNC and verify
      uint8_t camSync[6] = {0};
      int syncReceived = readResponse(camSync, 6, RESPONSE_TIMEOUT_MS);

      if (syncReceived >= 6 && camSync[0] == 0xAA && camSync[1] == 0x0D) {
        // Send ACK for camera's SYNC
        Serial2.write(CMD_ACK_SYNC, 6);
        Serial2.flush();
        Serial.printf("  SYNC OK (attempt %d)\n", attempt);
        return true;
      }
      Serial.println("  Camera SYNC not received, retrying...");
      continue;
    }

    if (attempt % 10 == 0) Serial.printf("  [%d] No response\n", attempt);
    delay(100);
  }

  Serial.println("  SYNC FAILED");
  return false;
}

// Capture JPEG image
bool captureJPEG() {
  // Step 1: INIT (JPEG, 320x240)
  if (!sendCommand(CMD_INIT, 0x01, "INIT")) return false;
  delay(100);

  // Step 2: SET_PACKAGE_SIZE (512 bytes)
  if (!sendCommand(CMD_SET_PKT, 0x06, "SET_PKT")) return false;
  delay(100);

  // Step 3: SNAPSHOT
  if (!sendCommand(CMD_SNAPSHOT, 0x05, "SNAPSHOT")) return false;
  delay(500);  // Wait for camera to capture

  // Step 4: GET_PICTURE
  while (Serial2.available()) Serial2.read();
  Serial2.write(CMD_GET_PIC, 6);
  Serial2.flush();

  uint8_t resp[6] = {0};
  int received = readResponse(resp, 6, 2000);  // Longer timeout for picture response

  if (received < 6 || resp[0] != 0xAA || resp[1] != 0x0E || resp[2] != 0x04) {
    Serial.println("  GET_PICTURE: ACK FAILED");
    if (received > 0) printHex("    RX: ", resp, received);
    return false;
  }

  // Read DATA response (contains JPEG size)
  uint8_t dataResp[6] = {0};
  received = readResponse(dataResp, 6, RESPONSE_TIMEOUT_MS);

  if (received < 6 || dataResp[0] != 0xAA || dataResp[1] != 0x0A) {
    Serial.println("  GET_PICTURE: DATA response FAILED");
    if (received > 0) printHex("    RX: ", dataResp, received);
    return false;
  }

  // JPEG size from bytes 3-5 (little-endian, 3 bytes)
  uint32_t picSize = dataResp[3] | (dataResp[4] << 8) | (dataResp[5] << 16);
  Serial.printf("  Picture size reported: %u bytes\n", picSize);

  if (picSize == 0 || picSize > JPEG_BUF_SIZE) {
    Serial.printf("  Invalid picture size (max %d)\n", JPEG_BUF_SIZE);
    return false;
  }

  // Step 5: Receive packets
  int totalPackets = (picSize + PIC_DATA_LEN - 1) / PIC_DATA_LEN;
  Serial.printf("  Receiving %d packets...\n", totalPackets);

  jpegSize = 0;
  uint8_t pktBuf[PIC_PKT_LEN];

  for (int pkt = 0; pkt < totalPackets; pkt++) {
    // Send ACK to request packet
    sendDataAck(pkt);

    // Read packet
    int pktReceived = readResponse(pktBuf, PIC_PKT_LEN, 1000);

    if (pktReceived < 6) {
      Serial.printf("  Packet %d: too short (%d bytes)\n", pkt, pktReceived);
      return false;
    }

    // Extract data size from packet header
    uint16_t dataSize = pktBuf[2] | (pktBuf[3] << 8);

    if (dataSize > PIC_DATA_LEN) {
      Serial.printf("  Packet %d: invalid data size %d\n", pkt, dataSize);
      return false;
    }

    // Verify checksum (sum of all bytes except last 2, lower byte)
    uint16_t pktLen = 4 + dataSize;  // header + data
    uint8_t checksum = 0;
    for (uint16_t i = 0; i < pktLen; i++) {
      checksum += pktBuf[i];
    }
    uint8_t expected = pktBuf[pktLen];  // verify code is lower byte
    if (checksum != expected) {
      Serial.printf("  Packet %d: checksum mismatch (got %02X, expected %02X)\n", pkt, checksum, expected);
    }

    // Copy image data (skip 4-byte header)
    if (jpegSize + dataSize > JPEG_BUF_SIZE) {
      Serial.println("  Buffer overflow!");
      return false;
    }

    memcpy(jpegBuf + jpegSize, pktBuf + 4, dataSize);
    jpegSize += dataSize;

    // Progress every 10 packets
    if ((pkt + 1) % 10 == 0 || pkt == totalPackets - 1) {
      Serial.printf("  [%d/%d] %d bytes received\n", pkt + 1, totalPackets, jpegSize);
    }
  }

  // Final ACK (0xF0F0 signals end of transfer per OV528 spec)
  sendDataAck(0xF0F0);

  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== Seedling Monitor - JPEG Capture Test ===");

  Serial2.begin(CAMERA_BAUD, SERIAL_8N1, CAMERA_RX, CAMERA_TX);
  delay(500);

  // SYNC handshake
  if (!doSync()) {
    Serial.println("FAILED: Camera SYNC failed");
    return;
  }

  delay(200);

  // Capture JPEG
  Serial.println("\nCapturing JPEG...");
  if (!captureJPEG()) {
    Serial.println("\nFAILED: JPEG capture failed");
    return;
  }

  // Verify JPEG markers
  Serial.println();
  Serial.printf("JPEG size: %d bytes\n", jpegSize);

  if (jpegSize >= 2) {
    Serial.printf("Header: %02X %02X", jpegBuf[0], jpegBuf[1]);
    if (jpegBuf[0] == 0xFF && jpegBuf[1] == 0xD8) {
      Serial.println(" (JPEG SOI marker - OK)");
    } else {
      Serial.println(" (NOT a JPEG!)");
    }
  }

  if (jpegSize >= 2) {
    Serial.printf("Footer: %02X %02X", jpegBuf[jpegSize - 2], jpegBuf[jpegSize - 1]);
    if (jpegBuf[jpegSize - 2] == 0xFF && jpegBuf[jpegSize - 1] == 0xD9) {
      Serial.println(" (JPEG EOI marker - OK)");
    } else {
      Serial.println(" (Missing EOI marker)");
    }
  }

  if (jpegSize >= 4 &&
      jpegBuf[0] == 0xFF && jpegBuf[1] == 0xD8 &&
      jpegBuf[jpegSize - 2] == 0xFF && jpegBuf[jpegSize - 1] == 0xD9) {
    Serial.println("\nSUCCESS: JPEG capture complete!");
  } else {
    Serial.println("\nWARNING: JPEG markers not found. Data may be corrupted.");
  }
}

void loop() {
  delay(10000);
}
