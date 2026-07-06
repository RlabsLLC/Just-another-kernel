#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

KERNEL_ELF="build/kernel.elf"
BUILD_DIR="build"
BOOT_ISO="$BUILD_DIR/kernel.iso"

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

ensure_bootloader_tools() {
    local missing_tools=()

    if ! command -v grub-mkrescue >/dev/null 2>&1; then
        missing_tools+=(grub-mkrescue)
    fi

    if ! command -v xorriso >/dev/null 2>&1; then
        missing_tools+=(xorriso)
    fi

    if ! command -v mformat >/dev/null 2>&1; then
        missing_tools+=(mtools)
    fi

    if (( ${#missing_tools[@]} > 0 )); then
        log "Missing boot tooling: ${missing_tools[*]}. Install grub-pc-bin, xorriso, and mtools, then re-run this script."
        exit 1
    fi
}

build_boot_iso() {
    local staging_dir
    staging_dir="$(mktemp -d)"

    cleanup() {
        rm -rf "$staging_dir"
    }
    trap cleanup RETURN

    mkdir -p "$staging_dir/boot/grub"
    cp "$KERNEL_ELF" "$staging_dir/boot/kernel.elf"

    cat > "$staging_dir/boot/grub/grub.cfg" <<'EOF'
set timeout=0
set default=0

# Prefer graphical terminal on firmware that lacks legacy text console.
if loadfont /boot/grub/fonts/unicode.pf2; then
    insmod all_video
    insmod gfxterm
    terminal_output gfxterm
else
    terminal_output console
fi

# Keep keyboard input available across firmware implementations.
terminal_input at_keyboard
terminal_input --append console
terminal_input --append serial

menuentry "Just another kernel" {
    set gfxpayload=keep
    insmod multiboot
    multiboot /boot/kernel.elf
    boot
}
EOF

    grub-mkrescue -o "$BOOT_ISO" "$staging_dir" >/dev/null
}

main() {
    build_kernel_if_needed
    ensure_bootloader_tools

    local qemu_bin
    if ! qemu_bin="$(find_qemu)"; then
        install_qemu
        qemu_bin="$(find_qemu)" || {
            log "QEMU installation did not provide qemu-system-i386 or qemu-system-x86_64."
            exit 1
        }
    fi

    build_boot_iso

    log "Booting ${KERNEL_ELF} with VGA output and 1GB RAM using ${qemu_bin}."

    exec "$qemu_bin" \
        -m 1G \
        -cdrom "$BOOT_ISO" \
        -boot d \
        -no-reboot \
        -no-shutdown \
        "$@"
}

main "$@"
