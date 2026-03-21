#include <Arduino.h>
#include <DHT.h>

// Seedling Monitor - ESP32 Firmware
// See docs/design.md for full system design.
// Development step 2: DHT22 単体テスト

#define DHT_PIN 25
#define DHT_TYPE DHT22
#define READ_INTERVAL 5000  // 5秒間隔（テスト用）

DHT dht(DHT_PIN, DHT_TYPE);
int consecutiveSuccess = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Seedling Monitor - DHT22 test");
  dht.begin();
}

void loop() {
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  if (isnan(temperature) || isnan(humidity)) {
    Serial.printf("ERROR: DHT22 read failed (consecutive success was %d)\n", consecutiveSuccess);
    consecutiveSuccess = 0;
  } else {
    consecutiveSuccess++;
    Serial.printf("[%d] Temperature: %.1f°C  Humidity: %.1f%%\n", consecutiveSuccess, temperature, humidity);
  }

  delay(READ_INTERVAL);
}
