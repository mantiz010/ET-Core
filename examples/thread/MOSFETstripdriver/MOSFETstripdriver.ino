// ESP32-H2: GPIO4 to an active-high, 3.3 V compatible PWM driver input.
#include <ETCoreThread.h>

ETCoreThread node;
constexpr int outputPin = 4;
constexpr int maxLevel = 255;
bool ready = false;
bool on = false;
int level = maxLevel;

void publish() {
  JsonDocument state;
  state["on"] = on;
  state["brightness"] = level;
  node.device.sendState(state.as<JsonObject>());
}

void command(const char* kind, JsonObject payload) {
  bool valid = ready && strcmp(kind, "light.dimmer") == 0 && payload["on"].is<bool>();
  if (payload.containsKey("brightness")) {
    valid = valid && payload["brightness"].is<int>();
    int requested = payload["brightness"] | -1;
    valid = valid && requested >= 0 && requested <= maxLevel;
  }
  if (valid) {
    int next = payload["brightness"] | level;
    bool enable = payload["on"].as<bool>() && next > 0;
    valid = ledcWrite(outputPin, enable ? next * 255 / maxLevel : 0);
    if (valid) {
      on = enable;
      if (next > 0) level = next;
    }
  }
  node.device.sendAck(payload["cmd_id"] | "", valid);
  if (valid) publish();
}

void setup() {
  Serial.begin(115200);
  ready = ledcAttach(outputPin, 2000, 8) && ledcWrite(outputPin, 0);
  if (!ready) Serial.println("PWM output unavailable");
  node.device.onSync(publish);
  node.device.onCommand(command);
  node.configure("", "");  // ET-Core key, routable hub IPv6 address
}

void loop() {
  node.service("h2-mosfetstripdriver", "light.dimmer", "MOSFET strip dimmer");
}
