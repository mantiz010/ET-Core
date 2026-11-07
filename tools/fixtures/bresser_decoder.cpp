#include <cassert>
#include <cstdint>
#include <cstddef>
#include <string>
#include <sstream>
#include <cmath>
#include <iostream>
constexpr int HEX = 16;
struct String : std::string {
  using std::string::string;
  String(const std::string& s) : std::string(s) {}
  String(int n) : std::string(std::to_string(n)) {}
  String(float n) : std::string(std::to_string(n)) {}
  String(uint32_t n, int) { std::ostringstream out; out << std::hex << n; assign(out.str()); }
};
enum Reading { TEMPERATURE, HUMIDITY, GUST, WIND, DIRECTION, RAIN, BATTERY, SENSOR_ID };
std::string saved[8];
unsigned writes[8] = {};
int8_t lastBattery = -1;
uint8_t batteryStableCount = 0;
void log(const String&) {}
void storeReading(Reading index, const String& text) { saved[index] = text; ++writes[index]; }
#include "production-decoder.inc"
void seal(uint8_t (&message)[18]) {
  const uint16_t digest = lfsrDigest16(message + 2, 15, 0x8810, 0x5412);
  message[0] = digest >> 8; message[1] = digest;
  unsigned sum = 0;
  for (unsigned i = 2; i < 17; ++i) sum += message[i];
  message[17] = uint8_t(0xff - sum);
}
int main() {
  uint8_t message[18] = {0x5c,0xe4,0x18,0x80,0x02,0xc3,0x18,0xfb,0xba,0xfc,0x26,0x98,0x11,0x84,0x81,0xff,0xf0,0x16};
  assert(decodeBresser6In1Full(message, 18));
  assert(std::fabs(std::stof(saved[TEMPERATURE]) - 11.8f) < 0.001);
  assert(saved[HUMIDITY] == "81" && saved[SENSOR_ID] == "188002c3");
  assert(writes[BATTERY] == 0);
  assert(decodeBresser6In1Full(message, 18));
  assert(writes[BATTERY] == 1);
  message[2] = 0x97; message[3] = 0x48; message[4] = 0x47; message[5] = 0x41;
  seal(message);
  assert(decodeBresser6In1Full(message, 18));
  assert(saved[SENSOR_ID] == "97484741");
  const unsigned temperatureBefore = writes[TEMPERATURE], batteryBefore = writes[BATTERY];
  message[16] |= 1;
  message[12] = message[13] = message[14] = 0xff;
  seal(message);
  assert(decodeBresser6In1Full(message, 18));
  assert(writes[RAIN] == 1 && std::stof(saved[RAIN]) == 0);
  assert(writes[TEMPERATURE] == temperatureBefore && writes[BATTERY] == batteryBefore);
  // A rain frame must not become temperature/battery merely because the
  // shared bytes happen to pass the old numeric validity check.
  message[12] = 0x12; message[13] = 0x32; message[14] = 0x34;
  seal(message);
  assert(decodeBresser6In1Full(message, 18));
  assert(writes[TEMPERATURE] == temperatureBefore && writes[BATTERY] == batteryBefore);
  message[0] ^= 1;
  assert(!decodeBresser6In1Full(message, 18));
  assert(!decodeBresser6In1Full(message, 17));
  std::cout << "PASS: production decoder: reference packet, battery debounce, high-bit sensor ID, zero rain, frame separation and corrupt/truncated packet rejection\n";
}
