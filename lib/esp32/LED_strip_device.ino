#include <WiFi.h>
#include <Adafruit_NeoPixel.h>

const char* ap_ssid = "esp_ap";
const char* ap_pass = "12345678";
WiFiServer server(12345);

int my_device_id = -1;
bool paired = false;

#define LED_PIN 18
#define LED_COUNT 60

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

uint8_t brightness_level = 80;
bool isOn = true;

String current_cmd = "";      // 当前动作
String next_cmd = "";         // 下一次收到的动作（用于打断）
unsigned long lastFrame = 0;  // 记录动画时间
int anim_offset = 0;          // 左右波浪位置


// =============================
// 辅助函数
// =============================
void applyBrightness() {
  strip.setBrightness(brightness_level);
  strip.show();
}

void clearStrip() {
  for (int i = 0; i < LED_COUNT; i++)
    strip.setPixelColor(i, 0);
  strip.show();
}


// =============================
// UP/DOWN 非阻塞
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
// LEFT 波浪（左 → 右） 非阻塞
// =============================
void doLeft() {
  if (millis() - lastFrame < 35) return;
  lastFrame = millis();

  for (int i = 0; i < LED_COUNT; i++) {
    int index = (i + anim_offset) % LED_COUNT;
    uint8_t intensity = (sin((i + anim_offset) * 0.12) + 1) * 120;
    strip.setPixelColor(index, strip.Color(255, 60 + intensity/2, 0));
  }

  strip.show();
  anim_offset = (anim_offset + 1) % LED_COUNT;
}


// =============================
// RIGHT 波浪（右 → 左） 非阻塞
// =============================
void doRight() {
  if (millis() - lastFrame < 35) return;
  lastFrame = millis();

  for (int i = 0; i < LED_COUNT; i++) {
    int index = (LED_COUNT - 1 - i + anim_offset) % LED_COUNT;
    uint8_t intensity = (sin((i + anim_offset) * 0.12) + 1) * 120;
    strip.setPixelColor(index, strip.Color(0, intensity, 255));
  }

  strip.show();
  anim_offset = (anim_offset + 1) % LED_COUNT;
}


// =============================
// OPEN 非阻塞彩虹
// =============================
byte rainbowPos = 0;

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


// =============================
// CLOSE：非阻塞渐暗
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
// 执行当前动作（非阻塞）
// =============================
void handleAction() {
  if (next_cmd != "") {
    current_cmd = next_cmd;
    next_cmd = "";
    anim_offset = 0;
  }

  if (current_cmd == "UP")          doUp();
  else if (current_cmd == "DOWN")   doDown();
  else if (current_cmd == "LEFT")   doLeft();
  else if (current_cmd == "RIGHT")  doRight();
  else if (current_cmd == "OPEN")   doOpen();
  else if (current_cmd == "CLOSE")  doClose();
}



// =============================
// Fake IR pairing
// =============================
void checkSerialForFakeIR(WiFiClient& client) {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();

    if (line.startsWith("PAIR")) {
      int id = line.substring(5).toInt();
      my_device_id = id;
      paired = true;

      Serial.printf("[FAKE IR] Paired with ID: %d\n", id);
      if (client.connected()) client.print("PAIR_OK," + String(id) + "\n");
    }
  }
}


// =============================
// Setup
// =============================
void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_pass);
  server.begin();

  strip.begin();
  strip.setBrightness(brightness_level);
  clearStrip();
}


// =============================
// Main Loop
// =============================
void loop() {
  WiFiClient client = server.available();

  if (client) {
    Serial.println("[WiFi] Wristband connected!");

    while (client.connected()) {

      checkSerialForFakeIR(client);

      if (client.available()) {
        String line = client.readStringUntil('\n');
        line.trim();

        int comma = line.indexOf(',');
        if (comma >= 0) {
          String cmd = line.substring(0, comma);
          int dev_id = line.substring(comma + 1).toInt();

          if (paired && dev_id == my_device_id) {
            next_cmd = cmd;   // ⭐ 非阻塞：存到 next_cmd
          }
        }
      }

      handleAction();  // ⭐ 非阻塞动画执行
    }
  }
}
