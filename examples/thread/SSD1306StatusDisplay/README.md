# Thread status display

Use a 128 × 64 SSD1306 breakout with 3.3 V I²C logic and the Adafruit SSD1306 and GFX libraries. Connect SDA to GPIO12, SCL to GPIO22, and the breakout supply and ground. Set the module address to 0x3C.

Follow the [commissioning instructions](../README.md). After attachment the display shows the Thread role and uptime. Toggle its Home Assistant switch and confirm the screen blanks and restores. Unplug the display and check the ET-Core error, then reconnect it.

Arduino Library Manager driver: **Adafruit SSD1306** 2.5.17.
