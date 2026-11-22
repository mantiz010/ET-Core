// Bresser CC1101 receiver recovery candidate 2026-11-07.1-etcore.
// Standard ESP32. Existing wiring and RF settings are preserved.
// Private settings are injected at build time; no credentials belong in this file.
#include <Arduino.h>
#include <WiFi.h>
#include <ETCore.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <RadioLib.h>
#include <atomic>
#include <lwip/sockets.h>
#include "RecoveryPolicy.h"
#include "BresserConfig.h"

#if !defined(BRESSER_WIFI_SSID) || !defined(BRESSER_WIFI_PASSWORD) || !defined(BRESSER_ETCORE_KEY) || !defined(BRESSER_ETCORE_PORT)
#error "Provide the private Bresser build settings before compiling."
#endif

constexpr char VERSION[] = "2026-11-07.1-etcore";
constexpr char OTA_NAME[] = "bresser-6in1";
constexpr uint8_t CS_PIN = 5, GDO0_PIN = 22, GDO2_PIN = 4;
constexpr size_t VALUE_COUNT = 8;
enum Reading { TEMPERATURE, HUMIDITY, GUST, WIND, DIRECTION, RAIN, BATTERY, SENSOR_ID };

CC1101 radio = new Module(CS_PIN, GDO0_PIN, RADIOLIB_NC, GDO2_PIN);
WiFiServer telnetServer(23);
WiFiClient telnetClient;
portMUX_TYPE valuesMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE radioMux = portMUX_INITIALIZER_UNLOCKED;
Recovery::Value values[VALUE_COUNT];
ETCore etcore;
bool etcoreConfigured = false, etcoreStarted = false;
bool stateDirty = false;
uint32_t lastStateSent = 0;
const char* const metrics[VALUE_COUNT] = {
  "temperature", "humidity", "wind_gust", "wind_speed",
  "wind_direction", "rainfall", "battery_ok", "sensor_id"
};
std::atomic<bool> networkReady{false}, otaBusy{false};
std::atomic<unsigned> disconnectReason{0};
volatile bool packetReady = false;
bool radioReady = false, radioReceiving = false;
bool hadWiFi = false, wifiStartPending = false, servicesStarted = false;
uint32_t wifiStoppedAt = 0, lastWiFiAttempt = 0, lastRadioAttempt = 0;
int8_t lastBattery = -1;
uint8_t batteryStableCount = 0;
QueueHandle_t messages = nullptr;
struct LogMessage { char text[128]; };

// The Arduino loop owns Serial/Telnet output.
void log(const String& text) {
  if (!messages) return;
  LogMessage entry = {};
  text.toCharArray(entry.text, sizeof(entry.text));
  xQueueSend(messages, &entry, 0);
}

void serviceLog() {
  LogMessage entry;
  for (unsigned n = 0; n < 4 && messages && xQueueReceive(messages, &entry, 0) == pdTRUE; ++n) {
    Serial.println(entry.text);
    if (telnetClient && telnetClient.connected()) {
      char line[132];
      const int length = snprintf(line, sizeof(line), "%s\r\n", entry.text);
      // A slow Telnet reader must not stall radio reception or OTA. Serial
      // remains the full local log; close a congested remote log connection.
      const int sent = send(telnetClient.fd(), line, length, MSG_DONTWAIT);
      if (sent != length) telnetClient.stop();
    }
  }
}

void storeReading(Reading index, const String& text) {
  portENTER_CRITICAL(&valuesMux);
  values[index].set(text.c_str(), millis());
  stateDirty = true;
  portEXIT_CRITICAL(&valuesMux);
}

// One loop owns ET-Core and the reading cache. No TCP client or MQTT task.
// Re-sync only sends fresh RF samples; missing fields are never fabricated.
void publishState() {
  if (!etcoreStarted || !networkReady.load() || otaBusy.load()) return;
  JsonDocument doc;
  JsonObject payload = doc.to<JsonObject>();
  const uint32_t now = millis();
  for (size_t i = 0; i < VALUE_COUNT; ++i) {
    const Recovery::Value& v = values[i];
    if (!v.revision || Recovery::elapsed(now, v.at, Recovery::FRESH_MS)) continue;
    if (i == SENSOR_ID) payload[metrics[i]] = v.text;
    else if (i == BATTERY) payload[metrics[i]] = strcmp(v.text, "1") == 0;
    else {
      char* end = nullptr;
      const float value = strtof(v.text, &end);
      if (end != v.text && *end == '\0' && isfinite(value)) payload[metrics[i]] = value;
    }
  }
  if (payload.size()) etcore.sendState(payload);
  lastStateSent = now;
  stateDirty = false;
}

void serviceETCore() {
  if (!etcoreStarted || !networkReady.load() || otaBusy.load()) return;
  etcore.loop();
  if ((stateDirty && Recovery::elapsed(millis(), lastStateSent, 250)) ||
      Recovery::elapsed(millis(), lastStateSent, 30000)) publishState();
}

