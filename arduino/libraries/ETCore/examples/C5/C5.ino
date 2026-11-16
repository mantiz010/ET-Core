// USB-flash this first on a spare ESP32-C5, then use ArduinoOTA over WiFi.
// Edit these three values here. Never commit your real credentials to Git.
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* OTA_PASSWORD = "CHOOSE_YOUR_OTA_PASSWORD";

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>

// Mirror diagnostics to native USB and the UART USB adapter.
#if ARDUINO_USB_CDC_ON_BOOT
#define LOGF(...) do { Serial.printf(__VA_ARGS__); Serial0.printf(__VA_ARGS__); } while (0)
#define LOGLN(...) do { Serial.println(__VA_ARGS__); Serial0.println(__VA_ARGS__); } while (0)
#else
#define LOGF(...) Serial.printf(__VA_ARGS__)
#define LOGLN(...) Serial.println(__VA_ARGS__)
#endif

#if !defined(CONFIG_IDF_TARGET_ESP32C5)
#error "Select ESP32C5 Dev Module for this starter sketch."
#endif

const char* OTA_HOSTNAME = "etcore-c5-test";
constexpr uint32_t WIFI_RETRY_MS = 15000;
constexpr uint32_t STATUS_MS = 5000;
bool configured = false;
bool otaReady = false;
uint32_t lastWifiAttempt = 0;
uint32_t lastStatus = 0;

void printStatus() {
  if (!configured) {
    LOGLN("[SETUP] Edit WIFI_SSID, WIFI_PASSWORD and OTA_PASSWORD at the top of C5.ino, then upload over USB.");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    LOGF("[WAITING] SSID=%s WiFi status=%d NO IP YET\n", WIFI_SSID, (int)WiFi.status());
    return;
  }
  LOGF("[READY] IP=%s MAC=%s RSSI=%d channel=%d OTA=%s.local:3232 (%s) uptime=%lu s\n",
                WiFi.localIP().toString().c_str(), WiFi.macAddress().c_str(),
                (int)WiFi.RSSI(), (int)WiFi.channel(), OTA_HOSTNAME, otaReady ? "password protected" : "starting",
                (unsigned long)(millis() / 1000));
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial0.begin(115200);
#endif
  delay(1000);  // bounded USB enumeration grace period; never wait for a monitor
  LOGLN("\nET-Core C5 OTA starter 1.0 — stays awake; no sensor or hub connection");
  LOGF("[BOARD] ESP32-C5 flash=%lu bytes PSRAM=%lu bytes\n", (unsigned long)ESP.getFlashChipSize(), (unsigned long)ESP.getPsramSize());
  configured = WIFI_SSID[0] && WIFI_PASSWORD[0] && OTA_PASSWORD[0] &&
               strcmp(WIFI_SSID, "YOUR_WIFI_SSID") != 0 &&
               strcmp(WIFI_PASSWORD, "YOUR_WIFI_PASSWORD") != 0 &&
               strcmp(OTA_PASSWORD, "CHOOSE_YOUR_OTA_PASSWORD") != 0;
  if (!configured) {
    printStatus();
    return;
  }

  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPort(3232);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() { LOGLN("[OTA] Update started; keep power connected"); });
  ArduinoOTA.onEnd([]() { LOGLN("\n[OTA] Update complete; rebooting"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static unsigned int lastPercent = 101;
    const unsigned int percent = total ? (unsigned int)((uint64_t)progress * 100 / total) : 0;
    if (percent != lastPercent) {
      lastPercent = percent;
      LOGF("[OTA] %u%%\n", percent);
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
    LOGF("[OTA] Error %u: %s\n", (unsigned int)error, reason);
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
      if (!otaReady) {
        ArduinoOTA.begin();
        otaReady = true;
        printStatus();
      }
      ArduinoOTA.handle();
    } else {
      if (otaReady) {
        ArduinoOTA.end();
        otaReady = false;
        LOGLN("[WiFi] Connection lost; reconnecting");
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
