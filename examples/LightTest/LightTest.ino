// ET-Core light test: an RGB light (class light.rgb) with effects, for testing the
// Home Assistant light entity. Drives the board LED (on/off + brightness) so the
// result is visible; colour is only reported back. Keeps OTA like HWTest.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ETCore.h>

static const char* WIFI_SSID = "YOUR_WIFI_SSID";
static const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_PSK_HEX = "";  // 64 hex chars, same as the hub
static const char* OTA_PASSWORD = "";    // after the first USB flash, update over WiFi

static const int LED_PIN = 8;            // ESP32-C3 super mini: blue LED, active low
static const bool LED_ACTIVE_LOW = true;

ETCore etcore;
bool on = false;
uint8_t r = 255, g = 255, b = 255, brightness = 255;
char effect[16] = "none";
uint32_t commands = 0;

static void addEffects(JsonObject hello) {
  JsonArray fx = hello["effects"].to<JsonArray>();
  fx.add("none");
  fx.add("blink");
  fx.add("breathe");
}

static void publishState() {
  JsonDocument d;
  JsonObject p = d.to<JsonObject>();
  p["on"] = on;
  p["r"] = r;
  p["g"] = g;
  p["b"] = b;
  p["brightness"] = brightness;
  p["effect"] = effect;
  p["commands"] = commands;
  etcore.sendState(p);
}

static void led(uint8_t level) {
  analogWrite(LED_PIN, LED_ACTIVE_LOW ? 255 - level : level);
}

static void onCommand(const char* dev_class, JsonObject payload) {
  if (strcmp(dev_class, "light.rgb") != 0) return;
  if (payload["on"].is<bool>()) on = payload["on"].as<bool>();
  if (payload["brightness"].is<int>()) brightness = (uint8_t)constrain(payload["brightness"].as<int>(), 0, 255);
  if (payload["r"].is<int>()) r = (uint8_t)constrain(payload["r"].as<int>(), 0, 255);
  if (payload["g"].is<int>()) g = (uint8_t)constrain(payload["g"].as<int>(), 0, 255);
  if (payload["b"].is<int>()) b = (uint8_t)constrain(payload["b"].as<int>(), 0, 255);
  if (payload["effect"].is<const char*>()) {
    const char* e = payload["effect"];
    if (strcmp(e, "none") && strcmp(e, "blink") && strcmp(e, "breathe")) {
      etcore.sendAck(payload["cmd_id"] | "", false);  // unknown effect: refuse, keep state
      return;
    }
    snprintf(effect, sizeof(effect), "%s", e);
  }
  commands++;
  Serial.printf("[LightTest] on=%d rgb=%u,%u,%u bri=%u fx=%s\n", on, r, g, b, brightness, effect);
  publishState();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(LED_PIN, OUTPUT);
  led(0);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  if (strlen(ETCORE_PSK_HEX) == 64) etcore.enableEncryptionHex(ETCORE_PSK_HEX);
  ArduinoOTA.setHostname("hwtest1");
  if (OTA_PASSWORD[0]) ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() { etcore.sendBye(); });
  ArduinoOTA.begin();

  etcore.onHello(addEffects);
  etcore.begin("hwlight1", "light.rgb", "ET-Core Light Test", "lighttest-1");
  etcore.onCommand(onCommand);
  etcore.onSync(publishState);
  publishState();
}

void loop() {
  ArduinoOTA.handle();
  etcore.loop();
  uint32_t t = millis();
  if (!on) led(0);
  else if (!strcmp(effect, "blink")) led((t / 500) % 2 ? brightness : 0);
  else if (!strcmp(effect, "breathe")) led((uint8_t)((uint32_t)brightness * (t % 2000 < 1000 ? t % 1000 : 999 - t % 1000) / 999));
  else led(brightness);
}
