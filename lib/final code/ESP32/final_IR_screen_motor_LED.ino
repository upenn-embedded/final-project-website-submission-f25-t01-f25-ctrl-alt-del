#include <WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
  
/******************************************************
 * TERMINAL ESP32 for LED (1) + MOTOR (2) + AC TFT (4)
 * - Supports HELLO,1,2,4
 * - IR pairing for 1/2/4
 * - AC UI: temp/mode + wind animation + power fade
 * - LED 灯带：修复亮度 DOWN/UP 问题 & 降低闪烁感
 ******************************************************/

// ==========================
// Device IDs
// ==========================
#define DEVICE_LED     1  
#define DEVICE_MOTOR   2  
#define DEVICE_AC      4

// ==========================
// IR Pins
// ==========================
#define IR_PIN_LED     33
#define IR_PIN_MOTOR   38
#define IR_PIN_AC      9

// ==========================
// WiFi
// ==========================
const char* ssid = "wrist_ap";  
const char* pass = "12345678";

WiFiClient client;
bool wifi_connected = false;

// ==========================
// Status LED
// ==========================
#define STATUS_LED 2

// ==========================
// LED Strip Config (Device 1)
// ==========================
#define LED_PIN   18
#define LED_COUNT 60

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// 亮度 0~255：用它手动缩放颜色，不再用 strip.setBrightness 动态调
int brightness_level = 150;

// LED 指令状态
String next_cmd_led     = "";
String current_cmd_led  = "";

int ledMode = 0;  // 0 常亮, 1 呼吸灯, 2 跑马灯
unsigned long lastLedUpdate = 0;

// 颜色按 brightness_level 缩放
uint32_t colorWithBrightness(uint8_t r, uint8_t g, uint8_t b){
  uint16_t scale = brightness_level;      // 0~255
  r = (uint16_t)r * scale / 255;
  g = (uint16_t)g * scale / 255;
  b = (uint16_t)b * scale / 255;
  return strip.Color(r, g, b);
}

// 设置所有灯为同一颜色（已是缩放后的颜色）
void ledSetAll(uint32_t color){
  for(int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, color);
  }
  strip.show();
}

// 亮度限制在 0~255
void clampBrightness(){
  if (brightness_level < 0)   brightness_level = 0;
  if (brightness_level > 255) brightness_level = 255;
}

// OPEN — 白光常亮，亮度由 brightness_level 决定
void ledOPEN(){
  ledMode = 0;
  clampBrightness();
  uint8_t v = brightness_level;   // 直接用亮度作为白光强度
  ledSetAll(strip.Color(v, v, v));
}

// CLOSE -- 只关灯，不改 brightness_level
void ledCLOSE(){
  ledMode = 0;
  ledSetAll(0);
}

// 亮度升高
void ledUP(){
  brightness_level += 20;
  clampBrightness();

  if (ledMode == 0) {
    uint8_t v = brightness_level;
    ledSetAll(strip.Color(v, v, v));
  }
}

// 亮度降低
void ledDOWN(){
  brightness_level -= 20;
  clampBrightness();

  if (ledMode == 0) {
    uint8_t v = brightness_level;
    ledSetAll(strip.Color(v, v, v));
  }
}

// 模式切换（切模式后由动画函数负责刷新）
void ledLEFT(){ 
  ledMode = 1; 
}

void ledRIGHT(){ 
  ledMode = 2; 
}

// 模式1：呼吸灯（速度放慢一点，更柔和）
void ledMode1(){
  if (millis() - lastLedUpdate < 40) return; // 原来 20ms → 40ms
  lastLedUpdate = millis();

  static int t = 0;
  t = (t + 1) & 0xFF; // 0~255 循环

  // 生成 0~255 的呼吸波形
  float s = sin(t * 0.0245f);    // -1 ~ 1
  uint8_t base = (uint8_t)((s + 1.0f) * 127.5f); // 0~255

  // 再乘上全局亮度
  uint8_t b = (uint16_t)base * brightness_level / 255;

  ledSetAll(strip.Color(b, 0, b));   // 紫色呼吸灯
}

