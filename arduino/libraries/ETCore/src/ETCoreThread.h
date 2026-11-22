#pragma once
#include <ETCore.h>
#include <OThread.h>
#include <ETCoreOpenThread.h>
#include <optional>

// One node per sketch. Provision the Thread dataset before running the application.
class ETCoreThread {
 public:
  ETCore device;
  bool configure(const char* key, const char* address) {
    if (!hub.fromString(address) || hub.type() != IPv6 || !device.enableEncryptionHex(key)) {
      Serial.println("Set the routable hub IPv6 address and 64-digit ET-Core key.");
      return false;
    }
    OThread.begin(false);
    if (!OThread || !OThread.hasActiveDataset()) {
      Serial.println("Commission the board on the Thread network first.");
      return false;
    }
    transport.emplace(hub);
    if (!device.setTransport(&*transport)) return false;
    device.setLeaseInterval(5000);
    device.setLeaseTtl(30);
    OThread.networkInterfaceUp();
    OThread.start();
    configured = true;
    return true;
  }

  bool service(const char* id, const char* kind, const char* name) {
    if (!configured) return false;
    auto role = OThread.otGetDeviceRole();
    if (role == OT_ROLE_DISABLED || role == OT_ROLE_DETACHED) return false;
    if (!started) {
      if (attempted && millis() - lastAttempt < 5000) return false;
      attempted = true;
      lastAttempt = millis();
      device.begin(id, kind, name, "1.0");
      started = device.transportReady();
      if (!started) return false;
    }
    device.loop();
    return true;
  }

 private:
  IPAddress hub;
  std::optional<ETCoreOpenThread> transport;
  uint32_t lastAttempt = 0;
  bool attempted = false;
  bool configured = false;
  bool started = false;
};
