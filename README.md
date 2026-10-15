# Yet/Just Another Kernel 26.5.3

A minimal, yet functional kernel built by Ring Inc.

### Changelog (from 26.5.2)
- Preserve framebuffer pixels beneath the mouse cursor when it moves
- Added PCI function detection (inspection only; no devices are initialized)
- Added a fixed-size kernel heap with allocation/free and a `heap` status command
- Renamed RTCI services to plugins and added kernel handler registration with `plugins`/`plugin <path>` commands
- Added a reusable GRUB boot menu configuration at `boot/grub/grub.cfg`

### Known issues
- The kernel heap is a fixed 64 KiB region; it is not backed by physical-page allocation.
- Plugins are statically registered kernel handlers, not dynamically loaded binaries.
- The 26.5.3 checklist defers paging, scheduling, GUI support, and networking to 26.6.

### vSH
The kernel starts in vSH, its built-in command shell. Type `help` at the `vsh>` prompt to list available commands. The host-side `./run-kernel.sh --bash` option is separate and starts GNU Bash on the host.

### Compiling

To compile, run `run-kernel.sh`

*Note: GRUB and other dependencies will need to be installed*

### Running Pre-compiled

To run the pre-compiled kernel, download the ISO from the `build/` directory and run it in a virtual machine such as QEMU or VirtualBox. You can also burn the ISO to a USB drive and boot from it on your physical machine.

## Suggestions and contributions are welcome! Please open an issue or submit a pull request on GitHub!