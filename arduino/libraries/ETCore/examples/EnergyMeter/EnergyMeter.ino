// ET-Core energy meter pulse counter (class sensor.energy): reads the flashing
// LED on your electricity meter (photodiode / LDR module) or an S0 output.
// Home Assistant gets Power (W, now) and Energy (kWh, total) and can use it in
// the Energy dashboard. The total is saved, so a power cut does not reset it.
#include <WiFi.h>
#include <Preferences.h>
#include <ETCore.h>

static const char* WIFI_SSID  = "YOUR_WIFI_SSID";
static const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_KEY = "";  // 64 hex characters, same key as in Home Assistant

#define PULSE_PIN        4
#define PULSES_PER_KWH   1000   // printed on the meter: "1000 imp/kWh"
#define EVERY_MS         10000

ETCore etcore;
Preferences prefs;
volatile uint32_t pulses = 0, lastPulseUs = 0, gapUs = 0;
uint64_t totalPulses = 0;

void IRAM_ATTR onPulse() {
  uint32_t now = micros();
  if (now - lastPulseUs < 50000) return;  // ignore bounce (max 72 kW at 1000 imp/kWh)
  gapUs = now - lastPulseUs;
  lastPulseUs = now;
  pulses++;
}

static void publish() {
  noInterrupts();
  uint32_t n = pulses; pulses = 0;
  uint32_t gap = gapUs, last = lastPulseUs;
  interrupts();
  totalPulses += n;
  // Power from the time between the last two pulses; 0 when they stop coming.
  float watts = (gap && micros() - last < 3600000000UL / PULSES_PER_KWH * 2)
                  ? 3600.0f * 1000.0f * 1000000.0f / PULSES_PER_KWH / gap : 0;
  JsonDocument d;
  d["power"] = roundf(watts);
  d["energy"] = roundf((double)totalPulses / PULSES_PER_KWH * 1000) / 1000;
  etcore.sendState(d.as<JsonObject>());
  static uint64_t saved = 0;
  if (totalPulses - saved >= PULSES_PER_KWH / 10) { prefs.putULong64("total", totalPulses); saved = totalPulses; }
}

void setup() {
  Serial.begin(115200);
  prefs.begin("energy", false);
  totalPulses = prefs.getULong64("total", 0);
  pinMode(PULSE_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PULSE_PIN), onPulse, FALLING);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  if (!etcore.enableEncryptionHex(ETCORE_KEY)) Serial.println("WARNING: no valid 64-character key, sending unencrypted");
  etcore.begin("energy1", "sensor.energy", "House Energy", "1.0");
  etcore.onSync(publish);
}

void loop() {
  etcore.loop();
  static uint32_t last = 0;
  if (millis() - last > EVERY_MS) { last = millis(); publish(); }
}