// 模式2：跑马灯（速度减慢）
void ledMode2(){
  if (millis() - lastLedUpdate < 100) return; // 原来 40ms → 100ms
  lastLedUpdate = millis();

  static int offset = 0;
  offset = (offset + 1) % LED_COUNT;

  for(int i = 0; i < LED_COUNT; i++){
    if ((i + offset) % 10 < 5) {
      // 基础颜色 (0,150,255) 再乘上全局亮度
      uint8_t r = 0;
      uint8_t g = (uint16_t)150 * brightness_level / 255;
      uint8_t b = (uint16_t)255 * brightness_level / 255;
      strip.setPixelColor(i, strip.Color(r, g, b));
    } else {
      strip.setPixelColor(i, 0);
    }
  }
  strip.show();
}

// 主 LED 执行流程
void handleLEDAction(){
  // 从 next_cmd_led 取出最新指令
  if (next_cmd_led != "") {
    current_cmd_led = next_cmd_led;
    next_cmd_led = "";
  }

  // 执行一次性指令
  if (current_cmd_led != "") {
    String cmd = current_cmd_led;
    current_cmd_led = "";

    if      (cmd == "OPEN")  ledOPEN();
    else if (cmd == "CLOSE") ledCLOSE();
    else if (cmd == "UP")    ledUP();
    else if (cmd == "DOWN")  ledDOWN();
    else if (cmd == "LEFT")  ledLEFT();
    else if (cmd == "RIGHT") ledRIGHT();
  }

  // 根据模式跑动画
  if      (ledMode == 1) ledMode1();
  else if (ledMode == 2) ledMode2();
}


// ==========================
// Motor Config (Device 2)
// ==========================
#define MOTOR_PWM_PIN 17

int  motorSpeed       = 0;
int  normalSpeed      = 150;
bool intermittentMode = false;
bool motorOn          = false;
unsigned long lastToggle = 0;

// ==========================
// TFT AC Display (Device 4)
// ==========================
#define TFT_CS   44
#define TFT_DC   43
#define TFT_RST  5
#define TFT_MOSI 35
#define TFT_SCLK 36

SPIClass spiHSPI(HSPI);
Adafruit_ST7735 tft = Adafruit_ST7735(&spiHSPI, TFT_CS, TFT_DC, TFT_RST);

// AC State
bool   ac_isOn   = false;
int    ac_temp   = 24;
String ac_mode   = "COOL";
int    windOffset = 0;
int    powerFade  = 100;
unsigned long lastACWind = 0;


// =======================================================
// AC UI (TFT)
// =======================================================

uint16_t acWindColor1(int fade=255){
  int s = fade;
  if(ac_mode=="COOL") return tft.color565(0, s, 255);
  if(ac_mode=="HEAT") return tft.color565(s, 120, 0);
  if(ac_mode=="DRY")  return tft.color565(s, 0, s);
  if(ac_mode=="AUTO") return tft.color565(0, s, 180);
  return ST77XX_WHITE;
}

uint16_t acWindColor2(int fade=255){
  int s = fade * 0.7;   // 可以改成 int s = fade * 7 / 10; 避免浮点
  if(ac_mode=="COOL") return tft.color565(0, s, 200);
  if(ac_mode=="HEAT") return tft.color565(s, 80, 0);
  if(ac_mode=="DRY")  return tft.color565(s*0.6, 0, s*0.6);
  if(ac_mode=="AUTO") return tft.color565(0, s, 140);
  return ST77XX_WHITE;
}

void drawACIcon(){
  uint16_t c = tft.color565(powerFade, powerFade, powerFade);
  tft.fillRoundRect(20,20,120,40,6,c);
  tft.fillRect(25,50,110,3,tft.color565(230,230,230));
  tft.fillRect(25,25,3,20,tft.color565(120,120,120));
  tft.fillRect(132,25,3,20,tft.color565(120,120,120));
}

