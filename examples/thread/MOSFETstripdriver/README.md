# MOSFET strip dimmer

Use a 3.3 V compatible low-side MOSFET driver and a single-colour low-voltage strip.

Connect GPIO4 to the driver PWM input, share logic ground, and use the rated load supply. Provide a hardware pull-down. Follow the [Thread setup](../README.md).

In Home Assistant, switch on and try the slider at 25%, 50% and 100%. Check the driver input duty cycle and confirm off after a reset.
