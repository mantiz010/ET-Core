# C3 OTA starter

Flash this over USB first. It stays awake, prints its IP every five seconds and
accepts password-protected ArduinoOTA uploads. No sensors, ETCore library,
MQTT, Home Assistant connection or encryption key are needed for this starter.
It does not change Home Assistant or send test sensor readings.

1. Download [C3OTA.zip](https://github.com/mantiz010/ET-Core/raw/refs/heads/main/downloads/C3OTA.zip), extract it, then open **C3OTA/C3OTA.ino** in Arduino IDE.
2. Edit **WIFI_SSID**, **WIFI_PASSWORD** and **OTA_PASSWORD** at the top of the
   sketch. Use your 2.4 GHz WiFi. Keep the OTA password for future uploads.
   Version **1.1** names each missing setting in the serial monitor. An unset
   OTA password disables OTA only; it no longer prevents WiFi connecting or the
   IP being printed. No passwords are printed.
3. Install/select the Espressif ESP32 board package. Select **ESP32C3 Dev Module**.
4. For a 4 MB board select **Flash Size: 4MB** and **Partition Scheme: Default
   4MB with spiffs** (includes OTA). Do not select a No OTA partition. Check the
   actual board's flash size before using these settings.
5. For native C3 USB select **USB CDC On Boot: Enabled**. For a board connected
   through a USB-to-UART bridge select **Disabled**, so serial output uses UART0.
   This sketch uses no external GPIO, sensor, LED or PSRAM.
6. Select its USB port, upload, then open Serial Monitor at **115200 baud**.
7. Look for `[READY] IP=... OTA=etcore-c3-test.local:3232`. Share that IP when
   ready for the next test. Arduino IDE may also list the hostname as a network
   port; the computer must be able to reach the board on the local network.

Keep USB power connected. Future firmware must retain ArduinoOTA handling to
remain remotely updateable. A deep-sleep test will require an explicit awake OTA
window; this starter itself never sleeps. OTA is local ArduinoOTA, not a browser
upload page or an internet service. Never publish your edited credentials.

Compile-checked with Arduino-ESP32 3.3.11 on ESP32C3 Dev Module, 4 MB flash,
default OTA partition layout, with USB CDC both enabled and disabled. Both clean
builds passed with unchanged source. Physical USB/OTA testing is pending.
