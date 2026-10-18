// Host (Linux) stand-in for the bits of the Arduino core ETCore uses.
// Only for the ET-Core simulator; never part of a firmware build.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <fcntl.h>
#include <unistd.h>

inline uint32_t millis() {
  using namespace std::chrono;
  static const auto t0 = steady_clock::now();
  return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}
inline uint32_t micros() {
  using namespace std::chrono;
  static const auto t0 = steady_clock::now();
  return (uint32_t)duration_cast<microseconds>(steady_clock::now() - t0).count();
}
#define log_e(...) (fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))
inline void delay(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
inline uint32_t esp_random() {
  uint32_t v = 0;
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd >= 0) { if (read(fd, &v, sizeof v) != (ssize_t)sizeof v) v = (uint32_t)micros(); close(fd); }
  return v;
}

class Print {
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t* buf, size_t n) { size_t k = 0; while (k < n && write(buf[k])) k++; return k; }
};

class Printable {
public:
  virtual ~Printable() {}
  virtual size_t printTo(Print& p) const = 0;
};

class Stream : public Print {
public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual size_t readBytes(char* buf, size_t n) { size_t k = 0; int c; while (k < n && (c = read()) >= 0) buf[k++] = (char)c; return k; }
};

struct SerialStub { template <typename T> void print(const T&) {} template <typename T> void println(const T&) {} void println() {} void begin(int) {} };
[[maybe_unused]] static SerialStub Serial;
