# pd-dualsense
Pd abstraction and Pd-Lua object to connect and display Sony DualSense controller

## usage

* put files in `/dualsense` folder in on of Pd's paths
* create display object via `[dualsense/connect]`
* create connection object via `[dualsense/connect]` - its output can be connected to the display object
* requires a bunch of externals ... pdlua, hidraw, command (maybe more)

## todos

* proper help files and examples will follow
* display object is still missing many input messages for customization
* properly document or remove dependencies
