#!/usr/bin/env bash
set -u

if [[ $# -lt 1 ]]; then
    printf 'usage: %s <source.c> [program args...]\n' "$0" >&2
    exit 1
fi

source_file="$1"
shift

if [[ ! -f "$source_file" ]]; then
    printf 'rtsi: source file not found: %s\n' "$source_file" >&2
    exit 1
fi

if ! command -v cc >/dev/null 2>&1; then
    printf 'rtsi: cc was not found on PATH\n' >&2
    exit 1
fi

build_dir="${TMPDIR:-/tmp}"
output_file="$(mktemp "$build_dir/rtsi.XXXXXX")"

cleanup() {
    rm -f "$output_file"
}
trap cleanup EXIT

cc -std=c11 -O2 -Wall -Wextra "$source_file" -o "$output_file" || exit 1
exec "$output_file" "$@"