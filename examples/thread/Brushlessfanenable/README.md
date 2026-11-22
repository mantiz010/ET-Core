# Brushless fan speed

Use a fan controller with an active-high 25 kHz PWM input and an explicit off state at zero duty.

Connect GPIO4 to the driver PWM input, share logic ground, and use the rated load supply. Provide a hardware pull-down. Follow the [Thread setup](../README.md).

In Home Assistant, switch on and try the slider at 25%, 50% and 100%. Check the driver input duty cycle and confirm off after a reset.
