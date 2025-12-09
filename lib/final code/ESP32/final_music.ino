#include <WiFi.h>
#include "USB.h"
#include "USBHIDConsumerControl.h"

USBHIDConsumerControl ConsumerControl;

/******************************************************
 * TERMINAL ESP32 for MUSIC (Device 3)
 ******************************************************/

#define IR_PIN 33   // 或你想用的任意普通 GPIO

const int TERMINAL_DEVICE_ID = 3;

const char* ssid = "wrist_ap";


const char* pass = "12345678";

WiFiClient client;
bool wifi_connected = false;

// =======================================================
// WiFi connect
// =======================================================
void connectWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 50) {
    delay(200);
    tries++;
  }

  if (WiFi.status() == WL_CONNECTED)
    Serial.println("[WiFi] Connected.");
}

void ensureWiFi()
{
  if (WiFi.status() != WL_CONNECTED)
    connectWiFi();

  if (!client.connected()) {
    Serial.println("[WiFi] Connecting TCP...");
    if (client.connect(IPAddress(192,168,4,1), 12345)) {
      Serial.println("[WiFi] TCP Connected.");
      client.print("HELLO,3\n");
      wifi_connected = true;
    }
  }
}

// =======================================================
// IR MULTI SAMPLE
// =======================================================
bool checkIRHit()
{
  int count = 0;
  for (int i = 0; i < 5; i++) {
    if (digitalRead(IR_PIN) == LOW) count++;
    delay(2);
  }
  return count >= 3;
}

// =======================================================
// HID
// =======================================================
void performMusicControl(const String& cmd)
{
  if (cmd == "UP") {
    ConsumerControl.press(CONSUMER_CONTROL_VOLUME_INCREMENT);
    delay(30);
    ConsumerControl.release();
  }
  else if (cmd == "DOWN") {
    ConsumerControl.press(CONSUMER_CONTROL_VOLUME_DECREMENT);
    delay(30);
    ConsumerControl.release();
  }
  else if (cmd == "LEFT") {
    ConsumerControl.press(CONSUMER_CONTROL_SCAN_PREVIOUS);
    delay(30);
    ConsumerControl.release();
  }
  else if (cmd == "RIGHT") {
    ConsumerControl.press(CONSUMER_CONTROL_SCAN_NEXT);
    delay(30);
    ConsumerControl.release();
  }
  else if (cmd == "OPEN") {
    ConsumerControl.press(CONSUMER_CONTROL_PLAY_PAUSE);
    delay(30);
    ConsumerControl.release();
  }
  else if (cmd == "CLOSE") {
    ConsumerControl.press(CONSUMER_CONTROL_STOP);
    delay(30);
    ConsumerControl.release();
  }
}

// =======================================================
// WiFi commands
// =======================================================
void handleWiFiCommands()
{
  if (!client.connected()) return;

  while (client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    int comma = line.indexOf(',');
    if (comma < 0) continue;

    String cmd = line.substring(0, comma);
    int id = line.substring(comma + 1).toInt();

    if (id == TERMINAL_DEVICE_ID)
      performMusicControl(cmd);
  }
}

// =======================================================
// IR pairing
// =======================================================
unsigned long lastPair = 0;

void checkIR()
{
  if (!client.connected()) return;
  if (millis() - lastPair < 500) return;

  if (checkIRHit()) {
    lastPair = millis();
    client.print("PAIR_OK,3\n");
    Serial.println("[PAIR] MUSIC");
  }
}

// =======================================================
// SETUP
// =======================================================
void setup()
{
  Serial.begin(115200);
  pinMode(IR_PIN, INPUT_PULLUP);

  USB.begin();
  ConsumerControl.begin();

  connectWiFi();
}

// =======================================================
// LOOP
// =======================================================
void loop()
{
  ensureWiFi();
  handleWiFiCommands();
  checkIR();
}
