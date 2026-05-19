#include "ETCore.h"

ETCore::ETCore() : _mcast(239, 10, 0, 2), _hubIp(0, 0, 0, 0) {}

void ETCore::_makeBootId() {
  uint32_t a = micros();
  uint32_t b = millis();
#if defined(ARDUINO_ARCH_ESP32)
  a ^= esp_random();
  b ^= esp_random();
#endif
  snprintf(_boot, sizeof(_boot), "%08lx%08lx", (unsigned long)a, (unsigned long)b);
}

void ETCore::begin(const char* device_id, const char* device_class, const char* device_name, const char* fw_version) {
  _id = device_id;
  _class = device_class;
  _name = device_name;
  _fw = fw_version;
  _makeBootId();

#if defined(ARDUINO_ARCH_ESP32)
  WiFi.setSleep(false);
#endif

  _udp.beginMulticast(_mcast, ETCORE_DEFAULT_PORT);
  sendHello();
  sendLease();
  _lastLeaseMs = millis();
}

void ETCore::onCommand(CommandHandler cb) {
  _cmdHandler = cb;
}

void ETCore::setLeaseInterval(uint32_t ms) {
  if (ms >= 500) _leaseIntervalMs = ms;
}

void ETCore::setLeaseTtl(uint16_t seconds) {
  if (seconds >= 2) _leaseTtlSec = seconds;
}

void ETCore::_learnHub(IPAddress ip) {
  if (!_hubKnown || _hubIp != ip) {
    _hubIp = ip;
    _hubKnown = true;
  }
}

void ETCore::loop() {
  const uint32_t now = millis();
  if (now - _lastLeaseMs >= _leaseIntervalMs) {
    sendLease();
    _lastLeaseMs = now;
  }

  const int packetSize = _udp.parsePacket();
  if (!packetSize) return;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, _udp);
  if (err) return;

  if ((int)(doc["v"] | 0) != 1) return;

  const char* type = doc["type"] | "";
  if (strcmp(type, "cmd") != 0) return;

  _learnHub(_udp.remoteIP());

  if (_cmdHandler) {
    JsonObject payload = doc["payload"].as<JsonObject>();
    _cmdHandler(doc["class"] | "", payload);
  }
}

void ETCore::_sendEnvelope(const char* type, JsonObject payload, bool multicast_fallback) {
  JsonDocument doc;
  doc["v"] = 1;
  doc["type"] = type;
  doc["id"] = _id ? _id : "";
  doc["class"] = _class ? _class : "";
  doc["boot"] = _boot;
  doc["seq"] = ++_seq;

  JsonObject out = doc.createNestedObject("payload");
  for (JsonPair kv : payload) {
    out[kv.key()] = kv.value();
  }

  IPAddress target = _hubKnown ? _hubIp : _mcast;
  if (!_hubKnown && !multicast_fallback) return;

  _udp.beginPacket(target, ETCORE_DEFAULT_PORT);
  serializeJson(doc, _udp);
  _udp.endPacket();
}

void ETCore::sendHello() {
  JsonDocument d;
  JsonObject p = d.to<JsonObject>();
  p["name"] = _name ? _name : "";
  p["fw"] = _fw ? _fw : "";
  p["lib"] = ETCORE_LIBRARY_VERSION;
  p["lease_interval_ms"] = _leaseIntervalMs;
  p["ttl"] = _leaseTtlSec;
  JsonArray features = p.createNestedArray("features");
  features.add("lease");
  features.add("ack");
  features.add("state_on_change");
  _sendEnvelope("hello", p, true);
}

void ETCore::sendLease() {
  JsonDocument d;
  JsonObject p = d.to<JsonObject>();
  p["ttl"] = _leaseTtlSec;
  p["uptime"] = (uint32_t)(millis() / 1000);
  p["rssi"] = WiFi.RSSI();
  _sendEnvelope("lease", p, true);
}

void ETCore::sendState(JsonObject payload) {
  _sendEnvelope("state", payload, true);
}

void ETCore::sendAck(const char* cmd_id, bool ok) {
  JsonDocument d;
  JsonObject p = d.to<JsonObject>();
  p["cmd_id"] = cmd_id ? cmd_id : "";
  p["ok"] = ok;
  _sendEnvelope("ack", p, false);
}
