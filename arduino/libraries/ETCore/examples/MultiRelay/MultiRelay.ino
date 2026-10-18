// ET-Core 4-relay board (class switch.multi): one Home Assistant switch per relay.
// Same messages as an ET-Bus multi-switch board: the hello lists the relays,
// state is {"switches": {"1": true, ...}}, commands carry "switch_id".
#include <WiFi.h>
#include <ETCore.h>

static const char* WIFI_SSID = "YOUR_WIFI_SSID";
static const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_PSK_HEX = "";  // 64 hex chars, same key as in Home Assistant

static const uint8_t RELAY_PINS[] = {26, 27, 32, 33};
static const char* RELAY_NAMES[] = {"Relay 1", "Relay 2", "Relay 3", "Relay 4"};
static const uint8_t RELAYS = sizeof(RELAY_PINS);

ETCore etcore;
bool relayOn[RELAYS] = {false};

static void publishState() { etcore.sendSwitchesState(relayOn, RELAYS); }

// Tell Home Assistant which relays exist (one entity each).
static void describeRelays(JsonObject hello) {
  JsonArray sw = hello["switches"].to<JsonArray>();
  for (uint8_t i = 0; i < RELAYS; i++) {
    JsonObject s = sw.add<JsonObject>();
    s["id"] = String(i + 1);
    s["name"] = RELAY_NAMES[i];
  }
}

static void onCommand(const char* dev_class, JsonObject payload) {
  int n = atoi(payload["switch_id"] | "0");
  if (n < 1 || n > RELAYS || !payload["on"].is<bool>()) return;
  relayOn[n - 1] = payload["on"].as<bool>();
  digitalWrite(RELAY_PINS[n - 1], relayOn[n - 1] ? HIGH : LOW);
  publishState();
}

void setup() {
  Serial.begin(115200);
  for (uint8_t i = 0; i < RELAYS; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], LOW);
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);

  if (strlen(ETCORE_PSK_HEX) == 64) etcore.enableEncryptionHex(ETCORE_PSK_HEX);
  etcore.onHello(describeRelays);
  etcore.begin("relay4", "switch.multi", "4 Relay Board", "relay4-1.0");
  etcore.onCommand(onCommand);
  etcore.onSync(publishState);
  publishState();
}

void loop() {
  etcore.loop();
}
