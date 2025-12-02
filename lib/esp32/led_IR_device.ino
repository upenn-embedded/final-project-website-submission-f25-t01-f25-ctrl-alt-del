#include <WiFi.h>
#include <Adafruit_NeoPixel.h>

// ========================
// 设备 ID（未来可改成 2/3/4）
// ========================
const int TERMINAL_DEVICE_ID = 1;  // ⭐灯带属于 Device 1

// ========================
// IR + LED 引脚
// ========================
const int IR_PIN  = 36;   // IR Receiver OUT → GPIO36
const int STATUS_LED = 5; // 配对指示灯 → GPIO5

// ========================
// WiFi AP
// ========================
const char* ap_ssid = "esp_ap";
const char* ap_pass = "12345678";
WiFiServer server(12345);

WiFiClient client;
bool paired = false;

// ========================
// NeoPixel Strip
// ========================
#define LED_PIN 18      // !!! 如果灯不亮，请换为 13
#define LED_COUNT 60

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

uint8_t brightness_level = 80;
bool isOn = true;

// 动画状态机
String current_cmd = "";
String next_cmd = "";
unsigned long lastFrame = 0;
int anim_offset = 0;
byte rainbowPos = 0;


// =============================
// 工具函数
// =============================
void applyBrightness() {
  strip.setBrightness(brightness_level);
  strip.show();
}

void clearStrip() {
  for (int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, 0);
  strip.show();
}


// =============================
// 亮度控制（非阻塞）
// =============================
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


// =============================
// 波浪动画（非阻塞）
// =============================
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


// =============================
// 彩虹效果
// =============================
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

  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, wheelColor((i * 256 / LED_COUNT + rainbowPos) & 255));
  }

  strip.show();
  rainbowPos += 4;
}


// =============================
// CLOSE（非阻塞）
// =============================
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


// =============================
// 动作状态机（非阻塞）
// =============================
void handleAction() {
  if (next_cmd != "") {
    current_cmd = next_cmd;

    Serial.println("[ACTION SWITCH] → " + current_cmd);

    next_cmd = "";
    anim_offset = 0;
  }

  if (current_cmd == "UP") doUp();
  else if (current_cmd == "DOWN") doDown();
  else if (current_cmd == "LEFT") doLeft();
  else if (current_cmd == "RIGHT") doRight();
  else if (current_cmd == "OPEN") doOpen();
  else if (current_cmd == "CLOSE") doClose();
}



// =============================
// IR 配对（检测 38kHz burst）
// =============================
void checkIRHitAndPair() {
  int level = digitalRead(IR_PIN);

  if (level == LOW) {
    digitalWrite(STATUS_LED, HIGH);

    if (!paired && client && client.connected()) {
      paired = true;

      Serial.printf("[IR] HIT → PAIRED as DEVICE %d\n", TERMINAL_DEVICE_ID);

      client.print("PAIR_OK," + String(TERMINAL_DEVICE_ID) + "\n");
      delay(150);
    }
  } else {
    digitalWrite(STATUS_LED, LOW);
  }
}


// =============================
// WiFi Command handler
// =============================
void handleWiFiCommands() {
  if (!client || !client.connected()) return;

  while (client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) return;

    // ⭐ 打印收到完整指令
    Serial.println("[CMD RECEIVED] " + line);

    int comma = line.indexOf(',');
    if (comma < 0) continue;

    String cmd = line.substring(0, comma);
    int id = line.substring(comma + 1).toInt();

    // ⭐ 打印解析
    Serial.printf("[PARSE] CMD=%s, ID=%d\n", cmd.c_str(), id);

    if (paired && id == TERMINAL_DEVICE_ID) {
      next_cmd = cmd;
    } else {
      Serial.printf("[IGNORE] paired=%d, my_id=%d, received=%d\n",
                    paired, TERMINAL_DEVICE_ID, id);
    }
  }
}



// =============================
// Setup
// =============================
void setup() {
  Serial.begin(115200);

  pinMode(IR_PIN, INPUT);
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_pass);
  server.begin();

  strip.begin();
  strip.setBrightness(brightness_level);
  clearStrip();

  Serial.println("ESP32 LED Device Ready.");
}


// =============================
// Main Loop
// =============================
void loop() {
  // —— 处理新连接 ——
  if (!client || !client.connected()) {
    WiFiClient newClient = server.available();
    if (newClient) {
      client = newClient;
      Serial.println("[WiFi] Wristband connected!");
    }
  }

  handleWiFiCommands();
  checkIRHitAndPair();
  handleAction();

  delay(5);
}
