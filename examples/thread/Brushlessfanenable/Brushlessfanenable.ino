// ESP32-H2: GPIO4 to an active-high, 3.3 V compatible PWM driver input.
#include <ETCoreThread.h>

ETCoreThread node;
constexpr int outputPin = 4;
constexpr int maxLevel = 100;
bool ready = false;
bool on = false;
int level = maxLevel;

void publish() {
  node.device.sendFanSpeed(on, on ? level : 0);
}

void command(const char* kind, JsonObject payload) {
  bool valid = ready && strcmp(kind, "fan.speed") == 0 && payload["on"].is<bool>();
  if (payload.containsKey("speed")) {
    valid = valid && payload["speed"].is<int>();
    int requested = payload["speed"] | -1;
    valid = valid && requested >= 0 && requested <= maxLevel;
  }
  if (valid) {
    int next = payload["speed"] | level;
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
  ready = ledcAttach(outputPin, 25000, 8) && ledcWrite(outputPin, 0);
  if (!ready) Serial.println("PWM output unavailable");
  node.device.onSync(publish);
  node.device.onCommand(command);
  node.configure("", "");  // ET-Core key, routable hub IPv6 address
}

void loop() {
  node.service("h2-brushlessfanenable", "fan.speed", "Brushless fan speed");
}
