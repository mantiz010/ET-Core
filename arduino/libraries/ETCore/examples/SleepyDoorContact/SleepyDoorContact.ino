// ET-Core battery door / window contact (class binary_sensor.door). The board
// sleeps until the reed switch changes, reports "Open" / "Closed" and sleeps
// again. It also wakes every SLEEP_SECONDS so Home Assistant knows it is alive.
//
// Wiring: reed switch between DOOR_PIN and GND (closed = magnet near = door
// shut), plus a 1 M resistor from DOOR_PIN to 3.3 V. DOOR_PIN must be able to
// wake the chip: an RTC pin on a classic ESP32 / S3 (for example GPIO 4 or 33),
// GPIO 0-5 on an ESP32-C3.
#include <WiFi.h>
#include <ETCore.h>

static const char* WIFI_SSID  = "YOUR_WIFI_SSID";
static const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_KEY = "";  // 64 hex characters, same key as in Home Assistant

#define DOOR_PIN        4
#define SLEEP_SECONDS   3600     // "still alive" report every hour
#define WIFI_LIMIT_MS   8000     // no WiFi by then: sleep and try next time

ETCore etcore;

// Wake when the contact leaves the state it is in now.
static void wakeOnChange(bool open) {
#if SOC_PM_SUPPORT_EXT0_WAKEUP
  esp_sleep_enable_ext0_wakeup((gpio_num_t)DOOR_PIN, open ? 0 : 1);
#else
  esp_deep_sleep_enable_gpio_wakeup(1ULL << DOOR_PIN, open ? ESP_GPIO_WAKEUP_GPIO_LOW : ESP_GPIO_WAKEUP_GPIO_HIGH);
#endif
}

void setup() {
  pinMode(DOOR_PIN, INPUT);
  delay(20);                                 // let the contact settle
  const bool open = digitalRead(DOOR_PIN) == HIGH;
  wakeOnChange(open);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_LIMIT_MS) delay(20);
  if (WiFi.status() != WL_CONNECTED) {       // never drain the battery waiting
    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_SECONDS * 1000000ULL);
    esp_deep_sleep_start();
  }

  etcore.enableEncryptionHex(ETCORE_KEY);
  etcore.setSleepInterval(SLEEP_SECONDS);    // before begin()
  etcore.begin("door1", "binary_sensor.door", "Front Door", "1.0");
  JsonDocument d;
  d["on"] = open;                            // on = open
  etcore.sendState(d.as<JsonObject>());
  etcore.sleepFor();                         // waits for the hub, then deep-sleeps
}

void loop() {}  // never reached: every wake starts again in setup()
