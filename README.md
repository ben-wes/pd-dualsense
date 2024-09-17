# dualsense library for Pd
Pd external and Pd-Lua object to connect and display Sony DualSense controller

![dualsense/display.pd screenshot](dualsense-display.png)

*(screenshot made with plugdata)*

## usage

* extract `dualsense` from release in your Pd externals folder.
* add `dualsense` to your paths or add `declare -path dualsense` to your patch
* create `[dslink]` object (its output can be connected to the `[dsshow]` object)
* send `open, poll 10` message to connect to controller and poll data in 10ms intervals
* additionally, you can add `[sensors2quat]` and `[sensors2impulse]` between [dslink] and [dsshow] to also display orientation and impulses

## dependencies

* requires `pdlua` external for display (available through deken)
