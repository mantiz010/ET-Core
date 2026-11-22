#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace Recovery {
constexpr uint32_t WIFI_RETRY_MS = 30000;
constexpr uint32_t FRESH_MS = 120000;
inline bool elapsed(uint32_t now, uint32_t then, uint32_t interval) {
  return uint32_t(now - then) >= interval;
}
struct Value {
  char text[32] = {};
  uint32_t at = 0;
  uint32_t revision = 0;
  bool pending = false;
  void set(const char* next, uint32_t now) {
    const size_t length = strlen(next);
    if (length >= sizeof(text)) return;
    memcpy(text, next, length + 1);
    at = now;
    ++revision;
    pending = true;
  }
  bool ready(uint32_t now) const {
    return pending && !elapsed(now, at, FRESH_MS);
  }
  void accepted(uint32_t sentRevision) {
    if (revision == sentRevision) pending = false;
  }
};
}
