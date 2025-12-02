#include <WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

// =======================================================
// DEVICE ID
// =======================================================
const int DEVICE_LED   = 1;
const int DEVICE_MOTOR = 2;

// 当前配对的目标设备（只能有一个）
// 0 = None, 1 = LED, 2 = Motor
int currentPairedDevice = 0;

// =======================================================
// IR Receiver Pins
// =======================================================
const int IR_PIN_LED   = 36;    // Pair Device 1
const int IR_PIN_MOTOR = 6;     // Pair Device 2

unsigned long lastPairTime = 0; // 1s cooldown
const unsigned long pairCooldown = 1000; // 1 seconds

// =======================================================
// Status LED
// =======================================================
const int STATUS_LED = 5;

// =======================================================
// WiFi AP
// =======================================================
const char* ap_ssid = "esp_ap";
const char* ap_pass = "12345678";

WiFiServer server(12345);
WiFiClient client;
bool wifi_connected = false;

// =======================================================
// LED Strip Config
// =======================================================
#define LED_PIN 18
#define LED_COUNT 60

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

uint8_t brightness_level = 80;
String next_cmd_led = "";
String current_cmd_led = "";
unsigned long lastFrame = 0;
int anim_offset = 0;
byte rainbowPos = 0;

// =======================================================
// Motor Config (NMOS + analogWrite)
// =======================================================
const int MOTOR_PWM_PIN = 17;

int motorSpeed = 0;       // ⭐ motor 开机初始停止
int normalSpeed = 150;
bool intermittentMode = false;
bool motorOn = false;     // ⭐ boot 不输出 PWM
unsigned long lastToggle = 0;


// =======================================================
// LED UTILITIES
// =======================================================
void applyBrightness() {
  strip.setBrightness(brightness_level);
  strip.show();
}

void clearStrip() {
  for (int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, 0);
  strip.show();
}

// =======================================================
// LED Actions
// =======================================================
void doUp() {
  if (millis() - lastFrame < 20) return;
  lastFrame = millis();
  brightness_level = constrain(brightness_level + 3, 0, 255);
  applyBrightness();
}

void doDown() {
  if (millis() - lastFrame < 20) return;
  lastFrame = millis();
  brightness_level = constrain(brightness_level - 3, 0, 255);
  applyBrightness();
}

void doLeft() {
  if (millis() - lastFrame < 35) return;
  lastFrame = millis();

  for (int i = 0; i < LED_COUNT; i++) {
    int idx = (i + anim_offset) % LED_COUNT;
    uint8_t intensity = (sin((i + anim_offset) * 0.12) + 1) * 120;
    strip.setPixelColor(idx, strip.Color(255, 60 + intensity / 2, 0));
  }
  strip.show();
  anim_offset = (anim_offset + 1) % LED_COUNT;
}

void doRight() {
  if (millis() - lastFrame < 35) return;
  lastFrame = millis();

  for (int i = 0; i < LED_COUNT; i++) {
    int idx = (LED_COUNT - 1 - i + anim_offset) % LED_COUNT;
    uint8_t intensity = (sin((i + anim_offset) * 0.12) + 1) * 120;
    strip.setPixelColor(idx, strip.Color(0, intensity, 255));
  }
  strip.show();
  anim_offset = (anim_offset + 1) % LED_COUNT;
}

uint32_t wheelColor(byte pos) {
  if (pos < 85) return strip.Color(pos * 3, 255 - pos * 3, 0);
  else if (pos < 170) {
    pos -= 85;
    return strip.Color(255 - pos * 3, 0, pos * 3);
  } else {
    pos -= 170;
    return strip.Color(0, pos * 3, 255 - pos * 3);
  }
}

void doOpen() {
  if (millis() - lastFrame < 20) return;
  lastFrame = millis();

  for (int i = 0; i < LED_COUNT; i++)
    strip.setPixelColor(i, wheelColor((i * 256 / LED_COUNT + rainbowPos) & 255));

  strip.show();
  rainbowPos += 4;
}

void doClose() {
  if (millis() - lastFrame < 25) return;
  lastFrame = millis();

  if (brightness_level > 3) {
    brightness_level -= 3;
    applyBrightness();
  } else {
    clearStrip();
  }
}

void handleLEDAction() {
  if (next_cmd_led != "") {
    current_cmd_led = next_cmd_led;
    next_cmd_led = "";
    anim_offset = 0;
  }

  if (current_cmd_led == "UP") doUp();
  else if (current_cmd_led == "DOWN") doDown();
  else if (current_cmd_led == "LEFT") doLeft();
  else if (current_cmd_led == "RIGHT") doRight();
  else if (current_cmd_led == "OPEN") doOpen();
  else if (current_cmd_led == "CLOSE") doClose();
}


// =======================================================
// MOTOR FUNCTIONS
// =======================================================
void updateMotor() {

  if (intermittentMode) {
    if (millis() - lastToggle > 500) {
      lastToggle = millis();
      motorOn = !motorOn;
    }
    analogWrite(MOTOR_PWM_PIN, motorOn ? motorSpeed : 0);

  } else {
    analogWrite(MOTOR_PWM_PIN, motorSpeed);
  }
}

