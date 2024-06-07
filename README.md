# Host application for EZSP NCP to act as ZigBee Router

This software lets you use an EZSP NCP, aka USB/Network ZigBee controller with
a Silicon Labs chip as a router. Normally one achieves this by flashing a different
firmware into the chip, one that is designed to work standalone as a ZigBee router.
However in cases where that firmware is hard to find, or you want to expose some
values like "Host CPU temperature", "Host Uptime", or even adding a reset button
to restart the server connected to the stick, this is the only way to do it.

## Why using the GSDK? Why not zigpy?

Mainly because I want to run these on ZigBee IP hubs for which the router firmware
is not available. To run this software on the hub (a MIPS CPU with very little RAM)
there was no other alternative, [bellows](https://github.com/zigpy/bellows) and
[zigpy](https://github.com/zigpy/zigpy) both are python programs, and running python
is impossible on these SOCs. My only alternative was C, and with a limited set of
compiler features.

The only other C/C++ alternative I could find is [libezsp](
https://github.com/Legrandgroup/libezsp), but it doesn't seem to be maintained or
have support for any ZCL features.

## Intializing project from GSDK and building

In the [`gsdk`](./gsdk/) directory you'll find a Dockerfile to build the GSDK image.
This image contains the appropriate environment and binaries used to generate
the projects in the GSDK (also in the image). The main three components are:

1. [GSDK](https://github.com/SiliconLabs/gecko_sdk)
2. [slc-cli](https://github.com/SiliconLabs/circuitpython_slc_cli_linux): CLI
   program to deal with the Simplicity Studio files without actually using
   Simplicity Studio.
3. [ZAP](https://github.com/project-chip/zap): Program to configure the ZCL
   components for the program.

Like any other docker image, you can build it with `docker build -t . "$IMAGE_NAME"`
from the gsdk directory.

With the image built, the project can be generated using the [`gen_project.sh`](
./gsdk/gen_project.sh) script, passing it the name of the image built before,
the directory with patches you want to apply, and the docker command to use
(could use `podman` here). For example:

```bash
./gsdk/gen_project.sh "$IMAGE_NAME" patches/orvibo-minihub/ sudo docker
```

This same script can be re-run in case you modified something on the project file or
ZAP files (to be tested).

Once the project has been initialized and all templates rendered, you can build it using
`make -f ZigbeeMinimalHost.Makefile` from the [`src/`](./src/) directory.

## Notes

The changes made to the default template can be found in the [`patches`](./patches/)
directory.

### Issues with my SOC SDK

The ancient version of GCC I'm using with these hubs has some problems:

1. Doesn't allow for typedef redefinitions, modern versions do
2. No support for `_Static_assert`
3. `sys/ttydefaults.h` isn't included


### Adding callbacks

You can add callbacks (noted nowhere in the documentation) by editing the project
`.slcp` file as one by the [00-add-callbacks.patch](./patches/all/00-add-callbacks.patch)
to find all callback names run the following command:

```bash
cd "$GSDK_DIR"
grep "'callback_type'" \
./protocol/zigbee/app/framework/common/template/zigbee_stack_callback_dispatcher.h.jinja
```
