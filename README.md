# Custom Minimal C Kernel [Patch 26.4.1 - Bootable Universal]

This is a minimal educational x86 kernel written in C with:

- A Multiboot-compliant entry point
- VGA text-mode terminal output
- Basic newline handling and screen scroll
- Driver probing with graceful fallback for common x86 devices (VGA, PS/2 keyboard, PIT, COM1 serial, CMOS RTC, ATA)
- Video driver selection with VBE framebuffer support (for systems without VGA text mode) and serial fallback
- An interactive kernel CLI with commands for diagnostics and control
- A host-side GNU Bash helper mode (`./run-kernel.sh --bash`) for build/run workflows

## Build

```sh
make
```

Output:

- `build/kernel.elf`

## Notes for macOS

To compile this kernel on macOS, install Apple Command Line Tools first:

```sh
xcode-select --install
```

You also need an ELF-capable linker.

Recommended:

- `i686-elf-gcc` and `i686-elf-ld`
- or Clang with `ld.lld`

This Makefile links using `KERNEL_LD` (defaults to `i686-elf-ld`).

Fallback order when `i686-elf-ld` is missing:

1. `ld.lld` from `PATH`
2. `tools/zig/zig ld.lld` (if `tools/zig/zig` exists)

You can still override it manually:

```sh
make KERNEL_LD=ld.lld
```

## Kernel CLI Commands

At the `> ` prompt, use:

- `help`
- `clear`
- `echo <text>`
- `drivers`
- `version`
- `about`
- `mem`
- `video`
- `serial`
- `uptime`
- `halt`
- `reboot`
- `bash` (prints how to use host GNU Bash mode)

## GNU Bash Mode

Run:

```sh
./run-kernel.sh --bash
```

This opens an interactive GNU Bash session on the host with helper commands:

- `kernel-build`
- `kernel-run`
- `kernel-clean`
