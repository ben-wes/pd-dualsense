# pd-dualsense
Pd abstraction and Pd-Lua object to connect and display Sony DualSense controller

![dualsense/display.pd screenshot](dualsense-display.png)

*(screenshot made with plugdata)*

## usage

* put files in `/dualsense` folder in one of Pd's paths for externals
* create hid reader object via `[dualsense/listen]` - its output can be connected to the `[dualsense/display]` object. optional argument `orientation` activates tracking of movement and orientation
* requires externals: pdlua and hidraw (fork at https://github.com/ben-wes/pd-hidraw - see releases)

## todos

* proper help files and examples will follow (mainly for documenting the output messages of the [dualsense/connect] object)
* display object is still missing many input messages for customization
