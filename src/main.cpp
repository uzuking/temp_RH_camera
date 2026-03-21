#include <Arduino.h>
#include "credentials.h"

// Seedling Monitor - ESP32 Firmware
// See docs/design.md for full system design.
// Development step 1 (preparation) complete.
// Next: step 2 (DHT22 test)

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Seedling Monitor - starting...");
}

void loop() {
  // TODO: implement per development steps in docs/design.md
}
