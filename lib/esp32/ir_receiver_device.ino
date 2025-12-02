#include <WiFi.h>

// ====== 硬件引脚配置 ======
const int IR_PIN  = 36;   // IR Receiver OUT → GPIO36
const int LED_PIN = 5;    // 指示灯 → GPIO5（你可以根据板子改）

// 这个 ESP32 终端在系统里的 ID（每个终端写一个不一样的）
const int TERMINAL_DEVICE_ID = 1;  

// ====== Wi-Fi AP 配置 ======
const char* ap_ssid = "esp_ap";
const char* ap_pass = "12345678";

WiFiServer server(12345);
WiFiClient client;        // 当前连接的 Wristband

bool paired = false;      // 是否已经通过 IR 完成配对

// ========== 工具函数 ==========

// 发送字符串给 Wristband（加换行）
void sendToWristband(WiFiClient& c, const String& msg) {
  c.print(msg + "\n");
  Serial.println("[WiFi → Wristband] " + msg);
}

// 处理手环发来的动作命令
void processAction(const String& cmd) {
  Serial.printf("[ACTION] Command received: %s\n", cmd.c_str());

  if (cmd == "UP") {
    Serial.println("→ Perform action: UP");
  } else if (cmd == "DOWN") {
    Serial.println("→ Perform action: DOWN");
  } else if (cmd == "LEFT") {
    Serial.println("→ Perform action: LEFT");
  } else if (cmd == "RIGHT") {
    Serial.println("→ Perform action: RIGHT");
  } else if (cmd == "OPEN") {
    Serial.println("→ Perform action: OPEN");
  } else if (cmd == "CLOSE") {
    Serial.println("→ Perform action: CLOSE");
  }
}

// 检测 IR 命中：当 OUT 变 LOW，认为有 38kHz burst
void checkIRHitAndPair() {
  int level = digitalRead(IR_PIN);   // 空闲一般是 HIGH，收到 38kHz 时会有很多 LOW 片段

  if (level == LOW) {
    // 收到 IR，点亮 LED
    digitalWrite(LED_PIN, HIGH);

    // 如果还没配对，且有 wristband 已连接，就发 PAIR_OK
    if (!paired && client && client.connected()) {
      paired = true;

      Serial.printf("[IR] HIT detected, pairing with device_id = %d\n", TERMINAL_DEVICE_ID);
      sendToWristband(client, "PAIR_OK," + String(TERMINAL_DEVICE_ID));

      // 简单防抖，避免连续多次触发
      delay(200);  
    }
  } else {
    digitalWrite(LED_PIN, LOW);
  }
}

// 处理 wristband 通过 WiFi 发来的命令："CMD,ID"
void handleWiFiCommands() {
  if (!client || !client.connected()) return;

  while (client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();

    if (line.length() == 0) continue;

    Serial.println("[WiFi → Appliance] " + line);

    int comma = line.indexOf(',');
    if (comma < 0) continue;   // 格式不对

    String cmd    = line.substring(0, comma);
    int   dev_id = line.substring(comma + 1).toInt();

    // 只处理已配对的设备 ID
    if (paired && dev_id == TERMINAL_DEVICE_ID) {
      processAction(cmd);
    } else {
      Serial.printf("[INFO] Command ignored (paired=%d, my_id=%d, got_id=%d)\n",
                    paired, TERMINAL_DEVICE_ID, dev_id);
    }
  }
}

// ========== 初始化 ==========
void setup() {
  Serial.begin(115200);
  delay(100);

  // IR 引脚和 LED 引脚
  pinMode(IR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Wi-Fi: 启动 AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_pass);

  Serial.println("Appliance ESP32 started as AP.");
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  server.begin();
  Serial.println("TCP server started on port 12345");
}

// ========== 主循环 ==========
void loop() {
  // 1. 接收新连接的 wristband（只保留一个）
  if (!client || !client.connected()) {
    WiFiClient newClient = server.available();
    if (newClient) {
      client = newClient;  // 覆盖旧的
      Serial.println("[WiFi] Wristband connected!");
    }
  }

  // 2. 处理 WiFi 命令
  handleWiFiCommands();

  // 3. 检测 IR 命中并完成配对
  checkIRHitAndPair();

  // 轻微延时，减小 CPU 占用
  delay(10);
}