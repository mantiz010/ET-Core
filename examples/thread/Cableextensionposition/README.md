# Cable extension position

Cable extension position transmitter with linear 4–20 mA output. Example nameplate span 0 to 1000 mm. Set the two constants to the actual transmitter span.

## Wiring

Use an isolated 4–20 mA receiver producing 0.4–2.0 V (100 Ω effective sense resistance). Receiver signal to ADC GPIO1, receiver output ground to GND. Connect the field loop only to the receiver input.

## Run

Follow the [Thread commissioning and build instructions](../README.md).

## Bench check

Use a loop calibrator at 4, 12 and 20 mA. Expect 0, 500 and 1000 mm. Disconnect the field loop and check for a sensor error.
