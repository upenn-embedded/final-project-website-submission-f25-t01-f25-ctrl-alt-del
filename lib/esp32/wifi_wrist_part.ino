#include <WiFi.h>

const char* ssid = "esp_ap";
const char* pass = "12345678";

WiFiClient client;

int paired_device_id = -1;

// ========== 模拟手势（用串口输入） ==========
String getFakeGesture() {
  if (Serial.available()) {
    String g = Serial.readStringUntil('\n');
    g.trim();
    return g;
  }
  return "";
}

void sendGesture(String gesture) {
  if (paired_device_id < 0) {
    Serial.println("[WARN] Not paired yet.");
    return;
  }

  String msg = gesture + "," + String(paired_device_id);
  client.print(msg + "\n");
  Serial.println("[Wristband → WiFi] " + msg);
}

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

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  Serial.println("Connecting to AP...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }

  Serial.println("\nConnected to WiFi.");
  connectToServer();
}

void loop() {
  // === 1. Listen for "PAIR_OK" ===
  if (client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();

    Serial.println("[Appliance → Wristband] " + line);

    if (line.startsWith("PAIR_OK")) {
      int comma = line.indexOf(',');
      paired_device_id = line.substring(comma + 1).toInt();
      Serial.printf("[INFO] Paired with device %d\n", paired_device_id);
    }
  }

  // === 2. Fake gesture input ===
  String gesture = getFakeGesture();
  if (gesture.length() > 0) {
    sendGesture(gesture);
  }

  delay(20);
}