void serviceWiFi() {
  const bool online = WiFi.status() == WL_CONNECTED && uint32_t(WiFi.localIP()) != 0;
  networkReady.store(online);
  if (online) {
    if (!hadWiFi) {
      hadWiFi = true;
      wifiStartPending = false;
      log("[WiFi] Connected: " + WiFi.localIP().toString() + " RSSI " + String(WiFi.RSSI()));
      if (etcoreConfigured) {
        etcore.begin("bresser-6in1", "sensor.weather", "Bresser Weather", VERSION);
        etcoreStarted = true;
        stateDirty = true;
      }
      ArduinoOTA.begin();
      telnetServer.begin();
      servicesStarted = true;
      log("[OTA] bresser-6in1 ready");
    }
    ArduinoOTA.handle();
    if (!telnetClient || !telnetClient.connected()) telnetClient = telnetServer.available();
    return;
  }
  if (hadWiFi) {
    log("[WiFi] Link lost, reason=" + String(disconnectReason.load()));
    hadWiFi = false;
    telnetClient.stop();
    if (servicesStarted) { ArduinoOTA.end(); telnetServer.end(); servicesStarted = false; }
    lastWiFiAttempt = millis() - Recovery::WIFI_RETRY_MS;
  }
  // Stop/start are separate passes. The core's automatic retries are disabled
  // so they cannot race this recovery state machine.
  if (wifiStartPending) {
    if (!Recovery::elapsed(millis(), wifiStoppedAt, 250)) return;
    wifiStartPending = false;
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(OTA_NAME);
    WiFi.setAutoReconnect(false);
    WiFi.setSleep(false);
    WiFi.begin(BRESSER_WIFI_SSID, BRESSER_WIFI_PASSWORD);
    lastWiFiAttempt = millis();
    return;
  }
  if (!Recovery::elapsed(millis(), lastWiFiAttempt, Recovery::WIFI_RETRY_MS)) return;
  log("[WiFi] Starting a timed connection attempt");
  WiFi.mode(WIFI_OFF);
  wifiStoppedAt = millis();
  wifiStartPending = true;
}

uint16_t lfsrDigest16(const uint8_t* data, size_t length, uint16_t gen, uint16_t key) {
  uint16_t sum = 0;
  for (size_t k = 0; k < length; ++k) {
    for (int i = 7; i >= 0; --i) {
      if ((data[k] >> i) & 1) sum ^= key;
      key = (key >> 1) ^ (key & 1 ? gen : 0);
    }
  }
  return sum;
}

bool decodeBresser6In1Full(const uint8_t* msg, size_t len) {
  if (len < 18) return false;
  if (((uint16_t(msg[0]) << 8) | msg[1]) != lfsrDigest16(&msg[2], 15, 0x8810, 0x5412)) return false;
  unsigned checksum = 0;
  for (size_t i = 2; i < 18; ++i) checksum += msg[i];
  if ((checksum & 0xff) != 0xff) return false;
  const uint32_t id = (uint32_t(msg[2]) << 24) | (uint32_t(msg[3]) << 16) | (uint32_t(msg[4]) << 8) | msg[5];
  const bool rainFrame = (msg[16] & 1) != 0;
  const bool tempOK = !rainFrame && msg[12] <= 0x99 && (msg[13] & 0xf0) <= 0x90;
  const int tempRaw = (msg[12] >> 4) * 100 + (msg[12] & 15) * 10 + (msg[13] >> 4);
  const float temperature = (tempRaw - ((msg[13] & 8) ? 1000 : 0)) * 0.1f;
  const int humidity = (msg[14] >> 4) * 10 + (msg[14] & 15);
  const uint8_t w7 = msg[7] ^ 0xff, w8 = msg[8] ^ 0xff, w9 = msg[9] ^ 0xff;
  const bool windOK = w7 <= 0x99 && w8 <= 0x99 && w9 <= 0x99;
  const float gust = ((w7 >> 4) * 100 + (w7 & 15) * 10 + (w8 >> 4)) * 0.1f;
  const float wind = ((w9 >> 4) * 100 + (w9 & 15) * 10 + (w8 & 15)) * 0.1f;
  const int direction = (msg[10] >> 4) * 100 + (msg[10] & 15) * 10 + (msg[11] >> 4);
  const uint8_t r12 = msg[12] ^ 0xff, r13 = msg[13] ^ 0xff, r14 = msg[14] ^ 0xff;
  const bool rainOK = rainFrame && r12 <= 0x99 && r13 <= 0x99 && r14 <= 0x99;
  const float rain = ((r12 >> 4) * 100000 + (r12 & 15) * 10000 + (r13 >> 4) * 1000 + (r13 & 15) * 100 + (r14 >> 4) * 10 + (r14 & 15)) * 0.1f;
  log("===== SENSOR DATA =====");
  log("Sensor ID       : " + String(id, HEX));
  storeReading(SENSOR_ID, String(id, HEX));
  if (tempOK) {
    const int8_t battery = (msg[13] & 2) ? 1 : 0;
    if (lastBattery == battery) { if (batteryStableCount < 255) ++batteryStableCount; }
    else { lastBattery = battery; batteryStableCount = 1; }
    log("Battery OK      : " + String(battery ? "Yes" : "No"));
    if (batteryStableCount >= 2) storeReading(BATTERY, battery ? "1" : "0");
    log("Temperature (C): " + String(temperature));
    log("Humidity (%)    : " + String(humidity));
    storeReading(TEMPERATURE, String(temperature));
    storeReading(HUMIDITY, String(humidity));
  }
  if (windOK) {
    log("Wind Gust (m/s) : " + String(gust));
    log("Wind Speed (m/s): " + String(wind));
    log("Wind Direction  : " + String(direction));
    storeReading(GUST, String(gust));
    storeReading(WIND, String(wind));
    storeReading(DIRECTION, String(direction));
  }
  if (rainOK) { log("Rainfall (mm)   : " + String(rain)); storeReading(RAIN, String(rain)); }
  log("========================");
  return true;
}

