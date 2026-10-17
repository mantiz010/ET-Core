// ET-Core hardware test: a switch device that reports health in its state.
// Set WiFi and the test key at the top, flash, and run sim/hw_test.py on the PC.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ETCore.h>

static const char* WIFI_SSID = "YOUR_WIFI_SSID";
static const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_PSK_HEX = "";  // 64 hex chars, same as the hub
static const char* OTA_PASSWORD = "";    // after the first USB flash, update over WiFi

ETCore etcore;
bool on = false;
uint32_t commands = 0;

static void publishState() {
  JsonDocument d;
  JsonObject p = d.to<JsonObject>();
  p["on"] = on;
  p["commands"] = commands;
  p["heap"] = ESP.getFreeHeap();
  p["min_heap"] = ESP.getMinFreeHeap();
  etcore.sendState(p);
}

static void onCommand(const char* dev_class, JsonObject payload) {
  if (strcmp(dev_class, "switch.relay") != 0) return;
  if (payload["on"].is<bool>()) on = payload["on"].as<bool>();
  commands++;
  Serial.printf("[HWTest] cmd %s on=%d (total %lu)\n", (const char*)(payload["cmd_id"] | ""), on,
                (unsigned long)commands);
  publishState();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[HWTest] ET-Core hardware test " ETCORE_LIBRARY_VERSION);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.printf("\n[HWTest] WiFi %s RSSI %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  if (strlen(ETCORE_PSK_HEX) == 64) etcore.enableEncryptionHex(ETCORE_PSK_HEX);
  // Over-the-air updates (port 3232): arduino-cli upload --port <ip> --protocol network
  ArduinoOTA.setHostname("hwtest1");
  if (OTA_PASSWORD[0]) ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() { Serial.println("[HWTest] OTA update starting"); });
  ArduinoOTA.onEnd([]() { Serial.println("[HWTest] OTA update done, rebooting"); });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[HWTest] OTA error %u\n", e); });
  ArduinoOTA.begin();

  etcore.begin("hwtest1", "switch.relay", "ET-Core HW Test", "hwtest-1");
  etcore.onCommand(onCommand);
  publishState();
}

void loop() {
  ArduinoOTA.handle();
  etcore.loop();
  static uint32_t last = 0;
  if (millis() - last > 60000) {  // slow refresh with heap numbers
    last = millis();
    publishState();
  }
}
