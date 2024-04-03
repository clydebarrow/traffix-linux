## Traffix for Linux

This program will listen for and connect to a SkyEcho ADSB device and rebroadcast
data in Flarm format on a UDP socket, port 4353.

XCSoar will receive this data as Flarm if a UDP device is configured with the Flarm protocol.

Obviously the device must have a Wifi connection to the SkyEcho.


Parameters such as port numbers, filtering distanca and altitude are currently hardcoded.

To run, copy to a suitable place and arrange for the program to run on boot. Configure an XCSoar device to read the data. That's it.


## Building

CMake is required.

```sh
cmake .
cmake --build . --target traffix_linux
```

or run the shell script `clean-build.sh`

The executable will be created in the current directory.

## Cross-compile for ARMV7 (32 bit)

To cross-compile an ARM 32 bit version on a host OS (e.g. MacOs) you can use docker:

```shell
docker run --rm -it --platform linux/arm/v7 -v `pwd`:/home/me -w /home/me clydeps/ubuntu-armv7 sh clean-build.sh
```