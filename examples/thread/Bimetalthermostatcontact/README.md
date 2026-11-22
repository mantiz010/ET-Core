# Bimetal thermostat contact

Voltage-free NO/COM model of the bimetal thermostat contact.

## Wiring

GPIO4 to NO, GND to COM. Internal pull-up enabled. Contact closure is active.

## Run

Follow the [Thread commissioning and build instructions](../README.md).

## Bench check

In Home Assistant watch the binary entity while you warm the sensing face through its rated trip point. Verify both transitions.
