// ET-Core PWM RGB light (class light.rgb), the ET-Bus RGBLight example ported.
// No effects list in the hello: Home Assistant offers the same 35 effects as ET-Bus.
#include <WiFi.h>
#include <ETCore.h>

static const char* WIFI_SSID = "YOUR_WIFI_SSID";
static const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_PSK_HEX = "";  // 64 hex chars, same key as in Home Assistant

static const int PIN_R = 25;
static const int PIN_G = 26;
static const int PIN_B = 27;

ETCore etcore;

static bool isOn = true;
static uint8_t r = 255, g = 255, b = 255;
static uint8_t bri = 255;
static uint8_t speed = 120;
static char effect[24] = "solid";

static void applyRgb() {
  analogWrite(PIN_R, isOn ? (uint16_t)r * bri / 255 : 0);
  analogWrite(PIN_G, isOn ? (uint16_t)g * bri / 255 : 0);
  analogWrite(PIN_B, isOn ? (uint16_t)b * bri / 255 : 0);
}

static void publishState() { etcore.sendRgbStateFx(isOn, r, g, b, bri, effect, speed); }

static void onCommand(const char* dev_class, JsonObject payload) {
  if (payload["on"].is<bool>()) isOn = payload["on"];
  if (payload["r"].is<int>()) r = payload["r"];
  if (payload["g"].is<int>()) g = payload["g"];
  if (payload["b"].is<int>()) b = payload["b"];
  if (payload["brightness"].is<int>()) bri = payload["brightness"];
  if (payload["speed"].is<int>()) speed = payload["speed"];
  if (payload["effect"].is<const char*>()) snprintf(effect, sizeof(effect), "%s", (const char*)payload["effect"]);
  applyRgb();
  publishState();
}

void setup() {
  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);

  if (strlen(ETCORE_PSK_HEX) == 64) etcore.enableEncryptionHex(ETCORE_PSK_HEX);
  etcore.begin("rgb1", "light.rgb", "RGB Light 1", "rgb-1.0");
  etcore.onCommand(onCommand);
  etcore.onSync(publishState);
  applyRgb();
  publishState();
}

void loop() {
  etcore.loop();
}
