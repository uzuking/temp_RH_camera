#include <Arduino.h>
#include <WiFi.h>
#include "credentials.h"

// Seedling Monitor - ESP32 Firmware
// See docs/design.md for full system design.
// Development step 4: WiFi + NTP time sync test

#define NTP_SERVER_PRIMARY "ntp.nict.jp"
#define NTP_SERVER_SECONDARY "pool.ntp.org"
#define GMT_OFFSET_SEC 32400   // JST = UTC+9 = 9*3600
#define DST_OFFSET_SEC 0       // Japan does not use DST
#define WIFI_TIMEOUT_MS 10000
#define NTP_TIMEOUT_MS 10000
#define NTP_VALID_EPOCH 1700000000  // 2023-11-14 UTC — any time after this means NTP synced
#define TIMESTAMP_FMT "%Y-%m-%dT%H:%M:%S+09:00"

bool setupOk = false;

// Connect to WiFi. Returns true on success.
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
    Serial.println("WiFi connected!");
    Serial.printf("  SSID: %s\n", WIFI_SSID);
    Serial.printf("  IP:   %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());
    return true;
  }

  Serial.println("FAILED: WiFi connection timed out.");
  Serial.println("Check SSID/password in include/credentials.h");
  return false;
}

// Sync time via NTP. Returns true on success.
bool syncNTP() {
  Serial.printf("Syncing NTP from %s ...\n", NTP_SERVER_PRIMARY);
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
      Serial.printf("NTP sync OK: %s\n", buf);
      return true;
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("FAILED: NTP sync timed out.");
  return false;
}

// Print current JST time
void printCurrentTime() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char buf[30];
    strftime(buf, sizeof(buf), TIMESTAMP_FMT, &timeinfo);
    Serial.printf("Current time: %s\n", buf);
  } else {
    Serial.println("Failed to get local time");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);  // シリアルモニタ接続安定化（テスト用）
  Serial.println("=== Seedling Monitor - WiFi + NTP Test ===");

  if (!connectWiFi()) return;
  if (!syncNTP()) return;

  setupOk = true;
  Serial.println();
  Serial.println("SUCCESS: WiFi connected and NTP synced.");
  Serial.println("Time will update every 10 seconds below:");
}

void loop() {
  if (!setupOk) {
    delay(10000);
    return;
  }
  printCurrentTime();
  delay(10000);
}
