#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#define ETCORE_LIBRARY_VERSION "0.1.0"
#define ETCORE_DEFAULT_PORT 5560

class ETCore {
public:
  typedef void (*CommandHandler)(const char* dev_class, JsonObject payload);

  ETCore();

  void begin(const char* device_id, const char* device_class, const char* device_name, const char* fw_version);
  void loop();
  void onCommand(CommandHandler cb);

  void sendHello();
  void sendLease();
  void sendState(JsonObject payload);
  void sendAck(const char* cmd_id, bool ok);

  void setLeaseInterval(uint32_t ms);
  void setLeaseTtl(uint16_t seconds);

private:
  void _sendEnvelope(const char* type, JsonObject payload, bool multicast_fallback);
  void _learnHub(IPAddress ip);
  void _makeBootId();

  WiFiUDP _udp;
  IPAddress _mcast;
  IPAddress _hubIp;
  bool _hubKnown = false;

  const char* _id = nullptr;
  const char* _class = nullptr;
  const char* _name = nullptr;
  const char* _fw = nullptr;

  uint32_t _seq = 0;
  char _boot[17] = {0};
  uint32_t _lastLeaseMs = 0;
  uint32_t _leaseIntervalMs = 2000;
  uint16_t _leaseTtlSec = 6;

  CommandHandler _cmdHandler = nullptr;
};
