/**
 * wristband_esp32s2.ino  (finalwrist-v3)
 *
 * Wristband ESP32-S2  (AP + Multi-device Server + TFT Display)
 * ------------------------------------------------------------
 * - Wristband = WiFi AP + TCP Server
 * - LED/MOTOR = 同一块 ESP32 → 使用 HELLO,1,2
 * - MUSIC    = 单独 ESP32   → 使用 HELLO,3
 * - AC       = 单独 ESP32   → 使用 HELLO,4
 * - PAIR_OK,<ID> 必须以 ID 为准（而不是 socket index）
 * - 多个 ID 可以共用同一个 socket
 * - TFT 显示：
 *      - 当前配对设备名称（LED / MOTOR / MUSIC / AC）
 *      - 最近一次发送的手势
 *      - Hello from: 所有已 HELLO 且仍连接的设备名称
 */

#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

// ===================================================
// WiFi AP
// ===================================================
const char* ap_ssid = "wrist_ap";
const char* ap_pass = "12345678";

WiFiServer server(12345);

// 每个设备的 socket（1=LED, 2=MOTOR, 3=MUSIC, 4=AC）
WiFiClient deviceSockets[5];
bool deviceConnected[5] = {false, false, false, false, false};

// 当前配对的设备 ID（1~4），-1 表示未配对
int paired_device_id = -1;

// 最近一次发送出去的手势字符串
String lastGesture = "";

// ===================================================
// UART pins (ATmega328PB)  —— 使用 ESP32-S2 的 GPIO 号
// ===================================================
#define MEGA_RX_PIN 44      // ESP32-S2 接收 → ATmega TX
#define MEGA_TX_PIN 43      // ESP32-S2 发送 → ATmega RX

// ===================================================
// TFT pins (ST7735 160x128 SPI) —— 使用 GPIO 号
// ===================================================
// 硬件 SPI: SCK=IO36, MOSI=IO35（接到屏幕 MOSI, esp32的SDO）
#define TFT_CS   12   // 接 TFT_TCS（板子上 D17 → IO12）
#define TFT_DC   14   // 接 TFT_DC（D16 → IO14）
#define TFT_RST  18   // 接 RESET（D15 → IO18）
#define TFT_BL   11   // 接 LITE（D13 → IO11，背光开关）

Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);

// ===================================================
// 设备 ID → 名称 映射
// ===================================================
String deviceName(int id)
{
  switch (id) {
    case 1: return "LED";
    case 2: return "MOTOR";
    case 3: return "MUSIC";
    case 4: return "AC";
    default: return "?";
  }
}

// ===================================================
// Gesture Parsing
// ===================================================
String parseGestureChar(char c)
{
  switch (c)
  {
    case 'U': return "UP";
    case 'D': return "DOWN";
    case 'L': return "LEFT";
    case 'R': return "RIGHT";
    case 'O': return "OPEN";
    case 'C': return "CLOSE";

    case 'P':  // IR gesture (pair only)
      Serial.println("[UART] IR Pairing Triggered");
      return "";

    default:
      Serial.print("[UART] Unknown: ");
      Serial.println(c);
      return "";
  }
}

// 从 ATmega328PB(Serial1) 读取一个手势字符
String getGestureFromMega()
{
  while (Serial1.available())
  {
    char c = Serial1.read();
    if (c == '\r' || c == '\n') continue;
    return parseGestureChar(c);
  }
  return "";
}

// USB 串口 fake 手势（调试用，例如输入 UP 回车）
String getFakeGestureFromUSB()
{
  if (Serial.available()) {
    String s = Serial.readStringUntil('\n');
    s.trim();
    return s;
  }
  return "";
}

// ===================================================
// TFT 显示相关函数
// ===================================================

void tftShowPairInfo();
void tftShowGestureInfo();
void tftShowHelloFrom();

// 初始化 UI：渲染静态文字
void tftInitUI()
{
  tft.fillScreen(ST77XX_BLACK);

  // 标题
  tft.setTextWrap(false);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.setCursor(5, 5);
  tft.print("Wristband");

  // 分割线
  tft.drawLine(0, 22, 160, 22, ST77XX_WHITE);

  // 静态标签
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(5, 30);
  tft.print("Paired Device:");

  tft.setCursor(5, 60);
  tft.print("Last Gesture :");

  tft.setCursor(5, 90);
  tft.print("Hello from:");

  // 初始化动态区
  tftShowPairInfo();
  tftShowGestureInfo();
  tftShowHelloFrom();
}

// 更新“配对设备”显示（显示名称）
void tftShowPairInfo()
{
  tft.fillRect(5, 40, 150, 12, ST77XX_BLACK);
  tft.setCursor(5, 40);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setTextSize(1);

  if (paired_device_id < 0) {
    tft.print("None");
  } else {
    tft.print(deviceName(paired_device_id));  // 用名称代替 #ID
  }
}

// 更新“最近手势”显示
void tftShowGestureInfo()
{
  tft.fillRect(5, 70, 150, 12, ST77XX_BLACK);
  tft.setCursor(5, 70);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(1);

  if (lastGesture.length() == 0) {
    tft.print("-");
  } else {
    tft.print(lastGesture);
  }
}

