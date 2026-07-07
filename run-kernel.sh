#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

KERNEL_ELF="build/kernel.elf"
BUILD_DIR="build"
BOOT_ISO="$BUILD_DIR/kernel.iso"
HOST_BASH_MODE=0

log() {
    printf '[kernel-runner] %s\n' "$*"
}

parse_args() {
    local filtered=()

    for arg in "$@"; do
        case "$arg" in
            --bash)
                HOST_BASH_MODE=1
                ;;
            *)
                filtered+=("$arg")
                ;;
        esac
    done

    set -- "${filtered[@]}"
    RUN_ARGS=("$@")
}

launch_host_bash() {
    local bash_bin
    local bash_banner
    local rc_file

    if ! command -v bash >/dev/null 2>&1; then
        log "GNU Bash was not found on this host."
        exit 1
    fi

    bash_bin="$(command -v bash)"
    bash_banner="$($bash_bin --version 2>/dev/null | head -n 1)"

    if [[ "$bash_banner" != *"GNU bash"* ]]; then
        log "The detected shell at ${bash_bin} is not GNU Bash."
        exit 1
    fi

    rc_file="$(mktemp)"
    printf 'JAK_ROOT=%q\n' "$ROOT_DIR" > "$rc_file"

    cat >> "$rc_file" <<'EOF'
set +e

kernel-build() {
    (cd "$JAK_ROOT" && make "$@")
}

kernel-run() {
    (cd "$JAK_ROOT" && "$JAK_ROOT/run-kernel.sh" "$@")
}

kernel-clean() {
    (cd "$JAK_ROOT" && make clean)
}

rtsi() {
    (cd "$JAK_ROOT" && bash "$JAK_ROOT/rtsi.sh" "$@") &
    JAK_RTSI_PID=$!
    JAK_RTSI_PID_FILE="${TMPDIR:-/tmp}/jak-rtsi.pid"
    printf '%s\n' "$JAK_RTSI_PID" > "$JAK_RTSI_PID_FILE"
    printf '[rtsi] started PID %s\n' "$JAK_RTSI_PID"
}

pkill() {
    if [[ $# -eq 0 ]]; then
        if [[ -n "${JAK_RTSI_PID:-}" ]] && kill -0 "$JAK_RTSI_PID" >/dev/null 2>&1; then
            kill "$JAK_RTSI_PID" >/dev/null 2>&1 || true
            wait "$JAK_RTSI_PID" >/dev/null 2>&1 || true
            unset JAK_RTSI_PID
            return 0
        fi

        if [[ -f "${JAK_RTSI_PID_FILE:-}" ]]; then
            local running_pid
            running_pid="$(cat "$JAK_RTSI_PID_FILE" 2>/dev/null || true)"
            if [[ -n "$running_pid" ]]; then
                kill "$running_pid" >/dev/null 2>&1 || true
            fi
        fi

        return 0
    fi

    command pkill "$@"
}

printf 'JAK GNU Bash shell ready.\n'
printf 'Helpers: kernel-build, kernel-run, kernel-clean\n'
printf 'Helpers: rtsi, pkill\n'
printf 'Project root: %s\n' "$JAK_ROOT"
PS1='jak-bash$ '
EOF

    log "Launching GNU Bash shell from ${bash_bin}."
    exec "$bash_bin" --noprofile --norc --rcfile "$rc_file" -i
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
    parse_args "$@"

    if (( HOST_BASH_MODE == 1 )); then
        launch_host_bash
        return
    fi

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
        "${RUN_ARGS[@]}"
}

main "$@"
