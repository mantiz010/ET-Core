#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ETChaCha20Poly1305.h>

#define ETCORE_LIBRARY_VERSION "0.2.0"
#define ETCORE_DEFAULT_PORT 5560

class ETCore {
public:
  typedef void (*CommandHandler)(const char* dev_class, JsonObject payload);

  ETCore();

  void begin(const char* device_id, const char* device_class, const char* device_name, const char* fw_version);
  void loop();
  void onCommand(CommandHandler cb);

  bool enableEncryptionHex(const char* psk_hex);
  void disableEncryption();
  bool encryptionEnabled() const;

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
  void _makeSessionNonce();
  bool _parseHex32(const char* hex, uint8_t out[32]);
  void _deriveKey();
  size_t _aad(const char* type, uint32_t seq, char* out, size_t out_len);
  bool _encryptPayload(const char* type, uint32_t seq, JsonObject plain, JsonObject out);
  bool _decryptPayload(const char* type, uint32_t seq, JsonObject wrapper, JsonDocument& out);
  void _makeNonce(uint8_t direction, uint32_t seq, uint8_t nonce[12]);
  bool _b64encode(const uint8_t* in, size_t in_len, char* out, size_t out_len);
  bool _b64decode(const char* in, uint8_t* out, size_t out_len, size_t* written);

  WiFiUDP _udp;
  IPAddress _mcast;
  IPAddress _hubIp;
  bool _hubKnown = false;

  const char* _id = nullptr;
  const char* _class = nullptr;
  const char* _name = nullptr;
  const char* _fw = nullptr;

  uint32_t _seq = 0;
  uint32_t _rxSeq = 0;
  uint32_t _lastCmdSeq = 0;
  char _boot[17] = {0};
  uint8_t _session[4] = {0};
  uint32_t _lastLeaseMs = 0;
  bool _helloPending = false;
  uint32_t _helloDueMs = 0;
  uint32_t _leaseIntervalMs = 2000;
  uint16_t _leaseTtlSec = 6;

  bool _cryptoEnabled = false;
  uint8_t _psk[32] = {0};
  uint8_t _key[32] = {0};

  CommandHandler _cmdHandler = nullptr;
};
