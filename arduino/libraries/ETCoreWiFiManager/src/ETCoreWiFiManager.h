/*
 * ETCoreWiFiManager.h
 * 
 * Branded WiFi + Encryption Key captive portal for ET-Core ESP32 devices.
 *
 * Install: Copy this folder to Arduino/libraries/ETCoreWiFiManager/
 *
 * Usage:
 *   #include <ETCoreWiFiManager.h>
 *   ETCoreWiFiManager wm;
 *
 *   void setup() {
 *     Serial.begin(115200);
 *     wm.begin("ETCore-Relay");
 *     // WiFi connected — credentials saved in NVS
 *     // wm.getSSID()    — WiFi SSID
 *     // wm.getPassword() — WiFi password
 *     // wm.getPSK()     — 64-char hex encryption key
 *     // wm.getDevName() — device name
 *   }
 */

#ifndef ETCORE_WIFI_MANAGER_H
#define ETCORE_WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

// Saved settings live in this NVS namespace. It is the ET-Bus WiFi manager's, so a
// board moved from ET-Bus to ET-Core keeps its WiFi, key and name (ET-Core derives
// its keys from the same PSK).
#ifndef ETCORE_WM_NVS_NS
#define ETCORE_WM_NVS_NS "etbuswm"
#endif

class ETCoreWiFiManager {
public:
    /// Portal timeout in seconds (default 180). Reboots if no save within this time.
    unsigned long portalTimeout = 180;

    /// Optional AP password (must be 8+ chars, or empty for open AP).
    String apPassword = "";

    /// Start WiFi manager. Connects to saved WiFi or opens captive portal.
    /// Blocks until WiFi is connected.
    /// @param apName  Name of the AP created when portal is active.
    void begin(const char* apName = "ETCore-Setup");

    /// Get saved WiFi SSID.
    /// IMPORTANT: Store the returned String in a variable before calling .c_str().
    /// Do NOT pass getXxx().c_str() directly to functions that store the pointer.
    String getSSID();

    /// Get saved WiFi password.
    String getPassword();

    /// Get saved 64-char hex pre-shared key for ChaCha20-Poly1305 encryption.
    String getPSK();

    /// Get saved device name.
    String getDevName();

    /// Clear all saved settings from NVS. Call before begin() to force portal.
    void resetSettings();

    /// Check if the captive portal is currently active.
    bool isPortalActive();

private:
    String _apName;
    String _ssid;
    String _pass;
    String _psk;
    String _devName;
    bool   _portalActive = false;

    WebServer* _server = nullptr;
    DNSServer* _dns    = nullptr;

    void   _loadNVS();
    void   _saveNVS();
    void   _startPortal();
    void   _sendHtml();
    String _extractJson(const String& json, const char* key);
    String _jsonEsc(const String& s);
};

#endif // ETCORE_WIFI_MANAGER_H