// 更新 “Hello from: XXX” 显示
// 显示所有 deviceConnected[id] == true 的设备名称，用逗号分隔
void tftShowHelloFrom()
{
  tft.fillRect(5, 100, 150, 12, ST77XX_BLACK);
  tft.setCursor(5, 100);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setTextSize(1);

  String list = "";
  for (int id = 1; id <= 4; id++) {
    if (deviceConnected[id]) {
      if (list.length() > 0) list += ",";
      list += deviceName(id);    // 显示名称而不是数字
    }
  }

  if (list.length() == 0) {
    tft.print("-");
  } else {
    tft.print(list);             // 例如：LED,MOTOR,AC
  }
}

// 背光控制（简单版：0=关，其它=开）
void setBacklight(uint8_t level)
{
  digitalWrite(TFT_BL, level ? HIGH : LOW);
}

// ===================================================
// Send Gesture to paired device
// ===================================================
void sendGesture(const String& g)
{
  if (paired_device_id < 0) {
    Serial.println("[WARN] No paired device.");
    return;
  }

  if (!deviceConnected[paired_device_id]) {
    Serial.printf("[WARN] Device %d not connected.\n", paired_device_id);
    return;
  }

  String msg = g + "," + String(paired_device_id);
  deviceSockets[paired_device_id].print(msg + "\n");

  Serial.println("[SEND] " + msg);

  // 更新显示
  lastGesture = g;
  tftShowGestureInfo();
}

// ===================================================
// Register Device IDs from HELLO,1,2,4
// ===================================================
void registerDeviceIds(WiFiClient& sock, const String& hello)
{
  int pos = hello.indexOf(',');
  if (pos < 0) return;

  int start = pos + 1;

  while (start < (int)hello.length())
  {
    int comma = hello.indexOf(',', start);
    String idStr;

    if (comma < 0) {
      idStr = hello.substring(start);
      start = hello.length();
    } else {
      idStr = hello.substring(start, comma);
      start = comma + 1;
    }

    idStr.trim();
    int id = idStr.toInt();

    if (id >= 1 && id <= 4) {
      deviceSockets[id] = sock;
      deviceConnected[id] = true;
      Serial.printf("[REGISTER] Device %d mapped to socket.\n", id);
    }
  }
}

// ===================================================
// Setup
// ===================================================
void setup()
{
  Serial.begin(115200);
  delay(200);

  // UART1 用于和 ATmega328PB 通信
  Serial1.begin(9600, SERIAL_8N1, MEGA_RX_PIN, MEGA_TX_PIN);
  Serial.println("[UART] Serial1 Ready");

  // ---------------- TFT + 背光初始化 ----------------
  // 硬件 SPI：SCK=36, MISO=37(不用), MOSI=35
  SPI.begin(36, 37, 35, TFT_CS);

  pinMode(TFT_BL, OUTPUT);
  setBacklight(255);   // 先开到最亮

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);

  // ---------------- Wi-Fi AP ----------------
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_pass);

  Serial.print("[WiFi] AP Ready. IP = ");
  Serial.println(WiFi.softAPIP());

  server.begin();
  Serial.println("[WiFi] TCP Server Started");

  // UI 初始化放在 WiFi 后面，这样可以显示初始状态
  tftInitUI();
}

// ===================================================
// Loop
// ===================================================
void loop()
{
  // ===================================================
  // 1. Accept new device connection
  // ===================================================
  WiFiClient newClient = server.available();

  if (newClient)
  {
    Serial.println("[WiFi] New device connected. Waiting for HELLO...");

    unsigned long t0 = millis();
    while (!newClient.available() && millis() - t0 < 2000) {
      delay(5);
    }

    if (!newClient.available()) {
      Serial.println("[WiFi] HELLO timeout.");
      newClient.stop();
    }
    else {
      String hello = newClient.readStringUntil('\n');
      hello.trim();

      Serial.println("[Device → Wristband] " + hello);

      if (hello.startsWith("HELLO")) {
        registerDeviceIds(newClient, hello);
        tftShowHelloFrom();   // 每次有新 HELLO 就刷新 “Hello from”
      } else {
        newClient.stop();
      }
    }
  }

  // ===================================================
  // 2. Handle messages from devices (PAIR_OK,ID)
  // ===================================================
  for (int id = 1; id <= 4; id++)
  {
    if (!deviceConnected[id]) continue;

    WiFiClient& sock = deviceSockets[id];

    if (!sock.connected()) {
      Serial.printf("[WiFi] Device %d disconnected.\n", id);
      deviceConnected[id] = false;
      tftShowHelloFrom();      // 掉线时也刷新 “Hello from”
      continue;
    }

    while (sock.available())
    {
      String s = sock.readStringUntil('\n');
      s.trim();
      if (s.length() == 0) continue;

      Serial.printf("[Device %d → Wristband] %s\n", id, s.c_str());

      // -------------------------
      // PAIR_OK Handler
      // -------------------------
      if (s.startsWith("PAIR_OK"))
      {
        int comma = s.indexOf(',');
        int real_id;

        if (comma > 0)
          real_id = s.substring(comma + 1).toInt();
        else
          real_id = id;  // fallback

        paired_device_id = real_id;

        Serial.printf("[PAIR] Now paired with device %d\n",
                      paired_device_id);

        // TFT 更新配对信息（显示名称）
        tftShowPairInfo();
      }
    }
  }

  // ===================================================
  // 3. Handle gestures (from ATmega or USB debug)
  // ===================================================
  String gesture = getGestureFromMega();

  if (gesture.length() == 0)
    gesture = getFakeGestureFromUSB();

  if (gesture.length() > 0)
    sendGesture(gesture);

  delay(10);
}
