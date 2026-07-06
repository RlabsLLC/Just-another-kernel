# Custom Minimal C Kernel [Patch 26.4.1 - Bootable Universal]

Changelog & Features:

- A Multiboot-compliant entry point
- VGA text-mode terminal output
- Basic newline handling and screen scroll
- Driver probing with graceful fallback for common x86 devices (VGA, PS/2 keyboard, PIT, COM1 serial, CMOS RTC, ATA)
- Video driver selection with VBE framebuffer support (for systems without VGA text mode) and serial fallback
- A tiny CLI command set including `drivers` and `version`

## Build

```
cd ~/Just-another-kernel/
sudo chmod +x ./run-kernel.sh
./run-kernel.sh
```

Report any issues in the issues tab.
