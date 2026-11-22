# Force sensing resistor

Force sensing resistor used as a variable resistance, a 10000 Ω resistor and a measured 2.0 V excitation supply.

## Wiring

2.0 V excitation → fixed resistor → GPIO1 → sensing element → GND. For a potentiometer use the wiper and one end terminal. Share the supply ground.

## Run

Follow the [Thread commissioning and build instructions](../README.md).

## Bench check

Substitute a resistor equal to the fixed resistor. Check for approximately half the excitation voltage and the expected resistance. Then vary the sensing element.
