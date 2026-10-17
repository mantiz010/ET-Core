#include "ETCore.h"

#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>

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

void ETCore::_makeSessionNonce() {
  // 56 random bits per boot, so two boots of one device (same key) practically
  // never share a nonce, even over many thousands of restarts.
  for (uint8_t i = 0; i < sizeof(_session); i++) _session[i] = (uint8_t)esp_random();
}

void ETCore::begin(const char* device_id, const char* device_class, const char* device_name, const char* fw_version) {
  _id = device_id;
  _class = device_class;
  _name = device_name;
  _fw = fw_version;
  _makeBootId();
  _makeSessionNonce();
  if (_cryptoEnabled) _deriveKey();

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

void ETCore::onSync(SyncHandler cb) {
  _syncHandler = cb;
}

bool ETCore::enableEncryptionHex(const char* psk_hex) {
  if (!_parseHex32(psk_hex, _psk)) {
    disableEncryption();
    return false;
  }
  _cryptoEnabled = true;
  _deriveKey();
  return true;
}

void ETCore::disableEncryption() {
  _cryptoEnabled = false;
  for (uint8_t i = 0; i < sizeof(_psk); i++) _psk[i] = 0;
  for (uint8_t i = 0; i < sizeof(_key); i++) _key[i] = 0;
}

bool ETCore::encryptionEnabled() const {
  return _cryptoEnabled;
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
  if (_helloPending && (int32_t)(now - _helloDueMs) >= 0) {
    _helloPending = false;
    sendHello();
    if (_syncHandler) _syncHandler();  // a new hub has no state for us yet
  }

  const int packetSize = _udp.parsePacket();
  if (!packetSize) return;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, _udp);
  if (err) return;

  if ((int)(doc["v"] | 0) != 1) return;

  const char* type = doc["type"] | "";

  // The hub announces itself at start and every 30 s. Learning its address moves
  // leases from multicast to unicast. Never answered, except that an announce
  // with "hello" (hub just started) gets one hello back after a random delay so
  // a big site does not answer all at once.
  if (strcmp(type, "announce") == 0) {
    _learnHub(_udp.remoteIP());
    if (doc["payload"]["hello"] | false) {
      _helloPending = true;
      _helloDueMs = now + (esp_random() % 2000);
    }
    return;
  }

  if (strcmp(type, "cmd") != 0) return;

  if (_cmdHandler) {
    // seq is bound into the encryption (see _aad), so it cannot be rewritten.
    uint32_t seq = doc["seq"].as<uint32_t>();
    if (seq == 0) return;
    // Old seq without encryption: nothing to authenticate, just drop it.
    if (!_cryptoEnabled && seq <= _lastCmdSeq) return;

    JsonObject payload = doc["payload"].as<JsonObject>();
    JsonDocument plainDoc;
    if (_cryptoEnabled) {
      if (payload["_enc"].as<int>() != 1) return;
      if (!_decryptPayload("cmd", seq, payload, plainDoc)) return;
      payload = plainDoc.as<JsonObject>();
      // The hub addresses each command to this boot's session (see hub _ds).
      char ds[15];
      _sessionHex(ds);
      if (strcmp(payload["_ds"] | "", ds) != 0) return;
      payload.remove("_ds");
      // A genuine command for this boot but with an old seq: the hub's counter went
      // back (e.g. HA restored from a backup). Tell it the last seq we accepted so
      // it can jump above it. Never executed. (A replay only gets this reply too.)
      if (seq <= _lastCmdSeq) {
        _sendStale(payload["cmd_id"] | "");
        return;
      }
    }

    _lastCmdSeq = seq;
    _learnHub(_udp.remoteIP());  // only after the command checked out

    // A resend after a lost ack carries the same cmd_id with a new seq:
    // answer it again, but do not run the command twice.
    const char* cmd_id = payload["cmd_id"] | "";
    if (cmd_id[0]) {
      for (uint8_t i = 0; i < ETCORE_CMD_HISTORY; i++) {
        if (strcmp(_cmdHist[i].id, cmd_id) == 0) {
          sendAck(cmd_id, _cmdHist[i].ok);
          return;
        }
      }
    }

    snprintf(_curCmd, sizeof(_curCmd), "%s", cmd_id);
    _curAcked = false;
    _curOk = true;
    _cmdHandler(doc["class"] | "", payload);
    // The sketch may call sendAck(cmd_id, false) itself; otherwise ack as handled.
    if (_curCmd[0] && !_curAcked) sendAck(_curCmd, true);
    if (_curCmd[0]) {
      snprintf(_cmdHist[_cmdHistNext].id, sizeof(_cmdHist[0].id), "%s", _curCmd);
      _cmdHist[_cmdHistNext].ok = _curOk;
      _cmdHistNext = (uint8_t)((_cmdHistNext + 1) % ETCORE_CMD_HISTORY);
    }
    _curCmd[0] = 0;
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

  JsonObject out = doc["payload"].to<JsonObject>();
  if (_cryptoEnabled) {
    if (!_encryptPayload(type, _seq, payload, out)) return;
  } else {
    for (JsonPair kv : payload) {
      out[kv.key()] = kv.value();
    }
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
  if (_cryptoEnabled) {
    features.add("chacha20_poly1305");
    p["enc"] = "chacha20_poly1305";
  }
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

void ETCore::_sendStale(const char* cmd_id) {
  JsonDocument d;
  JsonObject p = d.to<JsonObject>();
  p["cmd_id"] = cmd_id ? cmd_id : "";
  p["ok"] = false;
  p["err"] = "stale";
  p["last_seq"] = _lastCmdSeq;
  _sendEnvelope("ack", p, false);
}

void ETCore::sendAck(const char* cmd_id, bool ok) {
  if (_curCmd[0] && cmd_id && strcmp(cmd_id, _curCmd) == 0) {
    _curAcked = true;
    _curOk = ok;
  }
  JsonDocument d;
  JsonObject p = d.to<JsonObject>();
  p["cmd_id"] = cmd_id ? cmd_id : "";
  p["ok"] = ok;
  _sendEnvelope("ack", p, false);
}

bool ETCore::_parseHex32(const char* hex, uint8_t out[32]) {
  if (!hex || strlen(hex) != 64) return false;
  for (uint8_t i = 0; i < 32; i++) {
    char a = hex[i * 2];
    char b = hex[i * 2 + 1];
    auto val = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    int hi = val(a);
    int lo = val(b);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

void ETCore::_deriveKey() {
  uint8_t input[96];
  size_t id_len = _id ? strlen(_id) : 0;
  if (id_len > 64) id_len = 64;
  memcpy(input, _psk, 32);
  if (id_len) memcpy(input + 32, _id, id_len);
  mbedtls_sha256(input, 32 + id_len, _key, 0);
  for (size_t i = 0; i < sizeof(input); i++) input[i] = 0;
}

void ETCore::_sessionHex(char out[15]) {
  for (uint8_t i = 0; i < sizeof(_session); i++) snprintf(out + i * 2, 3, "%02x", _session[i]);
}

void ETCore::_makeNonce(uint8_t direction, uint32_t seq, uint8_t nonce[12]) {
  nonce[0] = direction;
  memcpy(nonce + 1, _session, sizeof(_session));
  nonce[8] = (uint8_t)seq;
  nonce[9] = (uint8_t)(seq >> 8);
  nonce[10] = (uint8_t)(seq >> 16);
  nonce[11] = (uint8_t)(seq >> 24);
}

bool ETCore::_b64encode(const uint8_t* in, size_t in_len, char* out, size_t out_len) {
  size_t olen = 0;
  int rc = mbedtls_base64_encode((uint8_t*)out, out_len, &olen, in, in_len);
  if (rc != 0 || olen >= out_len) return false;
  out[olen] = '\0';
  return true;
}

bool ETCore::_b64decode(const char* in, uint8_t* out, size_t out_len, size_t* written) {
  if (!in || !out || !written) return false;
  size_t olen = 0;
  int rc = mbedtls_base64_decode(out, out_len, &olen, (const uint8_t*)in, strlen(in));
  if (rc != 0) return false;
  *written = olen;
  return true;
}

// Associated data: the envelope fields the receiver acts on. Encrypting with them
// bound in means a packet cannot be relabelled (ack -> lease) or have its seq
// rewritten without failing authentication.
size_t ETCore::_aad(const char* type, uint32_t seq, char* out, size_t out_len) {
  int n = snprintf(out, out_len, "v1|%s|%s|%lu", type ? type : "", _id ? _id : "", (unsigned long)seq);
  return (n > 0 && (size_t)n < out_len) ? (size_t)n : 0;
}

bool ETCore::_encryptPayload(const char* type, uint32_t seq, JsonObject plain, JsonObject out) {
  char pt[768];
  size_t pt_len = serializeJson(plain, pt, sizeof(pt));
  if (pt_len == 0 || pt_len >= sizeof(pt)) return false;

  uint8_t nonce[12];
  uint8_t ct[768];
  uint8_t tag[16];
  char aad[128];
  size_t aad_len = _aad(type, seq, aad, sizeof(aad));
  if (!aad_len) return false;
  _makeNonce(1, seq, nonce);
  if (!ETChaCha20Poly1305::encrypt(_key, nonce, (const uint8_t*)aad, aad_len, (const uint8_t*)pt, pt_len, ct, tag)) {
    return false;
  }

  char n64[32];
  char c64[1100];
  char t64[32];
  if (!_b64encode(nonce, sizeof(nonce), n64, sizeof(n64))) return false;
  if (!_b64encode(ct, pt_len, c64, sizeof(c64))) return false;
  if (!_b64encode(tag, sizeof(tag), t64, sizeof(t64))) return false;

  out["_enc"] = 1;
  out["alg"] = "C20P1305";
  out["n"] = n64;
  out["ct"] = c64;
  out["tag"] = t64;
  return true;
}

bool ETCore::_decryptPayload(const char* type, uint32_t seq, JsonObject wrapper, JsonDocument& out) {
  if (wrapper["_enc"].as<int>() != 1) return false;
  const char* n64 = wrapper["n"] | "";
  const char* c64 = wrapper["ct"] | "";
  const char* t64 = wrapper["tag"] | "";

  uint8_t nonce[12];
  uint8_t ct[768];
  uint8_t tag[16];
  uint8_t pt[769];
  size_t n_len = 0;
  size_t ct_len = 0;
  size_t tag_len = 0;

  if (!_b64decode(n64, nonce, sizeof(nonce), &n_len) || n_len != sizeof(nonce)) return false;
  if (!_b64decode(c64, ct, sizeof(ct), &ct_len)) return false;
  if (!_b64decode(t64, tag, sizeof(tag), &tag_len) || tag_len != sizeof(tag)) return false;
  if (nonce[0] != 2) return false;

  char aad[128];
  size_t aad_len = _aad(type, seq, aad, sizeof(aad));
  if (!aad_len) return false;
  if (!ETChaCha20Poly1305::decrypt(_key, nonce, (const uint8_t*)aad, aad_len, ct, ct_len, tag, pt)) {
    return false;
  }
  pt[ct_len] = 0;
  return deserializeJson(out, pt, ct_len) == DeserializationError::Ok;
}
