#pragma once
#include <cstdint>
#include <cstdio>
class IPAddress {
public:
  IPAddress() : _a{0, 0, 0, 0} {}
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : _a{a, b, c, d} {}
  explicit IPAddress(uint32_t be) { for (int i = 0; i < 4; i++) _a[i] = (uint8_t)(be >> (24 - 8 * i)); }
  uint8_t operator[](int i) const { return _a[i]; }
  bool operator==(const IPAddress& o) const { for (int i = 0; i < 4; i++) if (_a[i] != o._a[i]) return false; return true; }
  bool operator!=(const IPAddress& o) const { return !(*this == o); }
  uint32_t be() const { return ((uint32_t)_a[0] << 24) | ((uint32_t)_a[1] << 16) | ((uint32_t)_a[2] << 8) | _a[3]; }
  void str(char* out, size_t n) const { snprintf(out, n, "%u.%u.%u.%u", _a[0], _a[1], _a[2], _a[3]); }
private:
  uint8_t _a[4];
};
