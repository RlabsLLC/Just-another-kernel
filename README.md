# Yet/Just Another Kernel 26.5.2-2

A minimal, yet functional kernel built by Ring Inc.

### Changelog (from 26.5.2)
- Added cursor texture, instead of being a dot
- Fixed formatting, and added more debugging logs

### Known issues
- The cursor can overwrite the text rendered on the screen, causing some eraser-effect rendering.

### Compiling

To compile, run `run-kernel.sh`

*Note: GRUB and other dependencies will need to be installed*

### Running Pre-compiled

To run the pre-compiled kernel, download the ISO from the `build/` directory and run it in a virtual machine such as QEMU or VirtualBox. You can also burn the ISO to a USB drive and boot from it on your physical machine.

## Suggestions and contributions are welcome! Please open an issue or submit a pull request on GitHub!