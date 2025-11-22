/**
 * ESP32-S2 <--> ATmega328PB UART bridge test
 * USB Serial: 96200
 * UART1 (to ATmega): 9600 baud
 */

#include <Arduino.h>

#define MEGA_RX_PIN 44    // ESP32-S2 receives <-- ATmega PD1 TX (via divider)
#define MEGA_TX_PIN 43    // ESP32-S2 sends    --> ATmega PD0 RX

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32-S2 <-> ATmega328PB UART TEST @ 9600 ===");

  // UART1 for communication with ATmega
  Serial1.begin(9600, SERIAL_8N1, MEGA_RX_PIN, MEGA_TX_PIN);
  Serial.println("Serial1 started at 9600 baud");
}

void loop() {
  // Forward bytes from ATmega to USB Serial
  while (Serial1.available() > 0) {
    char c = (char)Serial1.read();
    Serial.print(c);
  }

  // Forward USB Serial input to ATmega
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    Serial1.write(c);
  }

  // Optional heartbeat
  static unsigned long last = 0;
  if (millis() - last > 3000) {
    last = millis();
    Serial.println("\n[ESP32-S2] waiting data from ATmega...");
  }
}