void drawACWindFade(int fade){
  tft.fillRect(20,64,120,36,ST77XX_BLACK);
  if(!ac_isOn && fade==0) return;

  uint16_t c1 = acWindColor1(fade), c2 = acWindColor2(fade);
  for(int i = 0; i < 3; i++){
    int y = 64 + windOffset + i*10;
    if(y < 100){
      tft.drawLine(35,y,110,y,c1);
      tft.drawLine(35,y+1,110,y+1,c2);
    }
  }
}

void drawACWind(){
  drawACWindFade(255);
  windOffset += 2;
  if(windOffset > 30) windOffset = 0;
}

void drawACTempLabel(){
  tft.fillRect(5,100,70,28,ST77XX_BLACK);
  tft.setCursor(5,100);
  tft.setTextSize(3);
  tft.setTextColor(ST77XX_WHITE);
  tft.printf("%d",ac_temp);
  tft.setTextSize(2);
  tft.print("C");
}

void drawACModeLabel(){
  tft.fillRect(85,100,70,28,ST77XX_BLACK);
  tft.setCursor(85,105);
  tft.setTextSize(2);
  tft.setTextColor(acWindColor1());
  tft.print(ac_mode);
}

void acAnimatePowerOn(){
  for(int i=100;i<=180;i+=10){
    powerFade=i;
    drawACIcon();
    delay(20);
  }
  for(int f=0;f<=255;f+=40){
    drawACWindFade(f);
    delay(20);
  }
}

void acAnimatePowerOff(){
  for(int f=255;f>=0;f-=40){
    drawACWindFade(f);
    delay(20);
  }
  tft.fillRect(20,64,120,36,ST77XX_BLACK);

  for(int i=180;i>=100;i-=10){
    powerFade=i;
    drawACIcon();
    delay(20);
  }
}

void acNextMode(){
  if(ac_mode=="COOL") ac_mode="HEAT";
  else if(ac_mode=="HEAT") ac_mode="DRY";
  else if(ac_mode=="DRY") ac_mode="AUTO";
  else ac_mode="COOL";
}

void acPrevMode(){
  if(ac_mode=="COOL") ac_mode="AUTO";
  else if(ac_mode=="AUTO") ac_mode="DRY";
  else if(ac_mode=="DRY") ac_mode="HEAT";
  else ac_mode="COOL";
}

void handleACCommand(const String& cmd){
  if(cmd=="UP"){
    ac_temp = min(ac_temp+1, 30);
    drawACTempLabel();
  }
  else if(cmd=="DOWN"){
    ac_temp = max(ac_temp-1, 16);
    drawACTempLabel();
  }
  else if(cmd=="LEFT"){
    acPrevMode();
    drawACModeLabel();
  }
  else if(cmd=="RIGHT"){
    acNextMode();
    drawACModeLabel();
  }
  else if(cmd=="OPEN"){
    if(!ac_isOn){
      ac_isOn = true;
      acAnimatePowerOn();
      drawACTempLabel();
      drawACModeLabel();
    }
  }
  else if(cmd=="CLOSE"){
    if(ac_isOn){
      ac_isOn = false;
      acAnimatePowerOff();
      drawACTempLabel();
      drawACModeLabel();
    }
  }
}


// =======================================================
// WiFi Connect
// =======================================================
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  Serial.print("[WiFi] Connecting");
  int tries = 0;

  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(200);
    Serial.print(".");
    tries++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] Connected: ");
    Serial.println(WiFi.localIP());
  }
}

void ensureWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    wifi_connected = false;
  }

  if (!client.connected()) {
    Serial.println("[WiFi] Connecting TCP...");
    if (client.connect(IPAddress(192,168,4,1), 12345)) {
      Serial.println("[WiFi] TCP Connected!");
      client.print("HELLO,1,2,4\n");
      wifi_connected = true;
    }
  }
}


