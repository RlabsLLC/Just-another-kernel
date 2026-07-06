# SargentOS (Just Another Kernel - JAK)

A lightweight, bare-metal 32-bit x86 operating system kernel developed entirely from scratch. SargentOS features a polling PS/2 keyboard driver with state management, custom string parsing algorithms, an automated video memory terminal scroll controller, and an interactive command-line shell interface. 

This environment is specifically configured for cross-compiling **x86 target instructions** directly from an **ARM-based mobile environment** using Termux, Clang, and LLVM tools.

---

## 🛠️ Environment Prerequisites

To compile and execute this operating system within Android Termux, install the required native compiler toolchains and processor emulators:

```bash
pkg update && pkg install clang lld qemu-system-i386
