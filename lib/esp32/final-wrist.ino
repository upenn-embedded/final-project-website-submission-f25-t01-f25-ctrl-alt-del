/**
 * Wristband ESP32-S2:
 *  - UART1 <--> ATmega328PB (U/D/L/R + O/C + optional 'P' for IR)
 *  - WiFi client <--> Appliance ESP32
 *
 * USB Serial: debug + fake gesture input
 */

#include <Arduino.h>
#include <WiFi.h>

// ===== WiFi =====
const char* ssid = "esp_ap";
const char* pass = "12345678";

WiFiClient client;
int paired_device_id = -1;

// ===== UART pins =====
#define MEGA_RX_PIN 44
#define MEGA_TX_PIN 43

// ==================== 手势解析（包括 O / C / P） ====================
String parseGestureChar(char c)
{
  switch (c)
  {
    case 'U': Serial.println("[UART] U"); return "UP";
    case 'D': Serial.println("[UART] D"); return "DOWN";
    case 'L': Serial.println("[UART] L"); return "LEFT";
    case 'R': Serial.println("[UART] R"); return "RIGHT";

    case 'O': Serial.println("[UART] O"); return "OPEN";
    case 'C': Serial.println("[UART] C"); return "CLOSE";

    // ==== P（IR 手势）不发 WiFi ====
    case 'P':
      Serial.println("[UART] P (IR pairing triggered)");
      // ★★★ 不发 WiFi，直接忽略 ★★★
      return "";

    default:
      Serial.print("[UART] Unknown char: ");
      Serial.println(c);
      return "";
  }
}

// ==================== 读 ATmega UART ====================
String getGestureFromMega()
{
  while (Serial1.available())
  {
    char c = (char)Serial1.read();

    // 丢掉换行符
    if (c == '\r' || c == '\n') continue;

    return parseGestureChar(c);
  }
  return "";
}

// ==================== USB 调试假手势 ====================
String getFakeGestureFromUSB()
{
  if (Serial.available()) {
    String s = Serial.readStringUntil('\n');
    s.trim();
    return s;
  }
  return "";
}

// ==================== WiFi 发消息 ====================
void sendGesture(const String& g)
{
  if (paired_device_id < 0) {
    Serial.println("[WARN] Not paired yet.");
    return;
  }

  String msg = g + "," + String(paired_device_id);
  client.print(msg + "\n");

  Serial.println("[Wristband → WiFi] " + msg);
}

// ==================== 与 appliance 建立 TCP ====================
void connectToServer()
{
  Serial.println("Connecting to appliance...");

  while (!client.connect("192.168.4.1", 12345)) {
    Serial.println("Retry...");
    delay(500);
  }
  Serial.println("Connected to appliance.");
}

// ==================== setup ====================
void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[Wristband ESP32] Booting...");

  Serial1.begin(9600, SERIAL_8N1, MEGA_RX_PIN, MEGA_TX_PIN);
  Serial.println("[UART] Serial1 ready");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  Serial.print("Connecting to AP...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");
  Serial.println(WiFi.localIP());

  connectToServer();
}

// ==================== loop ====================
void loop()
{
  // --- 1. appliance → wristband ---
  if (client.connected() && client.available())
  {
    String line = client.readStringUntil('\n');
    line.trim();
    Serial.println("[Appliance → Wristband] " + line);

    // PAIR_OK,deviceId
    if (line.startsWith("PAIR_OK"))
    {
      int comma = line.indexOf(',');
      if (comma > 0) {
        paired_device_id = line.substring(comma + 1).toInt();
        Serial.printf("[INFO] Paired with device %d\n", paired_device_id);
      }
    }
  }
  else if (!client.connected())
  {
    Serial.println("[WARN] Lost connection, reconnecting...");
    connectToServer();
  }

  // --- 2. read from ATmega first ---
  String gesture = getGestureFromMega();

  // --- 3. if ATmega没有，则读USB调试输入 ---
  if (gesture.length() == 0)
    gesture = getFakeGestureFromUSB();

  // --- 4. P 会返回 ""，不会发 WiFi ---
  if (gesture.length() > 0)
    sendGesture(gesture);

  delay(20);
}