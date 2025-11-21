#include <WiFi.h>

const char* ap_ssid = "esp_ap";
const char* ap_pass = "12345678";

WiFiServer server(12345);

int my_device_id = -1;  // the device this ESP32 represents
bool paired = false;

void sendToWristband(WiFiClient& client, String msg) {
  client.print(msg + "\n");
  Serial.println("[WiFi → Wristband] " + msg);
}

// ========== 模拟 IR 接收（用串口替代 IRReceiver） ==========
void checkSerialForFakeIR(WiFiClient& client) {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();

    // 用户在串口输入例如： PAIR 3
    if (line.startsWith("PAIR")) {
      int id = line.substring(5).toInt(); 
      my_device_id = id;
      paired = true;

      Serial.printf("[FAKE IR] Device paired with ID: %d\n", id);

      if (client.connected()) {
        sendToWristband(client, "PAIR_OK," + String(id));
      }
    }
  }
}

// ========== 处理手环发来的动作 ==========
void processAction(String cmd) {
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

void setup() {
  Serial.begin(115200);

  // Wi-Fi: Start AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_pass);

  Serial.println("Appliance ESP32 started as AP.");
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  server.begin();
}

void loop() {
  WiFiClient client = server.available();

  if (client) {
    Serial.println("[WiFi] Wristband connected!");

    while (client.connected()) {
      // 1. Check 'fake IR'
      checkSerialForFakeIR(client);

      // 2. Handle Wi-Fi command
      if (client.available()) {
        String line = client.readStringUntil('\n');
        line.trim();

        Serial.println("[WiFi → Appliance] " + line);

        int comma = line.indexOf(',');
        if (comma < 0) continue;

        String cmd = line.substring(0, comma);
        int dev_id = line.substring(comma + 1).toInt();

        if (paired && dev_id == my_device_id) {
          processAction(cmd);
        } else {
          Serial.println("[INFO] Command ignored (device ID mismatch)");
        }
      }
    }

    Serial.println("[WiFi] Wristband disconnected!");
  }
}

