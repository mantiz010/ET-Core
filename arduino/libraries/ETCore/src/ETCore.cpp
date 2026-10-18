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

  _udp.beginMulticast(_mcast, _port);
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

void ETCore::onHello(HelloHandler cb) {
  _helloHandler = cb;
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
  for (uint8_t i = 0; i < sizeof(_siteKey); i++) _siteKey[i] = 0;
}

bool ETCore::encryptionEnabled() const {
  return _cryptoEnabled;
}

void ETCore::setPort(uint16_t port) {
  if (port) _port = port;
}

void ETCore::setLeaseInterval(uint32_t ms) {
  if (ms >= 500) _leaseIntervalMs = ms;
}

void ETCore::setLeaseTtl(uint16_t seconds) {
  if (seconds >= 2) _leaseTtlSec = seconds;
}

void ETCore::setHubSilentTimeout(uint32_t ms) {
  if (ms >= 1000) _hubSilentMs = ms;
}

// Called only for packets that authenticated as coming from the hub.
void ETCore::_learnHub(IPAddress ip) {
  _hubIp = ip;
  _hubKnown = true;
  _lastHubRxMs = millis();
}

void ETCore::loop() {
  const uint32_t now = millis();
  if (_hubKnown && now - _lastHubRxMs >= _hubSilentMs) _hubKnown = false;  // ask again
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
  // Read the whole datagram, then parse: parsing straight from the UDP stream
  // reads it a byte at a time (measured ~2-3 ms slower per command on a C3).
  static char buf[ETCORE_MAX_PACKET + 1];
  if (packetSize > ETCORE_MAX_PACKET) return;  // the next parsePacket() drops the rest
  const int len = _udp.read((uint8_t*)buf, ETCORE_MAX_PACKET);
  if (len <= 0) return;
  buf[len] = 0;

  // Nearly every packet a device gets is an encrypted command in the hub's fixed
  // layout: pick its fields straight out of the text (~40 us) instead of a full
  // JSON parse (~750 us on an ESP32). Anything else takes the general path below.
  if (_cryptoEnabled) {
    const char *cls, *n64, *c64, *t64;
    uint32_t seq;
    if (_scanCmd(buf, len, &cls, &seq, &n64, &c64, &t64)) {
      _runCmd(cls, seq, n64, c64, t64, JsonObject());
      return;
    }
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, (const char*)buf, (size_t)len);
  if (err) return;

  if ((int)(doc["v"] | 0) != 1) return;

  const char* type = doc["type"] | "";

  if (strcmp(type, "announce") == 0) {
    _handleAnnounce(doc, now);
    return;
  }

  if (strcmp(type, "cmd") != 0) return;

  JsonObject wrapper = doc["payload"].as<JsonObject>();
  if (_cryptoEnabled) {
    if (wrapper["_enc"].as<int>() != 1) return;
    _runCmd(doc["class"] | "", doc["seq"].as<uint32_t>(), wrapper["n"] | "", wrapper["ct"] | "",
            wrapper["tag"] | "", JsonObject());
  } else {
    _runCmd(doc["class"] | "", doc["seq"].as<uint32_t>(), nullptr, nullptr, nullptr, wrapper);
  }
}