void IRAM_ATTR onPacket() {
  portENTER_CRITICAL_ISR(&radioMux);
  packetReady = true;
  portEXIT_CRITICAL_ISR(&radioMux);
}

bool armRadio() {
  portENTER_CRITICAL(&radioMux);
  packetReady = false;
  portEXIT_CRITICAL(&radioMux);
  const int result = radio.startReceive();
  radioReceiving = result == RADIOLIB_ERR_NONE;
  if (!radioReceiving) log("[CC1101] Start receive failed: " + String(result));
  return radioReceiving;
}

void serviceRadio() {
  if (otaBusy.load()) return;
  if (!radioReady) {
    if (!Recovery::elapsed(millis(), lastRadioAttempt, 10000)) return;
    lastRadioAttempt = millis();
    int result = radio.begin(917.0, 8.22, 57.136417, 270.0, 10, 32);
    if (result == RADIOLIB_ERR_NONE) result = radio.setCrcFiltering(false);
    if (result == RADIOLIB_ERR_NONE) result = radio.fixedPacketLengthMode(27);
    if (result == RADIOLIB_ERR_NONE) result = radio.setSyncWord(0xAA, 0x2D, 0, false);
    if (result != RADIOLIB_ERR_NONE) { log("[CC1101] Init failed: " + String(result)); return; }
    radio.setPacketReceivedAction(onPacket);
    radioReady = true;
    log("[CC1101] Initialized; asynchronous reception active");
  }
  if (!radioReceiving) {
    if (armRadio()) return;
    radioReady = false;
    lastRadioAttempt = millis();
    return;
  }
  portENTER_CRITICAL(&radioMux);
  const bool ready = packetReady;
  packetReady = false;
  portEXIT_CRITICAL(&radioMux);
  if (!ready) return;
  uint8_t packet[27] = {};
  const int result = radio.readData(packet, sizeof(packet));
  radioReceiving = false;
  armRadio();
  if (result != RADIOLIB_ERR_NONE) { log("[CC1101] Read failed: " + String(result)); return; }
  for (size_t offset = 0; offset + 18 <= sizeof(packet); ++offset) {
    if (decodeBresser6In1Full(packet + offset, 18)) break;
  }
}

void setup() {
  Serial.begin(115200);
  messages = xQueueCreate(32, sizeof(LogMessage));
  Serial.printf("\nBresser recovery %s; no automatic device restart\n", VERSION);
  if (!messages) Serial.println("[ERROR] Diagnostic queue allocation failed");
  ArduinoOTA.setHostname(OTA_NAME);
  ArduinoOTA.onStart([]() {
    if (etcoreStarted) etcore.sendBye();
    otaBusy.store(true);
    if (radioReady) radio.standby();
    radioReceiving = false;
    Serial.println("[OTA] Upload started");
  });
  ArduinoOTA.onEnd([]() { Serial.println("[OTA] Complete; rebooting"); });
  ArduinoOTA.onError([](ota_error_t error) {
    otaBusy.store(false);
    if (etcoreStarted) { etcore.sendHello(); stateDirty = true; }
    Serial.printf("[OTA] Error %u; resuming reception\n", unsigned(error));
  });
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    disconnectReason.store(info.wifi_sta_disconnected.reason);
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  lastWiFiAttempt = millis() - Recovery::WIFI_RETRY_MS;
  lastRadioAttempt = millis() - 10000;
  etcore.setPort(BRESSER_ETCORE_PORT);
  etcoreConfigured = etcore.enableEncryptionHex(BRESSER_ETCORE_KEY);
  if (!etcoreConfigured) Serial.println("[ET-Core] Invalid encryption key; telemetry disabled");
  etcore.onSync(publishState);
}

void loop() {
  serviceWiFi();
  serviceRadio();
  serviceETCore();
  serviceLog();
  delay(1);
}
