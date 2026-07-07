# Just Another Kernel 26.5.1

This is a kernel completly built with vibe coding with GPT-5.3-codex and other models.

### This patch includes:
- A working terminal with commands
- A virtual FS in the RAM
- Custom VGA drivers that work in most places
- Near instant boot
- GRUB

### Fixed Bugs (from 26.5a)
- Keyboard Puller fixed
- Rendering in VGA mode is fixed
- Backspace doing "? ?" has been fixed
- Other bugfixes.
---
# How to run

Simplist way is to go to https://copy.sh/v86/ then
1. Copy exact .iso file from the build/ folder
2. Place it in the Custom CD Image (ISO) upload button
3. Press "Start Emulation"

## Other ways to run

Running *directly* on the hardware
1. Copy exact .iso file from the build/ folder
2. Depending on your operating system, get an ISO flasher.
3. Flash a USB (minimum 1GB) with the ISO.
4. Plug in your computer
5. Restart the computer and select the USB as boot device

*Note:* TPM could block the kernel, so you may need to disable TPM in the BIOS.

6. Now you are in the kernel!

Using Qemu for emulation (the hardest way)

#### LINUX RECOMENDED!!
1. clone or download repository zip (recomended to download zip from the release page)
2. chmod the `./run-kernel.sh` file
3. execute `./run-kernel.sh` and install any dependencies requested by the script.

*Note:* This will compile it on the spot, then load it into Qemu.

4. Wait for the command prompt to pull up a Qemu Window.
5. If any bugs happen in the kernel/build, report them please.
6. You are now in the Qemu Emulator with the kernel.

Any recomendations and feedback is welcome. This project is free to copy.
