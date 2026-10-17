/*
 * BasicRelay.ino — ET-Core WiFi Manager example (the ET-Bus one ported)
 *
 * First boot -> creates the "ETCore-Relay" WiFi access point
 * Connect from a phone -> the setup page opens
 * Enter WiFi + encryption key + device name -> saved in flash
 * Next boot -> connects by itself, no setup page
 *
 * Hold BOOT during startup to clear the settings and open the setup page again.
 * A board that ran the ET-Bus WiFi manager keeps its saved settings.
 */
#include <ETCoreWiFiManager.h>
#include <ETCore.h>

ETCoreWiFiManager wm;
ETCore etcore;

// Keep the Strings alive: ETCore keeps the pointers from begin().
String devName;
String psk;

#define RESET_PIN 0  // BOOT button on most ESP32 boards
#define RELAY_PIN 26

bool relayOn = false;

static void onCommand(const char* dev_class, JsonObject payload) {
  if (!payload["on"].is<bool>()) return;
  relayOn = payload["on"];
  digitalWrite(RELAY_PIN, relayOn ? HIGH : LOW);
  etcore.sendSwitchState(relayOn);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(RESET_PIN, INPUT_PULLUP);
  if (digitalRead(RESET_PIN) == LOW) wm.resetSettings();

  wm.begin("ETCore-Relay");  // blocks until WiFi is connected

  devName = wm.getDevName();
  if (devName.length() == 0) devName = "relay_board";
  psk = wm.getPSK();

  if (psk.length() == 64) etcore.enableEncryptionHex(psk.c_str());
  etcore.begin(devName.c_str(), "switch.relay", "Relay", "v2.0");
  etcore.onCommand(onCommand);
  etcore.onSync([]() { etcore.sendSwitchState(relayOn); });
  etcore.sendSwitchState(relayOn);
}

void loop() {
  etcore.loop();
}
