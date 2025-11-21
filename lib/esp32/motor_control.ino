#include <Arduino.h>

// === PWM CONFIG ===
const int MOTOR_PWM_PIN = 17;
const int PWM_CHANNEL = 0;
const int PWM_FREQ = 20000;     // 20 kHz
const int PWM_RESOLUTION = 10;  // duty 0–1023

// === Motor Settings ===
int motorSpeed = 600;       
int normalSpeed = 600;
bool intermittentMode = false; 
unsigned long lastToggle = 0;
bool motorOn = true;

void setup() {
  Serial.begin(115200);
  delay(1000);

  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(MOTOR_PWM_PIN, PWM_CHANNEL);

  Serial.println("\n=== Motor Serial Control Ready ===");
  Serial.println("Commands:");
  Serial.println("u = speed up");
  Serial.println("d = speed down");
  Serial.println("l = normal mode");
  Serial.println("r = intermittent mode");
  Serial.println("s = stop");
  Serial.println("o = start\n");
}

void updateMotor() {
  if (intermittentMode) {
    if (millis() - lastToggle > 500) {
      lastToggle = millis();
      motorOn = !motorOn;
    }
    ledcWrite(PWM_CHANNEL, motorOn ? motorSpeed : 0);

  } else {
    ledcWrite(PWM_CHANNEL, motorSpeed);
  }
}

// === Actions ===
void speedUp() {
  motorSpeed = min(1023, motorSpeed + 100);
  Serial.printf("Speed Up → %d\n", motorSpeed);
}

void speedDown() {
  motorSpeed = max(0, motorSpeed - 100);
  Serial.printf("Speed Down → %d\n", motorSpeed);
}

void normalMode() {
  intermittentMode = false;
  motorSpeed = normalSpeed;
  Serial.println("Normal Mode");
}

void intermittent() {
  intermittentMode = true;
  Serial.println("Intermittent Mode");
}

void stopMotor() {
  intermittentMode = false;
  motorSpeed = 0;
  ledcWrite(PWM_CHANNEL, 0);
  Serial.println("Motor STOP");
}

void startMotor() {
  intermittentMode = false;
  motorSpeed = normalSpeed;
  Serial.println("Motor START");
}

void loop() {
  // === Read Serial Input ===
  if (Serial.available()) {
    char c = Serial.read();

    switch (c) {
      case 'u': speedUp(); break;
      case 'd': speedDown(); break;
      case 'l': normalMode(); break;
      case 'r': intermittent(); break;
      case 's': stopMotor(); break;
      case 'o': startMotor(); break;
      default: Serial.println("Unknown command"); break;
    }
  }

  // Update motor PWM output
  updateMotor();
}
