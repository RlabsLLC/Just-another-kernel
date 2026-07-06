# Custom Minimal C Kernel

This is a minimal educational x86 kernel written in C with:

- A Multiboot-compliant entry point
- VGA text-mode terminal output
- Basic newline handling and screen scroll

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

If `i686-elf-ld` is missing but `tools/zig/zig` exists, it automatically falls back to `tools/zig/zig ld.lld`.

You can still override it manually:

```sh
make KERNEL_LD=ld.lld
```
