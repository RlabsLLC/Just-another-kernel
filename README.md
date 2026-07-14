# Yet/Just Another Kernel 26.5.2

A minimal, yet functional kernel built by Ring Inc.

### Changelog (from 26.5.1)
- Disabled FS drivers due to data corruption
- Added a new `draw-dot x,y` command to draw a single pixel on the screen
- Added cursor drivers along with a `mouse on/off` command

### Compiling

To compile, run `run-kernel.sh`

*Note: GRUB and other dependencies will need to be installed*

### Running Pre-compiled

To run the pre-compiled kernel, download the ISO from the `build/` directory and run it in a virtual machine such as QEMU or VirtualBox. You can also burn the ISO to a USB drive and boot from it on your physical machine.

## Suggestions and contributions are welcome! Please open an issue or submit a pull request on GitHub!