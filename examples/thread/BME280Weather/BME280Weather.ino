// ESP32-H2 Dev Module: 3.3 V I2C BME280 breakout; SDA GPIO12, SCL GPIO22.
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <ETCoreThread.h>
ETCoreThread node;
Adafruit_BME280 sensor;
bool found = false;
uint32_t last = 0;
bool due = true;
void requestReading() { due = true; }
void setup() {
  Serial.begin(115200);
  Wire.begin(SDA, SCL);
  node.device.onSync(requestReading);
  node.configure("", "");  // ET-Core key, routable IPv6 address of Home Assistant
}
void loop() {
  if (!node.service("h2-weather", "sensor.weather", "Thread Weather")) return;
  if (!due && millis() - last < 30000) return;
  due = false;
  last = millis();
  if (!found) found = sensor.begin(0x76) || sensor.begin(0x77);
  if (found) {
    float temp = sensor.readTemperature(), rh = sensor.readHumidity(), pa = sensor.readPressure();
    if (isfinite(temp) && isfinite(rh) && isfinite(pa) && pa > 0) {
      JsonDocument data;
      data["temp"] = temp;
      data["humidity"] = rh;
      data["pressure"] = pa / 100;
      node.device.sendState(data.as<JsonObject>());
      return;
    }
    found = false;
  }
  node.device.sendError("sensor", "Check BME280 at 0x76 or 0x77");
}
