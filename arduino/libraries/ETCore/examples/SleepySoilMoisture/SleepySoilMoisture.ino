// ET-Core battery soil moisture sensor (class sensor.moisture). The board wakes
// every SLEEP_SECONDS, reads the probe, reports and goes back to deep sleep.
// Home Assistant keeps it online between reports and marks it offline only after
// about three missed ones.
//
// Hardware awake time and battery life have not been measured. WiFi association
// and DHCP time depend on the board and network. Simulator timing is not a
// battery-life estimate. This example defaults to classic ESP32 GPIO34; change
// PROBE_PIN to a verified ADC pin before using another chip or board.
// Wiring: capacitive probe output to PROBE_PIN (an ADC pin that works with WiFi
// on: GPIO 32-39 on a classic ESP32, GPIO 0-4 on an ESP32-C3).
#include <WiFi.h>
#include <ETCore.h>

static const char* WIFI_SSID  = "YOUR_WIFI_SSID";
static const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_KEY = "";  // 64 hex characters, same key as in Home Assistant

#define PROBE_PIN       34
#define DRY_MV          2600     // probe reading in dry air
#define WET_MV          1100     // probe reading in water
#define SLEEP_SECONDS   900      // report every 15 minutes
#define WIFI_LIMIT_MS   8000     // no WiFi by then: sleep and try next time

ETCore etcore;

void setup() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_LIMIT_MS) delay(20);
  if (WiFi.status() != WL_CONNECTED) {      // never drain the battery waiting
    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_SECONDS * 1000000ULL);
    esp_deep_sleep_start();
  }

  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PROBE_PIN);
  mv /= 8;
  const float pct = constrain((float)(DRY_MV - (int)mv) * 100.0f / (DRY_MV - WET_MV), 0.0f, 100.0f);

  etcore.enableEncryptionHex(ETCORE_KEY);
  etcore.setSleepInterval(SLEEP_SECONDS);   // before begin()
  etcore.begin("soil1", "sensor.moisture", "Soil Moisture", "1.0");
  etcore.sendValue(pct, "%");
  etcore.sleepFor();                        // waits for the hub, then deep-sleeps
}

void loop() {}  // never reached: every wake starts again in setup()
