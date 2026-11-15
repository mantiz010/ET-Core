// Dedicated C3 test board only. Test readings are counters, not sensor data.
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* TEST_KEY = "YOUR_64_HEX_TEST_KEY"; // separate from the production key

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <ETCore.h>
#include <esp_sleep.h>
#include <esp_system.h>

#if !defined(CONFIG_IDF_TARGET_ESP32C3)
#error "This hardware test targets ESP32-C3 only."
#endif

// Mirror diagnostics to native USB and UART0 for C3 boards with either USB path.
class TestLog : public Print {
public:
  size_t write(uint8_t value) override {
    Serial.write(value);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial0.write(value);
#endif
    return 1;
  }
};
TestLog testLog;

constexpr uint16_t TEST_PORT = 15561; // isolated hub; never production port 5560
constexpr uint32_t SLEEP_SECONDS = 5;
constexpr uint32_t AWAKE_MS = 8000;   // deliberate OTA/debug window, not low-power tuning
constexpr uint32_t MAX_WAKES = 10;
RTC_DATA_ATTR uint32_t wakeCount = 0;
RTC_DATA_ATTR bool runTest = false;
ETCore core;
WebServer web(80);
bool servicesReady = false, otaReady = false, updating = false, coreReady = false;
uint32_t awakeStarted = 0, lastWifiAttempt = 0, lastStatus = 0;

void publishTest() {
  if (!coreReady) return;
  JsonDocument d;
  d["test_only"] = true;
  d["value"] = wakeCount;
  d["wake_count"] = wakeCount;
  d["resumed"] = core.wokeFromSleep();
  d["running"] = runTest;
  d["awake_ms"] = millis();
  core.sendState(d.as<JsonObject>());
}

void statusReply() {
  JsonDocument d;
  d["firmware"] = "c3-sleep-test-1.0";
  d["mac"] = WiFi.macAddress();
  d["ip"] = WiFi.localIP().toString();
  d["wake_count"] = wakeCount;
  d["running"] = runTest;
  d["resumed"] = coreReady && core.wokeFromSleep();
  d["core_ready"] = coreReady;
  d["ota"] = otaReady;
  d["uptime_ms"] = millis();
  d["reset_reason"] = (int)esp_reset_reason();
  d["flash_bytes"] = ESP.getFlashChipSize();
  d["free_heap"] = ESP.getFreeHeap();
  d["test_port"] = TEST_PORT;
  String body;
  serializeJson(d, body);
  web.send(200, "application/json", body);
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial0.begin(115200);
#endif
  delay(300);
  if (esp_reset_reason() != ESP_RST_DEEPSLEEP) {
    wakeCount = 0;
    runTest = false; // cold boot / reset / OTA always leaves a recovery window
  } else if (runTest) {
    ++wakeCount;
    if (wakeCount >= MAX_WAKES) runTest = false;
  }
  testLog.printf("\nC3 isolated sleep test: wake=%lu running=%d\n", (unsigned long)wakeCount, runTest);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiAttempt = millis();
  ArduinoOTA.setHostname("etcore-c3-test");
  ArduinoOTA.onStart([]() { updating = true; runTest = false; testLog.println("[OTA] Updating; sleep inhibited"); });
  ArduinoOTA.onError([](ota_error_t e) { updating = false; runTest = false; testLog.printf("[OTA] Error %u; staying awake\n", e); });
  ArduinoOTA.onEnd([]() { testLog.println("[OTA] Complete"); });
  web.on("/", HTTP_GET, statusReply);
  web.on("/status", HTTP_GET, statusReply);
  web.on("/hold", HTTP_POST, []() {
    runTest = false;
    publishTest();
    statusReply();
  });
  web.on("/run", HTTP_POST, []() {
    if (!coreReady || updating) { web.send(409, "text/plain", "Test not ready"); return; }
    wakeCount = 0;
    runTest = true;
    awakeStarted = millis();
    publishTest();
    statusReply();
  });
}

void loop() {
  const uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    if (!otaReady) { ArduinoOTA.begin(); otaReady = true; }
    if (!servicesReady) {
      web.begin();
      servicesReady = true;
      core.setPort(TEST_PORT);
      core.setSleepInterval(SLEEP_SECONDS);
      core.onSync(publishTest);
      if (core.enableEncryptionHex(TEST_KEY)) {
        core.begin("c3-battery-test", "sensor.test", "C3 Battery TEST ONLY", "c3-sleep-test-1.0");
        coreReady = true;
        publishTest();
      } else {
        runTest = false;
        testLog.println("[TEST] Invalid test key; staying awake with OTA available");
      }
      awakeStarted = millis();
      testLog.printf("[READY] IP=%s OTA=etcore-c3-test.local:3232 (NO PASSWORD) test_port=%u\n", WiFi.localIP().toString().c_str(), TEST_PORT);
    }
    ArduinoOTA.handle();
    web.handleClient();
    if (coreReady) core.loop();
    if (coreReady && runTest && !updating && millis() - awakeStarted >= AWAKE_MS) {
      publishTest();
      testLog.printf("[SLEEP] wake=%lu next sleep=%lu seconds\n", (unsigned long)wakeCount, (unsigned long)SLEEP_SECONDS);
      core.sleepFor(SLEEP_SECONDS, 1500);
    }
  } else {
    if (otaReady) { ArduinoOTA.end(); otaReady = false; }
    if (now - lastWifiAttempt >= 15000) {
      lastWifiAttempt = now;
      WiFi.reconnect();
    }
  }
  if (now - lastStatus >= 5000) {
    lastStatus = now;
    testLog.printf("[STATUS] IP=%s WiFi=%d wake=%lu resumed=%d running=%d OTA=%d\n", WiFi.localIP().toString().c_str(), (int)WiFi.status(), (unsigned long)wakeCount, core.wokeFromSleep(), runTest, otaReady);
  }
  delay(2);
}
