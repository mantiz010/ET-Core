// ET-Core water tank level with an ultrasonic sensor (class sensor.tank).
// Home Assistant gets Distance (cm, sensor to water) and Level (% full).
// Works with HC-SR04 (5V: put a divider on ECHO) and the waterproof JSN-SR04T.
// Takes the median of 7 pings so a stray echo does not jump the level.
#include <WiFi.h>
#include <ETCore.h>

static const char* WIFI_SSID  = "YOUR_WIFI_SSID";
static const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_KEY = "";  // 64 hex characters, same key as in Home Assistant

#define TRIG_PIN       4
#define ECHO_PIN       5
#define EMPTY_CM       180.0f   // distance when the tank is empty
#define FULL_CM        25.0f    // distance when it is full
#define EVERY_MS       60000

ETCore etcore;

static float pingCm() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(3);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  uint32_t us = pulseIn(ECHO_PIN, HIGH, 30000);
  return us ? us / 58.0f : -1;
}

static void publish() {
  float v[7]; int n = 0;
  for (int i = 0; i < 7; i++) { float d = pingCm(); if (d > 0) v[n++] = d; delay(60); }
  if (n < 3) { etcore.sendError("sensor", "No echo: check the sensor wiring and that nothing blocks it"); return; }
  std::sort(v, v + n);
  float cm = v[n / 2];
  float pct = constrain((EMPTY_CM - cm) / (EMPTY_CM - FULL_CM) * 100.0f, 0.0f, 100.0f);
  JsonDocument d;
  d["distance"] = roundf(cm * 10) / 10;
  d["level"] = roundf(pct);
  etcore.sendState(d.as<JsonObject>());
}

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  if (!etcore.enableEncryptionHex(ETCORE_KEY)) Serial.println("WARNING: no valid 64-character key, sending unencrypted");
  etcore.begin("tank1", "sensor.tank", "Water Tank", "1.0");
  etcore.onSync(publish);
  publish();
}

void loop() {
  etcore.loop();
  static uint32_t last = 0;
  if (millis() - last > EVERY_MS) { last = millis(); publish(); }
}