// One command, from either receive path. With a key, n64/c64/t64 are the
// encrypted payload; without one, plain is the payload as sent.
void ETCore::_runCmd(const char* cls, uint32_t seq, const char* n64, const char* c64,
                     const char* t64, JsonObject plain) {
  if (_cmdHandler) {
    // seq is bound into the encryption (see _aad), so it cannot be rewritten.
    if (seq == 0) return;
    // Old seq without encryption: nothing to authenticate, just drop it.
    if (!_cryptoEnabled && seq <= _lastCmdSeq) return;

    JsonObject payload = plain;
    JsonDocument plainDoc;
    if (_cryptoEnabled) {
      if (!_decryptB64("cmd", seq, n64, c64, t64, plainDoc)) return;
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
    _cmdHandler(cls, payload);
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

// ---- fast command scan -------------------------------------------------------
// The hub writes commands as compact JSON in a fixed order:
//   {"v":1,"type":"cmd","id":"..","class":"..","seq":N,"payload":{"_enc":1,
//    "alg":"C20P1305","n":"..","ct":"..","tag":".."}}
// Finds each field's value; fails (-> full JSON parse) on anything unexpected,
// e.g. an escaped character. Nothing is trusted from it: seq, type and id are
// bound into the authentication tag, so a doctored packet just fails to decrypt.
static const char* _jkey(const char* s, const char* end, const char* key) {
  const size_t kl = strlen(key);
  for (const char* p = s + 1; p + kl + 3 <= end; p++) {  // a key always follows '{' or ','
    if (p[0] == '"' && (p[-1] == '{' || p[-1] == ',') && memcmp(p + 1, key, kl) == 0 &&
        p[kl + 1] == '"' && p[kl + 2] == ':') {
      return p + kl + 3;
    }
  }
  return nullptr;
}
// A string value: returns its start and sets *vend at the closing quote.
static const char* _jstr(const char* s, const char* end, const char* key, const char** vend) {
  const char* v = _jkey(s, end, key);
  if (!v || v >= end || *v != '"') return nullptr;
  v++;
  const char* q = v;
  while (q < end && *q != '"') {
    if (*q == '\\' || (uint8_t)*q < 0x20) return nullptr;
    q++;
  }
  if (q >= end) return nullptr;
  *vend = q;
  return v;
}

bool ETCore::_scanCmd(char* buf, int len, const char** cls, uint32_t* seq, const char** n64,
                      const char** c64, const char** t64) {
  static const char kHead[] = "{\"v\":1,\"type\":\"cmd\",";
  const char* end = buf + len;
  if (len < (int)sizeof(kHead) || memcmp(buf, kHead, sizeof(kHead) - 1) != 0) return false;
  // One pass: each field is looked for after the previous one (the hub's order).
  const char *ce, *ne, *cte, *te;
  const char* c = _jstr(buf, end, "class", &ce);
  if (!c) return false;
  const char* sv = _jkey(ce, end, "seq");
  if (!sv) return false;
  uint32_t s = 0;
  const char* d = sv;
  while (d < end && *d >= '0' && *d <= '9') {
    if (s > 429496729u) return false;
    s = s * 10 + (uint32_t)(*d - '0');
    d++;
  }
  if (d == sv || d >= end || *d != ',') return false;
  const char* pay = _jkey(d, end, "payload");
  if (!pay || *pay != '{') return false;
  const char* enc = _jkey(pay, end, "_enc");
  if (!enc || end - enc < 2 || enc[0] != '1' || enc[1] != ',') return false;
  const char* n = _jstr(enc, end, "n", &ne);
  if (!n) return false;
  const char* ct = _jstr(ne, end, "ct", &cte);
  if (!ct) return false;
  const char* t = _jstr(cte, end, "tag", &te);
  if (!t) return false;
  // All found: cut the values out in place.
  buf[ce - buf] = 0;
  buf[ne - buf] = 0;
  buf[cte - buf] = 0;
  buf[te - buf] = 0;
  *cls = c;
  *seq = s;
  *n64 = n;
  *c64 = ct;
  *t64 = t;
  return true;
}

// The hub announces itself: multicast at start and every 30 s (site key), and
// unicast to one device when it first hears it (that device's key, addressed to
// this boot with _ds). With a key set, an announce is only believed if it
// decrypts and the hub address inside it is the address it came from, so a
// forged or recorded announce sent from elsewhere cannot point the device away.
// Never answered, except that "hello": true gets one hello (+ onSync) back, at
// most once per 10 s, after a random delay so a big site does not answer at once.
void ETCore::_handleAnnounce(JsonDocument& doc, uint32_t now) {
  const char* to = doc["id"] | "";
  const bool unicast = _id && strcmp(to, _id) == 0;
  if (!unicast && strcmp(to, "hub") != 0) return;
  JsonObject payload = doc["payload"].as<JsonObject>();
  JsonDocument plainDoc;
  if (_cryptoEnabled) {
    uint32_t seq = doc["seq"].as<uint32_t>();
    if (!_decryptPayload("announce", seq, payload, plainDoc, unicast ? _key : _siteKey, 3, to)) return;
    payload = plainDoc.as<JsonObject>();
    IPAddress claimed;
    if (!claimed.fromString(payload["hub"] | "") || claimed != _udp.remoteIP()) return;
    if (unicast) {
      char ds[15];
      _sessionHex(ds);
      if (strcmp(payload["_ds"] | "", ds) != 0) return;
    }
  }
  _learnHub(_udp.remoteIP());
  // A new challenge is fresh by construction, so its hello request is never held
  // back; anything else (incl. a replayed announce) at most once per 10 s.
  const char* ch = payload["ch"] | "";
  const bool newCh = unicast && _cryptoEnabled && ch[0] && strlen(ch) < sizeof(_hubCh) &&
                     strcmp(ch, _hubCh) != 0;
  if ((payload["hello"] | false) && !_helloPending &&
      (newCh || !_helloReqSeen || now - _lastHelloReqMs >= 10000)) {
    _helloReqSeen = true;
    _lastHelloReqMs = now;
    _helloPending = true;
    _helloDueMs = now + (unicast ? esp_random() % 100 : esp_random() % 2000);
  }
  // A hub that has not seen this boot yet sends a challenge; until a packet
  // carries it back the hub ignores us (it cannot tell us from a recording of an
  // earlier boot). Answer at once with a lease unless a hello is about to go.
  if (newCh) {
    snprintf(_hubCh, sizeof(_hubCh), "%s", ch);
    if (!_helloPending) {
      sendLease();
      _lastLeaseMs = now;
    }
  }
}

void ETCore::_sendEnvelope(const char* type, JsonObject payload, bool multicast_fallback) {
  ++_seq;
  IPAddress target = _hubKnown ? _hubIp : _mcast;
  if (!_hubKnown && !multicast_fallback) return;
  if (_cryptoEnabled && _sendEncryptedFast(type, payload, target)) return;

  JsonDocument doc;
  doc["v"] = 1;
  doc["type"] = type;
  doc["id"] = _id ? _id : "";
  doc["class"] = _class ? _class : "";
  doc["boot"] = _boot;
  doc["seq"] = _seq;

  JsonObject out = doc["payload"].to<JsonObject>();
  if (_cryptoEnabled) {
    if (!_encryptPayload(type, _seq, payload, out)) return;
  } else {
    for (JsonPair kv : payload) {
      out[kv.key()] = kv.value();
    }
  }

  _udp.beginPacket(target, _port);
  serializeJson(doc, _udp);
  _udp.endPacket();
}

static bool _jsonSafe(const char* s) {
  for (; *s; s++) {
    if (*s == '"' || *s == '\\' || (uint8_t)*s < 0x20) return false;
  }
  return true;
}

// The encrypted envelope written straight into one buffer: the same JSON the
// ArduinoJson path produces, ~1 ms faster per message on an ESP32. Falls back
// (returns false) if id/class need escaping. Uses _seq already incremented.
bool ETCore::_sendEncryptedFast(const char* type, JsonObject payload, IPAddress target) {
  const char* id = _id ? _id : "";
  const char* cls = _class ? _class : "";
  if (!_jsonSafe(type) || !_jsonSafe(id) || !_jsonSafe(cls) || !_jsonSafe(_boot)) return false;
  char n64[24], t64[28];
  const char* c64;
  if (!_encryptB64(type, _seq, payload, n64, &c64, t64)) return true;  // logged; nothing to send
  static char pkt[ETCORE_MAX_PLAIN * 4 / 3 + 320];
  const int n = snprintf(pkt, sizeof(pkt),
                         "{\"v\":1,\"type\":\"%s\",\"id\":\"%s\",\"class\":\"%s\",\"boot\":\"%s\","
                         "\"seq\":%lu,\"payload\":{\"_enc\":1,\"alg\":\"C20P1305\",\"n\":\"%s\","
                         "\"ct\":\"%s\",\"tag\":\"%s\"}}",
                         type, id, cls, _boot, (unsigned long)_seq, n64, c64, t64);
  if (n <= 0 || n >= (int)sizeof(pkt)) return false;
  _udp.beginPacket(target, _port);
  _udp.write((const uint8_t*)pkt, (size_t)n);
  _udp.endPacket();
  return true;
}

void ETCore::sendHello() {
  JsonDocument d;
  JsonObject p = d.to<JsonObject>();
  p["name"] = _name ? _name : "";
  p["fw"] = _fw ? _fw : "";
  p["lib"] = ETCORE_LIBRARY_VERSION;
  p["lease_interval_ms"] = _leaseIntervalMs;
  p["ttl"] = _leaseTtlSec;
  if (_hubCh[0]) p["ch"] = _hubCh;
  JsonArray features = p.createNestedArray("features");
  features.add("lease");
  features.add("ack");
  features.add("state_on_change");
  if (_cryptoEnabled) {
    features.add("chacha20_poly1305");
    p["enc"] = "chacha20_poly1305";
  }
  if (_helloHandler) _helloHandler(p);
  _sendEnvelope("hello", p, true);
}

void ETCore::sendLease() {
  JsonDocument d;
  JsonObject p = d.to<JsonObject>();
  p["ttl"] = _leaseTtlSec;
  p["uptime"] = (uint32_t)(millis() / 1000);
  p["rssi"] = WiFi.RSSI();
  if (_hubCh[0]) p["ch"] = _hubCh;
  if (!_hubKnown) p["nohub"] = true;  // multicast: the hub answers with an announce
  _sendEnvelope("lease", p, true);
}

void ETCore::sendState(JsonObject payload) {
  _sendEnvelope("state", payload, true);
}

void ETCore::sendError(const char* code, const char* message) {
  JsonDocument d;
  JsonObject p = d.to<JsonObject>();
  p["code"] = code && code[0] ? code : "error";
  if (message && message[0]) p["message"] = message;
  _sendEnvelope("error", p, true);
}

// Same payloads as the ET-Bus helpers, so ET-Bus sketches port with no other change.
void ETCore::sendSwitchState(bool on) {
  JsonDocument d;
  JsonObject p = d.to<JsonObject>();
  p["on"] = on;
  sendState(p);
}

void ETCore::sendSwitchesState(const bool* states, uint8_t count) {
  JsonDocument d;
  JsonObject sw = d["switches"].to<JsonObject>();
  char id[4];
  for (uint8_t i = 0; i < count; i++) {
    snprintf(id, sizeof(id), "%u", (unsigned)(i + 1));
    sw[id] = states[i];
  }
  sendState(d.as<JsonObject>());
}

void ETCore::sendRgbStateFx(bool on, uint8_t r, uint8_t g, uint8_t b, uint8_t brightness,
                            const char* effect, uint8_t speed) {
  JsonDocument d;
  JsonObject p = d.to<JsonObject>();
  p["on"] = on;
  p["r"] = r;
  p["g"] = g;
  p["b"] = b;
  p["brightness"] = brightness;
  p["effect"] = effect && effect[0] ? effect : "solid";
  p["speed"] = speed;
  sendState(p);
}

void ETCore::sendFanSpeed(bool on, uint8_t percent) {
  JsonDocument d;
  JsonObject p = d.to<JsonObject>();
  p["on"] = on;
  p["speed"] = percent > 100 ? 100 : percent;
  sendState(p);
}

void ETCore::sendFanPreset(bool on, const char* preset) {
  JsonDocument d;
  JsonObject p = d.to<JsonObject>();
  p["on"] = on;
  p["preset"] = preset && preset[0] ? preset : "off";
  sendState(p);
}

void ETCore::sendValue(float value, const char* unit) {
  JsonDocument d;
  JsonObject p = d.to<JsonObject>();
  p["value"] = value;
  if (unit && unit[0]) p["unit"] = unit;
  sendState(p);
}

void ETCore::sendBye() {
  JsonDocument d;
  JsonObject p = d.to<JsonObject>();
  p["reason"] = "restart";
  _sendEnvelope("bye", p, true);
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
  // The site key for hub multicast announces. The label starts with a 0 byte,
  // which no device id can contain, so no device key can ever equal it.
  static const char label[] = "\0etcore-announce";
  memcpy(input + 32, label, sizeof(label) - 1);
  mbedtls_sha256(input, 32 + sizeof(label) - 1, _siteKey, 0);
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

static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

bool ETCore::_b64encode(const uint8_t* in, size_t in_len, char* out, size_t out_len) {
  const size_t need = (in_len + 2) / 3 * 4;
  if (!out || need >= out_len) return false;
  char* o = out;
  size_t i = 0;
  for (; i + 3 <= in_len; i += 3) {
    const uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
    *o++ = kB64[v >> 18];
    *o++ = kB64[(v >> 12) & 63];
    *o++ = kB64[(v >> 6) & 63];
    *o++ = kB64[v & 63];
  }
  if (i < in_len) {
    uint32_t v = (uint32_t)in[i] << 16;
    if (i + 1 < in_len) v |= (uint32_t)in[i + 1] << 8;
    *o++ = kB64[v >> 18];
    *o++ = kB64[(v >> 12) & 63];
    *o++ = (i + 1 < in_len) ? kB64[(v >> 6) & 63] : '=';
    *o++ = '=';
  }
  *o = '\0';
  return true;
}

static inline int _b64val(char ch) {
  if (ch >= 'A' && ch <= 'Z') return ch - 'A';
  if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
  if (ch >= '0' && ch <= '9') return ch - '0' + 52;
  if (ch == '+') return 62;
  if (ch == '/') return 63;
  return -1;
}

// Strict: whole groups of 4, padding only at the end.
bool ETCore::_b64decode(const char* in, uint8_t* out, size_t out_len, size_t* written) {
  if (!in || !out || !written) return false;
  const size_t n = strlen(in);
  if (n % 4) return false;
  size_t o = 0;
  for (size_t i = 0; i < n; i += 4) {
    const bool last = (i + 4 == n);
    const int a = _b64val(in[i]), b = _b64val(in[i + 1]);
    if (a < 0 || b < 0) return false;
    const bool p2 = last && in[i + 2] == '=', p3 = last && in[i + 3] == '=';
    if (p2 && !p3) return false;
    const int c = p2 ? 0 : _b64val(in[i + 2]), d = p3 ? 0 : _b64val(in[i + 3]);
    if (c < 0 || d < 0) return false;
    const uint32_t v = ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6) | (uint32_t)d;
    const size_t k = p2 ? 1 : (p3 ? 2 : 3);
    if (o + k > out_len) return false;
    out[o++] = (uint8_t)(v >> 16);
    if (k > 1) out[o++] = (uint8_t)(v >> 8);
    if (k > 2) out[o++] = (uint8_t)v;
  }
  *written = o;
  return true;
}

// Associated data: the envelope fields the receiver acts on. Encrypting with them
// bound in means a packet cannot be relabelled (ack -> lease) or have its seq
// rewritten without failing authentication.
size_t ETCore::_aad(const char* type, uint32_t seq, char* out, size_t out_len, const char* id) {
  if (!id) id = _id ? _id : "";
  int n = snprintf(out, out_len, "v1|%s|%s|%lu", type ? type : "", id, (unsigned long)seq);
  return (n > 0 && (size_t)n < out_len) ? (size_t)n : 0;
}

bool ETCore::_encryptPayload(const char* type, uint32_t seq, JsonObject plain, JsonObject out) {
  char n64[24], t64[28];
  const char* c64;
  if (!_encryptB64(type, seq, plain, n64, &c64, t64)) return false;
  out["_enc"] = 1;
  out["alg"] = "C20P1305";
  out["n"] = n64;
  out["ct"] = c64;
  out["tag"] = t64;
  return true;
}

// Encrypts plain; n64/t64 get the nonce and tag, *c64 points at the ciphertext
// (a static buffer, valid until the next call).
bool ETCore::_encryptB64(const char* type, uint32_t seq, JsonObject plain, char n64[24],
                         const char** c64_out, char t64[28]) {
  // Static: too big for the loop task's stack. Sends all happen from loop().
  // A hello with a long effect list is ~800 bytes; the packet then goes out as
  // IP fragments, which the hub reassembles.
  static char pt[ETCORE_MAX_PLAIN];
  static uint8_t ct[ETCORE_MAX_PLAIN];
  static char c64[ETCORE_MAX_PLAIN * 4 / 3 + 8];
  size_t pt_len = serializeJson(plain, pt, sizeof(pt));
  if (pt_len == 0 || pt_len >= sizeof(pt)) {
    log_e("ETCore: '%s' message too big to send (%u bytes, max %u)", type,
          (unsigned)measureJson(plain), (unsigned)(sizeof(pt) - 1));
    return false;
  }

  uint8_t nonce[12];
  uint8_t tag[16];
  char aad[128];
  size_t aad_len = _aad(type, seq, aad, sizeof(aad));
  if (!aad_len) return false;
  _makeNonce(1, seq, nonce);
  if (!ETChaCha20Poly1305::encrypt(_key, nonce, (const uint8_t*)aad, aad_len, (const uint8_t*)pt, pt_len, ct, tag)) {
    return false;
  }

  if (!_b64encode(nonce, sizeof(nonce), n64, 24)) return false;
  if (!_b64encode(ct, pt_len, c64, sizeof(c64))) return false;
  if (!_b64encode(tag, sizeof(tag), t64, 28)) return false;
  *c64_out = c64;
  return true;
}

bool ETCore::_decryptPayload(const char* type, uint32_t seq, JsonObject wrapper, JsonDocument& out,
                             const uint8_t* key, uint8_t direction, const char* aad_id) {
  if (wrapper["_enc"].as<int>() != 1) return false;
  return _decryptB64(type, seq, wrapper["n"] | "", wrapper["ct"] | "", wrapper["tag"] | "", out, key,
                     direction, aad_id);
}

bool ETCore::_decryptB64(const char* type, uint32_t seq, const char* n64, const char* c64,
                         const char* t64, JsonDocument& out, const uint8_t* key, uint8_t direction,
                         const char* aad_id) {
  if (!key) key = _key;
  if (!n64 || !c64 || !t64) return false;

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
  if (nonce[0] != direction) return false;

  char aad[128];
  size_t aad_len = _aad(type, seq, aad, sizeof(aad), aad_id);
  if (!aad_len) return false;
  if (!ETChaCha20Poly1305::decrypt(key, nonce, (const uint8_t*)aad, aad_len, ct, ct_len, tag, pt)) {
    return false;
  }
  pt[ct_len] = 0;
  return deserializeJson(out, pt, ct_len) == DeserializationError::Ok;
}
