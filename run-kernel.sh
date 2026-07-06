#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

KERNEL_ELF="build/kernel.elf"

log() {
    printf '[kernel-runner] %s\n' "$*"
}

find_qemu() {
    if command -v qemu-system-i386 >/dev/null 2>&1; then
        command -v qemu-system-i386
        return 0
    fi

    if command -v qemu-system-x86_64 >/dev/null 2>&1; then
        command -v qemu-system-x86_64
        return 0
    fi

    return 1
}

install_qemu() {
    local os
    os="$(uname -s)"

    log "QEMU not found. Attempting installation for ${os}."

    case "$os" in
        Darwin)
            if ! command -v brew >/dev/null 2>&1; then
                log "Homebrew is required on macOS to auto-install QEMU."
                log "Install Homebrew, then re-run this script."
                exit 1
            fi
            brew list qemu >/dev/null 2>&1 || brew install qemu
            ;;
        Linux)
            if command -v apt-get >/dev/null 2>&1; then
                sudo apt-get update
                sudo apt-get install -y qemu-system-x86
            elif command -v dnf >/dev/null 2>&1; then
                sudo dnf install -y qemu-system-x86
            elif command -v pacman >/dev/null 2>&1; then
                sudo pacman -Sy --noconfirm qemu-system-x86
            elif command -v zypper >/dev/null 2>&1; then
                sudo zypper --non-interactive install qemu-x86
            else
                log "No supported package manager found for automatic QEMU install."
                exit 1
            fi
            ;;
        *)
            log "Unsupported OS: ${os}. Install QEMU manually."
            exit 1
            ;;
    esac
}

build_kernel_if_needed() {
    if [[ ! -f "$KERNEL_ELF" ]]; then
        log "Kernel artifact missing. Building."
        make
        return
    fi

    if make -q "$KERNEL_ELF" >/dev/null 2>&1; then
        log "Kernel is up-to-date."
    else
        log "Kernel is out-of-date. Rebuilding."
        make
    fi
}

main() {
    build_kernel_if_needed

    local qemu_bin
    if ! qemu_bin="$(find_qemu)"; then
        install_qemu
        qemu_bin="$(find_qemu)" || {
            log "QEMU installation did not provide qemu-system-i386 or qemu-system-x86_64."
            exit 1
        }
    fi

    log "Booting ${KERNEL_ELF} in text mode with 2GB RAM using ${qemu_bin}."

    exec "$qemu_bin" \
        -m 2G \
        -kernel "$KERNEL_ELF" \
        -display curses \
        -no-reboot \
        -no-shutdown \
        "$@"
}

main "$@"
