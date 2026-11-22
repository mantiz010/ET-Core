# Low-voltage relay

Active-high relay driver with 3.3 V logic and a low-voltage indicator load.

## Wiring

GPIO4 to driver IN, common GND. Power the coil from its rated supply.

## Run

Follow the [Thread commissioning and build instructions](../README.md).

## Bench check

Start with the load disconnected. Check that reset leaves the relay off, then toggle it in Home Assistant and confirm both contact continuity and reported state.
