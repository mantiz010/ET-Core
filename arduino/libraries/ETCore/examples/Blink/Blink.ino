// ET-Core Blink: the smallest ET-Core device. The board's own LED becomes a
// switch in Home Assistant. Start here to check WiFi, the key and the hub.
#include <WiFi.h>
#include <ETCore.h>

// WiFi and the ET-Core network key (same key as in Home Assistant)
static const char* WIFI_SSID  = "YOUR_WIFI_SSID";
static const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_KEY = "";  // 64 hex characters

#ifndef LED_BUILTIN
#define LED_BUILTIN 2  // most ESP32 dev boards; ESP32-C3 SuperMini uses 8
#endif

ETCore etcore;
bool ledOn = false;

void onCommand(const char* cls, JsonObject p) {
  if (p["on"].is<bool>()) ledOn = p["on"];
  digitalWrite(LED_BUILTIN, ledOn ? HIGH : LOW);
  etcore.sendSwitchState(ledOn);  // tell Home Assistant; the library sends the ack
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  Serial.printf("WiFi OK, IP %s\n", WiFi.localIP().toString().c_str());

  etcore.enableEncryptionHex(ETCORE_KEY);
  etcore.begin("blink1", "switch.led", "Blink LED", "1.0");
  etcore.onCommand(onCommand);
  etcore.onSync([]() { etcore.sendSwitchState(ledOn); });  // after a hub restart
  etcore.sendSwitchState(ledOn);
}

void loop() {
  etcore.loop();
}
