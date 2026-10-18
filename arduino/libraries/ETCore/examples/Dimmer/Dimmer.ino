// ET-Core dimmer (class light.dimmer): one PWM channel, brightness only.
// Home Assistant shows a light with a brightness slider and no colour picker.
// Drives a MOSFET for a 12/24 V LED strip, a single LED, or a PWM LED driver.
// Changes fade smoothly; the state is saved across power cuts.
#include <WiFi.h>
#include <Preferences.h>
#include <ETCore.h>

static const char* WIFI_SSID  = "YOUR_WIFI_SSID";
static const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_KEY = "";  // 64 hex characters, same key as in Home Assistant

#define PWM_PIN    5
#define PWM_FREQ   5000   // Hz; above hearing range for most drivers
#define PWM_BITS   12     // 0..4095
#define FADE_MS    400

ETCore etcore;
Preferences prefs;
bool on = false;
uint8_t brightness = 200;        // 1..255 as Home Assistant sends it
float level = 0, from = 0, to = 0;
uint32_t fadeStart = 0;

// Eyes see brightness logarithmically: square it so the slider feels even.
static uint32_t duty(float b) { return (uint32_t)(b * b / (255.0f * 255.0f) * ((1 << PWM_BITS) - 1)); }

static void startFade() {
  from = level;
  to = on ? brightness : 0;
  fadeStart = millis();
}
static void serviceFade() {
  float t = min(1.0f, (millis() - fadeStart) / (float)FADE_MS);
  float next = from + (to - from) * t;
  if (next != level) { level = next; ledcWrite(PWM_PIN, duty(level)); }
}

static void report() {
  JsonDocument d;
  JsonObject p = d.to<JsonObject>();
  p["on"] = on;
  p["brightness"] = brightness;
  etcore.sendState(p);
}

void onCommand(const char* cls, JsonObject p) {
  if (p["on"].is<bool>()) on = p["on"];
  if (p["brightness"].is<int>()) brightness = constrain((int)p["brightness"], 1, 255);
  startFade();
  report();
  prefs.putBool("on", on);
  prefs.putUChar("bri", brightness);
}

void setup() {
  Serial.begin(115200);
  ledcAttach(PWM_PIN, PWM_FREQ, PWM_BITS);
  prefs.begin("dimmer", false);
  on = prefs.getBool("on", false);
  brightness = prefs.getUChar("bri", 200);
  startFade();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { serviceFade(); delay(10); }

  etcore.enableEncryptionHex(ETCORE_KEY);
  etcore.begin("dimmer1", "light.dimmer", "Dimmer", "1.0");
  etcore.onCommand(onCommand);
  etcore.onSync(report);
  report();
}

void loop() {
  etcore.loop();
  serviceFade();
}