void motorStart() {
  intermittentMode = false;
  motorSpeed = normalSpeed;
  motorOn = true;
  analogWrite(MOTOR_PWM_PIN, motorSpeed);
  Serial.println("[MOTOR] START");
}

void motorStop() {
  intermittentMode = false;
  motorSpeed = 0;
  motorOn = false;
  analogWrite(MOTOR_PWM_PIN, 0);
  Serial.println("[MOTOR] STOP");
}

void motorSpeedUp() {
  motorSpeed = min(255, motorSpeed + 25);
  analogWrite(MOTOR_PWM_PIN, motorSpeed);
  Serial.printf("[MOTOR] Speed Up → %d\n", motorSpeed);
}

void motorSpeedDown() {
  motorSpeed = max(0, motorSpeed - 25);
  analogWrite(MOTOR_PWM_PIN, motorSpeed);
  Serial.printf("[MOTOR] Speed Down → %d\n", motorSpeed);
}

void motorIntermittent() {
  intermittentMode = true;
  Serial.println("[MOTOR] Intermittent Mode");
}

void motorNormal() {
  intermittentMode = false;
  motorSpeed = normalSpeed;
  analogWrite(MOTOR_PWM_PIN, motorSpeed);
  Serial.println("[MOTOR] Normal Mode");
}


// =======================================================
// Pairing Functions (Option 2: only one device active)
// =======================================================
void pairLED() {
  currentPairedDevice = DEVICE_LED;

  Serial.println("[PAIR] LED paired (Device 1)");
  client.print("PAIR_OK,1\n");

  lastPairTime = millis();
}

void pairMOTOR() {
  currentPairedDevice = DEVICE_MOTOR;

  Serial.println("[PAIR] MOTOR paired (Device 2)");
  client.print("PAIR_OK,2\n");

  lastPairTime = millis();
}


void checkIR_LED() {
  if (millis() - lastPairTime < pairCooldown) return;

  if (digitalRead(IR_PIN_LED) == LOW && wifi_connected) {
    pairLED();
  }
}

void checkIR_MOTOR() {
  if (millis() - lastPairTime < pairCooldown) return;

  if (digitalRead(IR_PIN_MOTOR) == LOW && wifi_connected) {
    pairMOTOR();
  }
}


// =======================================================
// WiFi Command Handler
// =======================================================
void handleWiFiCommands() {

  if (!wifi_connected) return;

  while (client.available()) {

    String line = client.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) return;

    Serial.printf("[RECEIVED] %s\n", line.c_str());

    int comma = line.indexOf(',');
    if (comma < 0) continue;

    String cmd = line.substring(0, comma);
    int id = line.substring(comma + 1).toInt();

    Serial.printf("[PARSE] CMD=%s, ID=%d\n", cmd.c_str(), id);

    if (id != currentPairedDevice) {
      Serial.printf("[IGNORE] Not current paired device. Currently paired = %d\n",
                    currentPairedDevice);
      continue;
    }

    // ---- LED ----
    if (id == DEVICE_LED) {
      next_cmd_led = cmd;
      Serial.printf("[EXECUTE] LED → %s\n", cmd.c_str());
    }

    // ---- MOTOR ----
    else if (id == DEVICE_MOTOR) {

      Serial.printf("[EXECUTE] MOTOR → %s\n", cmd.c_str());

      if      (cmd == "OPEN")        motorStart();         // 开始转
      else if (cmd == "CLOSE")       motorStop();          // 停止转
      else if (cmd == "LEFT")        motorNormal();        // 恒速模式
      else if (cmd == "RIGHT")       motorIntermittent();  // 间歇转
      else if (cmd == "UP")          motorSpeedUp();       // 加速
      else if (cmd == "DOWN")        motorSpeedDown();     // 减速
      else {
        Serial.println("[WARN] Unknown cmd for MOTOR.");
      }
    }
  }
}


// =======================================================
// SETUP
// =======================================================
void setup() {
  Serial.begin(115200);

  pinMode(IR_PIN_LED, INPUT);
  pinMode(IR_PIN_MOTOR, INPUT);
  pinMode(STATUS_LED, OUTPUT);

  pinMode(MOTOR_PWM_PIN, OUTPUT);
  analogWrite(MOTOR_PWM_PIN, 0);   // ⭐ 上电 motor 停止

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_pass);
  server.begin();

  strip.begin();
  strip.setBrightness(brightness_level);
  clearStrip();

  Serial.println("ESP32-S2 Multi-device Ready (Pair Switch + NMOS + 1s Debounce).");
}


// =======================================================
// MAIN LOOP
// =======================================================
void loop() {

  if (!wifi_connected) {
    WiFiClient newClient = server.available();
    if (newClient) {
      client = newClient;
      wifi_connected = true;
      Serial.println("[WiFi] Wristband Connected!");
    }
  }

  handleWiFiCommands();

  checkIR_LED();
  checkIR_MOTOR();

  handleLEDAction();
  updateMotor();

  delay(5);
}
