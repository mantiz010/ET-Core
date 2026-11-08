#pragma once
#include "Arduino.h"
#include "IPAddress.h"
#define WL_CONNECTED 3
struct WiFiStub {
  int rssi = -55;
  void setSleep(bool) {}
  int RSSI() const { return rssi; }
  int status() const { return WL_CONNECTED; }
};
extern WiFiStub WiFi;
// Address this simulated device "owns" (127.0.0.x). Set by the simulator before begin().
extern char g_sim_local_ip[16];
