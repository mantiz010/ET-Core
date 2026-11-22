// ESP32-H2: 128x64 3.3 V I2C SSD1306 display, address 0x3C.
#include <Adafruit_SSD1306.h>
#include <ETCoreThread.h>
ETCoreThread node;
Adafruit_SSD1306 screen(128, 64, &Wire, -1);
constexpr uint8_t address = 0x3C;
bool visible = true;
bool ready = false;
uint32_t lastDraw = 0;

void publish() { if (ready) node.device.sendSwitchState(visible); }
void command(const char* kind, JsonObject payload) {
  bool valid = ready && strcmp(kind, "switch.display") == 0 && payload["on"].is<bool>();
  if (valid) {
    visible = payload["on"].as<bool>();
    screen.ssd1306_command(visible ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
  }
  node.device.sendAck(payload["cmd_id"] | "", valid);
  publish();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA, SCL);
  node.device.onSync(publish);
  node.device.onCommand(command);
  node.configure("", "");  // ET-Core key, routable hub IPv6 address
}

void loop() {
  if (!node.service("h2-status-display", "switch.display", "Thread Status Display")) return;
  if (millis() - lastDraw < 2000) return;
  lastDraw = millis();
  Wire.beginTransmission(address);
  if (Wire.endTransmission() != 0) {
    ready = false;
    node.device.sendError("display", "No OLED at address 0x3C");
    return;
  }
  if (!ready) {
    ready = screen.begin(SSD1306_SWITCHCAPVCC, address, false, false);
    if (!ready) return;
    screen.setTextColor(SSD1306_WHITE);
    screen.ssd1306_command(visible ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
    publish();
  }
  if (visible) {
    screen.clearDisplay();
    screen.setCursor(0, 0);
    screen.println("ET-Core / Thread");
    screen.println(OThread.otGetStringDeviceRole());
    screen.printf("Uptime %lu s", (unsigned long)(millis() / 1000));
    screen.display();
  }
}
