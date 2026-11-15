// USB-flash this first on a spare ESP32-C3, then use ArduinoOTA over WiFi.
// Edit these three values here. Never commit your real credentials to Git.
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* OTA_PASSWORD = "CHOOSE_YOUR_OTA_PASSWORD";

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>

#if !defined(CONFIG_IDF_TARGET_ESP32C3)
#error "Select ESP32C3 Dev Module for this starter sketch."
#endif

const char* OTA_HOSTNAME = "etcore-c3-test";
constexpr uint32_t WIFI_RETRY_MS = 15000;
constexpr uint32_t STATUS_MS = 5000;
bool configured = false;
bool otaConfigured = false;
bool otaReady = false;
uint32_t lastWifiAttempt = 0;
uint32_t lastStatus = 0;

void printStatus() {
  if (!WIFI_SSID[0] || strcmp(WIFI_SSID, "YOUR_WIFI_SSID") == 0)
    Serial.println("[SETUP] WIFI_SSID is missing: edit line 3, save, then upload over USB.");
  if (!WIFI_PASSWORD[0] || strcmp(WIFI_PASSWORD, "YOUR_WIFI_PASSWORD") == 0)
    Serial.println("[SETUP] WIFI_PASSWORD is missing: edit line 4, save, then upload over USB.");
  if (!otaConfigured)
    Serial.println("[OTA DISABLED] OTA_PASSWORD is missing: edit line 5 for future OTA uploads. WiFi can still connect.");
  if (!configured) return;
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("[WAITING] SSID=%s WiFi status=%d NO IP YET\n", WIFI_SSID, (int)WiFi.status());
    return;
  }
  Serial.printf("[READY] IP=%s MAC=%s RSSI=%d OTA=%s.local:3232 (%s) uptime=%lu s\n",
                WiFi.localIP().toString().c_str(), WiFi.macAddress().c_str(),
                (int)WiFi.RSSI(), OTA_HOSTNAME, otaReady ? "password protected" : (otaConfigured ? "starting" : "DISABLED: password not set"),
                (unsigned long)(millis() / 1000));
}

void setup() {
  Serial.begin(115200);
  delay(1000);  // bounded USB enumeration grace period; never wait for a monitor
  Serial.println("\nET-Core C3 OTA starter 1.1 — stays awake; no sensor or hub connection");
  configured = WIFI_SSID[0] && WIFI_PASSWORD[0] &&
               strcmp(WIFI_SSID, "YOUR_WIFI_SSID") != 0 &&
               strcmp(WIFI_PASSWORD, "YOUR_WIFI_PASSWORD") != 0;
  otaConfigured = OTA_PASSWORD[0] && strcmp(OTA_PASSWORD, "CHOOSE_YOUR_OTA_PASSWORD") != 0;
  if (!configured) {
    printStatus();
    return;
  }

  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPort(3232);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() { Serial.println("[OTA] Update started; keep power connected"); });
  ArduinoOTA.onEnd([]() { Serial.println("\n[OTA] Update complete; rebooting"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static unsigned int lastPercent = 101;
    const unsigned int percent = total ? (unsigned int)((uint64_t)progress * 100 / total) : 0;
    if (percent != lastPercent) {
      lastPercent = percent;
      Serial.printf("[OTA] %u%%\n", percent);
    }
  });
  ArduinoOTA.onError([](ota_error_t error) {
    const char* reason = "unknown";
    switch (error) {
      case OTA_AUTH_ERROR: reason = "authentication failed"; break;
      case OTA_BEGIN_ERROR: reason = "cannot start update; check OTA partitions and image size"; break;
      case OTA_CONNECT_ERROR: reason = "connection failed"; break;
      case OTA_RECEIVE_ERROR: reason = "receive failed"; break;
      case OTA_END_ERROR: reason = "final verification failed"; break;
    }
    Serial.printf("[OTA] Error %u: %s\n", (unsigned int)error, reason);
  });

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);  // this is an always-awake OTA starter, not a battery test
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiAttempt = millis();
  printStatus();
}

void loop() {
  const uint32_t now = millis();
  if (configured) {
    if (WiFi.status() == WL_CONNECTED) {
      if (otaConfigured && !otaReady) {
        ArduinoOTA.begin();
        otaReady = true;
        printStatus();
      }
      if (otaReady) ArduinoOTA.handle();
    } else {
      if (otaReady) {
        ArduinoOTA.end();
        otaReady = false;
        Serial.println("[WiFi] Connection lost; reconnecting");
      }
      if (now - lastWifiAttempt >= WIFI_RETRY_MS) {
        lastWifiAttempt = now;
        WiFi.reconnect();
      }
    }
  }
  if (now - lastStatus >= STATUS_MS) {
    lastStatus = now;
    printStatus();
  }
  delay(2);
}