// ==========================
// IR SAMPLE
// ==========================
bool checkIRHit(int pin) {
  int hit = 0;
  for (int i = 0; i < 5; i++) {
    if (digitalRead(pin) == LOW) hit++;
    delay(2);
  }
  return hit >= 3;
}


// ==========================
// Motor behavior
// ==========================
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

void motorStart()        { intermittentMode = false; motorSpeed = normalSpeed; motorOn = true;  analogWrite(MOTOR_PWM_PIN, motorSpeed); }
void motorStop()         { intermittentMode = false; motorSpeed = 0;          motorOn = false; analogWrite(MOTOR_PWM_PIN, 0); }
void motorNormal()       { intermittentMode = false; motorSpeed = normalSpeed;                 analogWrite(MOTOR_PWM_PIN, motorSpeed); }
void motorIntermittent() { intermittentMode = true; }
void motorSpeedUp()      { motorSpeed = min(255, motorSpeed + 25); analogWrite(MOTOR_PWM_PIN, motorSpeed); }
void motorSpeedDown()    { motorSpeed = max(0,   motorSpeed - 25); analogWrite(MOTOR_PWM_PIN, motorSpeed); }


// ==========================
// WiFi Command Router
// ==========================
void handleWiFiCommands(){
  if(!client.connected()) return;

  while(client.available()){
    String line = client.readStringUntil('\n');
    line.trim();
    if(line.length() == 0) continue;

    int comma = line.indexOf(',');
    if(comma < 0) continue;

    String cmd = line.substring(0, comma);
    int id = line.substring(comma+1).toInt();

    if(id == DEVICE_LED){
      next_cmd_led = cmd;
    }
    else if(id == DEVICE_MOTOR){
      if      (cmd=="OPEN")  motorStart();
      else if (cmd=="CLOSE") motorStop();
      else if (cmd=="LEFT")  motorNormal();
      else if (cmd=="RIGHT") motorIntermittent();
      else if (cmd=="UP")    motorSpeedUp();
      else if (cmd=="DOWN")  motorSpeedDown();
    }
    else if(id == DEVICE_AC){
      handleACCommand(cmd);
    }
  }
}


// ==========================
// IR pairing
// ==========================
unsigned long lastPair=0;

void checkIR(){
  if(!client.connected()) return;
  if(millis()-lastPair < 500) return;

  if(checkIRHit(IR_PIN_LED)){
    lastPair=millis();
    client.print("PAIR_OK,1\n");
    Serial.println("[PAIR] LED");
  }
  if(checkIRHit(IR_PIN_MOTOR)){
    lastPair=millis();
    client.print("PAIR_OK,2\n");
    Serial.println("[PAIR] MOTOR");
  }
  if(checkIRHit(IR_PIN_AC)){
    lastPair=millis();
    client.print("PAIR_OK,4\n");
    Serial.println("[PAIR] AC TFT");
  }
}


// ==========================
// Setup
// ==========================
void setup(){
  Serial.begin(115200);

  pinMode(STATUS_LED,OUTPUT);
  pinMode(IR_PIN_LED,INPUT_PULLUP);
  pinMode(IR_PIN_MOTOR,INPUT_PULLUP);
  pinMode(IR_PIN_AC,INPUT_PULLUP);

  pinMode(MOTOR_PWM_PIN,OUTPUT);
  analogWrite(MOTOR_PWM_PIN,0);

  // LED Strip init
  strip.begin();
  brightness_level = 150;
  ledSetAll(0);

  // TFT init
  spiHSPI.begin(TFT_SCLK,-1,TFT_MOSI,TFT_CS);
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);

  ac_isOn   = false;
  powerFade = 100;
  drawACIcon();
  drawACTempLabel();
  drawACModeLabel();
  drawACWindFade(0);

  connectWiFi();
}


// ==========================
// Loop
// ==========================
void loop(){
  ensureWiFi();
  handleWiFiCommands();
  checkIR();
  handleLEDAction();
  updateMotor();

  if(ac_isOn && millis() - lastACWind > 60){
    lastACWind = millis();
    drawACWind();
  }
}