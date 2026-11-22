# Diaphragm pressure switch

Voltage-free NO/COM model of the diaphragm pressure switch.

## Wiring

GPIO4 to NO, GND to COM. Internal pull-up enabled. Contact closure is active.

## Run

Follow the [Thread commissioning and build instructions](../README.md).

## Bench check

In Home Assistant watch the binary entity while you apply pressure within the switch rating. Verify both transitions.
