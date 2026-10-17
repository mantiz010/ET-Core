#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ETChaCha20Poly1305.h>

#define ETCORE_LIBRARY_VERSION "0.2.0"
#ifndef ETCORE_DEFAULT_PORT
#define ETCORE_DEFAULT_PORT 5560
#endif
// Recent command ids remembered so a resend is acked again but not re-run.
#define ETCORE_CMD_HISTORY 4

class ETCore {
public:
  typedef void (*CommandHandler)(const char* dev_class, JsonObject payload);
  typedef void (*SyncHandler)();
  typedef void (*HelloHandler)(JsonObject payload);

  ETCore();

  void begin(const char* device_id, const char* device_class, const char* device_name, const char* fw_version);
  void loop();
  void onCommand(CommandHandler cb);
  // Called when the hub asks for a hello (it just started or does not know us):
  // the sketch should send its current state with sendState().
  void onSync(SyncHandler cb);
  // Lets the sketch add fields to its hello, e.g. a light's "effects" list.
  void onHello(HelloHandler cb);

  bool enableEncryptionHex(const char* psk_hex);
  void disableEncryption();
  bool encryptionEnabled() const;

  void sendHello();
  void sendLease();
  void sendState(JsonObject payload);
  void sendAck(const char* cmd_id, bool ok);
  // Call just before a planned restart (OTA, ESP.restart()): the hub shows the
  // device offline at once instead of when its lease runs out.
  void sendBye();

  void setLeaseInterval(uint32_t ms);
  void setLeaseTtl(uint16_t seconds);
  // With no authenticated packet from the hub for this long (default 100 s, a bit
  // over 3 hub announce periods) the device forgets the hub address and asks again
  // by multicast, so it finds Home Assistant again after it moved address.
  void setHubSilentTimeout(uint32_t ms);

private:
  void _sendEnvelope(const char* type, JsonObject payload, bool multicast_fallback);
  void _sendStale(const char* cmd_id);
  void _learnHub(IPAddress ip);
  void _makeBootId();
  void _makeSessionNonce();
  bool _parseHex32(const char* hex, uint8_t out[32]);
  void _deriveKey();
  size_t _aad(const char* type, uint32_t seq, char* out, size_t out_len, const char* id = nullptr);
  bool _encryptPayload(const char* type, uint32_t seq, JsonObject plain, JsonObject out);
  bool _decryptPayload(const char* type, uint32_t seq, JsonObject wrapper, JsonDocument& out,
                       const uint8_t* key = nullptr, uint8_t direction = 2, const char* aad_id = nullptr);
  void _handleAnnounce(JsonDocument& doc, uint32_t now);
  void _sessionHex(char out[15]);
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
  uint8_t _session[7] = {0};  // random per boot: nonce bytes 1-7
  uint32_t _lastLeaseMs = 0;
  uint32_t _lastHubRxMs = 0;
  uint32_t _hubSilentMs = 100000;
  struct CmdSeen { char id[24]; bool ok; };
  CmdSeen _cmdHist[ETCORE_CMD_HISTORY] = {};
  uint8_t _cmdHistNext = 0;
  char _curCmd[24] = {0};
  bool _curAcked = false;
  bool _curOk = true;
  bool _helloPending = false;
  uint32_t _helloDueMs = 0;
  uint32_t _leaseIntervalMs = 2000;
  uint16_t _leaseTtlSec = 6;

  bool _cryptoEnabled = false;
  uint8_t _psk[32] = {0};
  uint8_t _key[32] = {0};
  uint8_t _siteKey[32] = {0};  // hub multicast announces: sha256(psk || "\0etcore-announce")
  uint32_t _lastHelloReqMs = 0;
  bool _helloReqSeen = false;
  char _hubCh[9] = {0};  // last challenge from the hub, echoed in lease and hello

  CommandHandler _cmdHandler = nullptr;
  SyncHandler _syncHandler = nullptr;
  HelloHandler _helloHandler = nullptr;
};
