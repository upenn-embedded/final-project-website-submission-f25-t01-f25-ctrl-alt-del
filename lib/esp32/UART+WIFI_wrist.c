/**
 * Wristband ESP32-S2:
 *  - UART1 <--> ATmega328PB (gesture chars: U / D / L / R + \r\n)
 *  - WiFi client <--> Appliance ESP32 (TCP, sends "UP,<id>" etc.)
 *
 * USB Serial (Serial): debug + optional fake gesture input.
 */

#include <Arduino.h>
#include <WiFi.h>

// ====== WiFi config (same as before) ======
const char* ssid = "esp_ap";
const char* pass = "12345678";

WiFiClient client;
int paired_device_id = -1;

// ====== UART to ATmega ======
// ESP32-S2 pins you used in the test sketch:
#define MEGA_RX_PIN 44    // ESP32-S2 receives  <-- ATmega TX (PD1, via divider)
#define MEGA_TX_PIN 43    // ESP32-S2 sends     --> ATmega RX (PD0)

// --------- 从 USB 串口读假手势（保留调试用） ----------
String getFakeGestureFromUSB() {
  if (Serial.available()) {
    String g = Serial.readStringUntil('\n');
    g.trim();
    return g;   // EXPECT: "UP", "DOWN", "LEFT", "RIGHT"
  }
  return "";
}

// --------- 从 ATmega UART 读手势字符 ----------
String getGestureFromMega() {
  // ATmega 发送的是：'U' / 'D' / 'L' / 'R' + "\r\n"
  while (Serial1.available()) {
    char c = (char)Serial1.read();

    // 丢弃换行符
    if (c == '\r' || c == '\n') {
      continue;
    }

    // 映射成字符串命令
    switch (c) {
      case 'U':
        Serial.println("[UART] Gesture from Mega: U");
        return "UP";
      case 'D':
        Serial.println("[UART] Gesture from Mega: D");
        return "DOWN";
      case 'L':
        Serial.println("[UART] Gesture from Mega: L");
        return "LEFT";
      case 'R':
        Serial.println("[UART] Gesture from Mega: R");
        return "RIGHT";
      default:
        // 其他字符（如果有）直接忽略
        Serial.print("[UART] Unknown char: ");
        Serial.println(c);
        break;
    }
  }
  return "";
}

// --------- 发送手势到终端 ESP32 ----------
void sendGesture(const String& gesture) {
  if (paired_device_id < 0) {
    Serial.println("[WARN] Not paired yet.");
    return;
  }

  String msg = gesture + "," + String(paired_device_id);
  client.print(msg + "\n");
  Serial.println("[Wristband → WiFi] " + msg);
}

// --------- 连接到 AP + 服务器 ----------
void connectToServer() {
  Serial.println("Connecting to appliance...");

  while (!client.connect("192.168.4.1", 12345)) {
    Serial.println("Retry...");
    delay(500);
  }

  Serial.println("Connected to appliance.");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[Wristband ESP32] Booting...");

  // === UART1 <-> ATmega ===
  // 波特率 9600，需要和 ATmega uart0_init() 一致
  Serial1.begin(9600, SERIAL_8N1, MEGA_RX_PIN, MEGA_TX_PIN);
  Serial.println("[UART] Serial1 started at 9600 for ATmega.");

  // === WiFi STA 模式 ===
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  Serial.println("Connecting to AP...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi.");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  connectToServer();
}

void loop() {
  // === 1. 处理来自终端 ESP32 的消息（包括 PAIR_OK） ===
  if (client.connected() && client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();

    Serial.println("[Appliance → Wristband] " + line);

    if (line.startsWith("PAIR_OK")) {
      int comma = line.indexOf(',');
      if (comma > 0) {
        paired_device_id = line.substring(comma + 1).toInt();
        Serial.printf("[INFO] Paired with device %d\n", paired_device_id);
      }
    }
  } else if (!client.connected()) {
    // 如果断开了，尝试重连
    Serial.println("[WARN] Disconnected from appliance, reconnecting...");
    connectToServer();
  }

  // === 2. 优先从 ATmega UART 读手势 ===
  String gesture = getGestureFromMega();

  // === 3. 如果没有 ATmega 手势，就允许从 USB 串口输入假手势（调试） ===
  if (gesture.length() == 0) {
    gesture = getFakeGestureFromUSB();
  }

  // === 4. 有手势就发 WiFi ===
  if (gesture.length() > 0) {
    sendGesture(gesture);
  }

  delay(20);
}
