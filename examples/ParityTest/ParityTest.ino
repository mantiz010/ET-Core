// ET-Core parity test for the C3: one device type per build (PT_TYPE), all with OTA.
// 1 switch.multi (3 relays)  2 fan.speed  3 fan.preset  4 sensor.air  5 sensor.temperature
// 6 light.rgb without an effects list (like RGB1), with speed, reports one error at start
#ifndef PT_TYPE
#define PT_TYPE 1
#endif
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ETCore.h>

static const char* WIFI_SSID = "";
static const char* WIFI_PASS = "";
static const char* ETCORE_PSK_HEX = "";
static const char* OTA_PASSWORD = "";

ETCore etcore;
bool on = false;
bool sw[3] = {false, false, false};
uint8_t pct = 0, r = 255, g = 255, b = 255, bri = 255, speed = 120;
char preset[16] = "off";
char effect[24] = "solid";

static void publish() {
#if PT_TYPE == 1
  etcore.sendSwitchesState(sw, 3);
#elif PT_TYPE == 2
  etcore.sendFanSpeed(on, pct);
#elif PT_TYPE == 3
  etcore.sendFanPreset(on, preset);
#elif PT_TYPE == 4
  JsonDocument d;
  JsonObject p = d.to<JsonObject>();
  p["co2"] = 612;
  p["temp"] = 21.5;
  p["humidity"] = 48.2;
  etcore.sendState(p);
#elif PT_TYPE == 5
  etcore.sendValue(19.25, "°C");
#else
  etcore.sendRgbStateFx(on, r, g, b, bri, effect, speed);
#endif
}

static void hello(JsonObject h) {
#if PT_TYPE == 1
  JsonArray s = h["switches"].to<JsonArray>();
  const char* names[] = {"Pump", "Lamp", "Heater"};
  for (int i = 0; i < 3; i++) {
    JsonObject o = s.add<JsonObject>();
    o["id"] = String(i + 1);
    o["name"] = names[i];
  }
#elif PT_TYPE == 3
  JsonArray p = h["presets"].to<JsonArray>();
  for (const char* x : {"off", "low", "medium", "high", "turbo"}) p.add(x);
#endif
}

static void onCommand(const char* cls, JsonObject p) {
#if PT_TYPE == 1
  int n = atoi(p["switch_id"] | "0");
  if (n >= 1 && n <= 3 && p["on"].is<bool>()) sw[n - 1] = p["on"];
#else
  if (p["on"].is<bool>()) on = p["on"];
  if (p["speed"].is<int>()) { if (PT_TYPE == 2) pct = p["speed"]; else speed = p["speed"]; }
  if (p["preset"].is<const char*>()) snprintf(preset, sizeof(preset), "%s", (const char*)p["preset"]);
  if (p["r"].is<int>()) r = p["r"];
  if (p["g"].is<int>()) g = p["g"];
  if (p["b"].is<int>()) b = p["b"];
  if (p["brightness"].is<int>()) bri = p["brightness"];
  if (p["effect"].is<const char*>()) snprintf(effect, sizeof(effect), "%s", (const char*)p["effect"]);
#endif
  publish();
}

void setup() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  ArduinoOTA.setHostname("hwtest1");
  if (OTA_PASSWORD[0]) ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() { etcore.sendBye(); });
  ArduinoOTA.begin();
  if (strlen(ETCORE_PSK_HEX) == 64) etcore.enableEncryptionHex(ETCORE_PSK_HEX);
  etcore.onHello(hello);
  static const char* ids[] = {"", "hwmulti1", "hwfan1", "hwfanp1", "hwair1", "hwtemp1", "hwrgb1"};
  static const char* cls[] = {"", "switch.multi", "fan.speed", "fan.preset", "sensor.air", "sensor.temperature", "light.rgb"};
  static const char* names[] = {"", "PT Multi", "PT Fan", "PT Fan Preset", "PT Air", "PT Temp", "PT RGB"};
  etcore.begin(ids[PT_TYPE], cls[PT_TYPE], names[PT_TYPE], "parity-1");
  etcore.onCommand(onCommand);
  etcore.onSync(publish);
  publish();
}

void loop() {
  ArduinoOTA.handle();
  etcore.loop();
  static uint32_t last = 0, t0 = millis();
  static bool errSent = false;
  if (PT_TYPE == 6 && !errSent && millis() - t0 > 4000) {
    etcore.sendError("strip", "LED strip not detected (test)");
    errSent = true;
  }
  if (millis() - last > 30000) { last = millis(); publish(); }
}
