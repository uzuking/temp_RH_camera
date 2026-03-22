#include <Arduino.h>

// Seedling Monitor - ESP32 Firmware
// See docs/design.md for full system design.
// Development step 3: Grove Serial Camera (OV528) SYNC test
// Note: Camera TX is 3.3V level — connected directly to GPIO16 (no level converter)

#define CAMERA_RX 16  // ESP32 GPIO16 (RX2) ← カメラTX (黄、直結)
#define CAMERA_TX 17  // ESP32 GPIO17 (TX2) → カメラRX (白)
#define MAX_SYNC_ATTEMPTS 60
#define SYNC_INTERVAL_MS 100
#define RESPONSE_TIMEOUT_MS 500

// OV528 commands (6 bytes each, camera address = 0x00)
const uint8_t CMD_SYNC[] = {0xaa, 0x0d, 0x00, 0x00, 0x00, 0x00};
const uint8_t CMD_ACK[]  = {0xaa, 0x0e, 0x0d, 0x00, 0x00, 0x00};

// Baud rates to try (hardware serial demo uses 115200, factory default may be 9600)
const long BAUD_RATES[] = {115200, 9600};
const int NUM_BAUD_RATES = 2;

// Print bytes as hex to Serial monitor
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
      delay(1);  // yield to RTOS / feed WDT
    }
  }
  return count;
}

// Check if response is an ACK for SYNC command
bool isSyncAck(const uint8_t* resp, int len) {
  if (len < 6) return false;
  // Byte 0: 0xaa (sync marker)
  // Byte 1: 0x0e (ACK opcode) — camera address is 0x00
  // Byte 2: 0x0d (echoed SYNC command)
  return resp[0] == 0xaa && resp[1] == 0x0e && resp[2] == 0x0d;
}

bool syncDone = false;

// Try SYNC handshake at a given baud rate. Returns true on success.
bool trySyncAtBaud(long baudRate) {
  Serial.printf("\n--- Trying %ld baud ---\n", baudRate);

  Serial2.end();
  Serial2.begin(baudRate, SERIAL_8N1, CAMERA_RX, CAMERA_TX);

  // カメラ電源安定待ち
  delay(500);

  Serial.printf("Sending SYNC (max %d attempts)...\n", MAX_SYNC_ATTEMPTS);

  uint8_t response[6] = {0};

  for (int attempt = 1; attempt <= MAX_SYNC_ATTEMPTS; attempt++) {
    // Clear RX buffer before each attempt
    while (Serial2.available()) Serial2.read();

    // Step 1: Host → Camera: SYNC
    Serial2.write(CMD_SYNC, 6);
    Serial2.flush();

    // Step 2: Camera → Host: ACK
    memset(response, 0, sizeof(response));
    int received = readResponse(response, 6, RESPONSE_TIMEOUT_MS);

    if (received > 0) {
      Serial.printf("[%d] ", attempt);
      printHex("RX: ", response, received);

      if (isSyncAck(response, received)) {
        // Step 3: Camera → Host: SYNC (camera sends its own SYNC)
        uint8_t cameraSyncResp[6] = {0};
        int syncReceived = readResponse(cameraSyncResp, 6, RESPONSE_TIMEOUT_MS);
        if (syncReceived > 0) {
          printHex("Camera SYNC: ", cameraSyncResp, syncReceived);
        }

        // Step 4: Host → Camera: ACK (acknowledge camera's SYNC)
        Serial2.write(CMD_ACK, 6);
        Serial2.flush();
        printHex("TX ACK: ", CMD_ACK, 6);

        Serial.println();
        Serial.printf("SUCCESS: Camera sync confirmed at %ld baud!\n", baudRate);
        Serial.println("4-step handshake completed (SYNC -> ACK -> SYNC -> ACK)");
        return true;
      }
    } else if (attempt % 10 == 0) {
      Serial.printf("[%d] No response\n", attempt);
    }

    delay(SYNC_INTERVAL_MS);
  }

  Serial.printf("No response at %ld baud.\n", baudRate);
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== Seedling Monitor - Camera SYNC Test ===");
  Serial.printf("Serial2: RX=GPIO%d, TX=GPIO%d (direct, no level converter)\n", CAMERA_RX, CAMERA_TX);

  for (int i = 0; i < NUM_BAUD_RATES; i++) {
    if (trySyncAtBaud(BAUD_RATES[i])) {
      syncDone = true;
      return;
    }
  }

  // All baud rates failed
  Serial.println();
  Serial.println("FAILED: No sync response at any baud rate.");
  Serial.println();
  Serial.println("Troubleshooting:");
  Serial.println("  1. Swap TX/RX wires (yellow <-> white) and retry");
  Serial.println("  2. Verify camera VCC is connected to 5V (VIN)");
  Serial.println("  3. Check GND connection");
}

void loop() {
  if (syncDone) {
    delay(10000);
    Serial.println("Sync OK. Waiting... (reset to re-test)");
  } else {
    delay(1000);
  }
}
