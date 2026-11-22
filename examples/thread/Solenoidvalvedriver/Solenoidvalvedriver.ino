// Active-high 3.3 V compatible driver input on GPIO4. Use a low-voltage test load.
#include <ETCoreThread.h>
ETCoreThread node;
constexpr int enablePin = 4;
bool enabled = false;
void publish() { node.device.sendSwitchState(enabled); }
void command(const char* kind, JsonObject payload) {
  bool valid = strcmp(kind, "switch.relay") == 0 && payload["on"].is<bool>();
  if (valid) {
    enabled = payload["on"].as<bool>();
    digitalWrite(enablePin, enabled);
  }
  node.device.sendAck(payload["cmd_id"] | "", valid);
  if (valid) publish();
}
void setup() {
  digitalWrite(enablePin, LOW);
  pinMode(enablePin, OUTPUT);
  Serial.begin(115200);
  node.device.onCommand(command);
  node.device.onSync(publish);
  node.configure("", "");  // ET-Core key, routable IPv6 address of Home Assistant
}
void loop() {
  node.service("h2-solenoidvalvedriver", "switch.relay", "Solenoid valve driver");
}
