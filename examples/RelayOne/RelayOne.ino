#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ETCore.h>

static const char* WIFI_SSID = "YOUR_WIFI_SSID";
static const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

#define RELAY_PIN 4

ETCore etcore;
bool relayOn = false;

static void publishState() {
  JsonDocument d;
  JsonObject p = d.to<JsonObject>();
  p["on"] = relayOn;
  etcore.sendState(p);
}

static void onCommand(const char* dev_class, JsonObject payload) {
  if (strcmp(dev_class, "switch.relay") != 0) return;
  if (payload.containsKey("on")) {
    relayOn = bool(payload["on"]);
    digitalWrite(RELAY_PIN, relayOn ? HIGH : LOW);
    etcore.sendAck(payload["cmd_id"] | "", true);
    publishState();
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);

  etcore.begin("relay1", "switch.relay", "ET-Core Relay 1", "relay-0.1.0");
  etcore.onCommand(onCommand);
  publishState();
}

void loop() {
  etcore.loop();
}
