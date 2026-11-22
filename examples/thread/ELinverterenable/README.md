# EL inverter enable

A enclosed EL panel with logic enable and a driver with an active-high, 3.3 V compatible enable input and a hardware pull-down.

## Wiring

GPIO4 to the driver logic input and GND to logic ground. Use the rated load supply and a protected driver suitable for the load.

## Run

Follow the [Thread commissioning and build instructions](../README.md).

## Bench check

Measure the enable output with the load disconnected. Verify low after reset, high on command, and low on the next off command. Then repeat with the rated load attached.
