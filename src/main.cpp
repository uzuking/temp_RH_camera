#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include "credentials.h"

// Seedling Monitor - ESP32 Firmware
// See docs/design.md for full system design.
// Development step 5: DHT22 + WiFi + NTP + HTTP POST

// DHT22 settings
#define DHT_PIN 25
#define DHT_TYPE DHT22
#define DHT_MAX_RETRIES 3
#define DHT_RETRY_DELAY_MS 2500  // DHT22 minimum sampling interval is 2s

// NTP settings
#define NTP_SERVER_PRIMARY "ntp.nict.jp"
#define NTP_SERVER_SECONDARY "pool.ntp.org"
#define GMT_OFFSET_SEC 32400   // JST = UTC+9
#define DST_OFFSET_SEC 0
#define WIFI_TIMEOUT_MS 10000
#define NTP_TIMEOUT_MS 10000
#define NTP_VALID_EPOCH 1700000000  // 2023-11-14 UTC

// HTTP settings
#define SEND_INTERVAL_MS 10000  // 10秒（テスト用。本番は600000 = 10分）
#define HTTP_TIMEOUT_MS 5000

// Timestamp format (assumes JST, matches GMT_OFFSET_SEC)
#define TIMESTAMP_FMT "%Y-%m-%dT%H:%M:%S+09:00"

DHT dht(DHT_PIN, DHT_TYPE);
bool setupOk = false;

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
    Serial.printf("  IP:   %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());
    return true;
  }

  Serial.println("FAILED: WiFi connection timed out.");
  return false;
}

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
  Serial.println("\nFAILED: NTP sync timed out.");
  return false;
}

// Get current timestamp as ISO 8601 string
void getTimestamp(char* buf, size_t len) {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    strftime(buf, len, TIMESTAMP_FMT, &timeinfo);
  } else {
    snprintf(buf, len, "unknown");
  }
}

// Read DHT22 with retry
bool readDHT(float* temperature, float* humidity) {
  for (int i = 0; i < DHT_MAX_RETRIES; i++) {
    *temperature = dht.readTemperature();
    *humidity = dht.readHumidity();
    if (!isnan(*temperature) && !isnan(*humidity)) {
      return true;
    }
    if (i < DHT_MAX_RETRIES - 1) {
      Serial.printf("  DHT22 retry %d/%d...\n", i + 1, DHT_MAX_RETRIES);
      delay(DHT_RETRY_DELAY_MS);
    }
  }
  return false;
}

// Send sensor data via HTTP POST
void sendSensorData(float temperature, float humidity) {
  // Check WiFi before attempting HTTP
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("  WiFi disconnected, skipping POST");
    return;
  }

  char timestamp[30];
  getTimestamp(timestamp, sizeof(timestamp));

  // Build JSON
  char json[256];
  snprintf(json, sizeof(json),
    "{\"temperature\":%.1f,\"humidity\":%.1f,\"timestamp\":\"%s\"}",
    temperature, humidity, timestamp);

  // Build URL
  char url[256];
  snprintf(url, sizeof(url), "http://%s:%d/sensor", SERVER_HOST, SERVER_PORT);

  Serial.printf("POST %s\n", url);
  Serial.printf("  Body: %s\n", json);

  HTTPClient http;
  http.begin(url);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.POST(json);

  if (httpCode == 200) {
    Serial.println("  OK (200)");
  } else if (httpCode > 0) {
    Serial.printf("  Server error: %d\n", httpCode);
  } else {
    Serial.printf("  Connection error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== Seedling Monitor - HTTP POST Test ===");

  dht.begin();

  if (!connectWiFi()) {
    Serial.println("Rebooting in 10 seconds...");
    delay(10000);
    ESP.restart();
  }
  if (!syncNTP()) {
    Serial.println("Rebooting in 10 seconds...");
    delay(10000);
    ESP.restart();
  }

  setupOk = true;
  Serial.println();
  Serial.printf("SUCCESS: Ready. Sending every %d seconds.\n", SEND_INTERVAL_MS / 1000);
  Serial.printf("Server: %s:%d\n", SERVER_HOST, SERVER_PORT);
}

void loop() {
  if (!setupOk) {
    delay(10000);
    return;
  }

  float temperature, humidity;
  if (readDHT(&temperature, &humidity)) {
    Serial.printf("DHT22: %.1fC / %.1f%%\n", temperature, humidity);
    sendSensorData(temperature, humidity);
  } else {
    Serial.println("ERROR: DHT22 read failed after retries");
  }

  delay(SEND_INTERVAL_MS);
}
