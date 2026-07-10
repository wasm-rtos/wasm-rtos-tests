#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

CC_BIN="${CC:-cc}"
WASM_CC="${WASM_CC:-clang}"
failed=0

for dir in tests/*; do
    [ -d "$dir" ] || continue
    [ -f "$dir/main.c" ] || continue

    name="$(basename "$dir")"
    source="$dir/$name.c"
    wasm="$dir/$name.wasm"
    runner="$dir/$name.runner"
    log="$dir/$name.log"

    echo "=== $name ===" | tee "$log"

    if [ ! -f "$source" ]; then
        echo "FAIL missing $source" | tee -a "$log"
        failed=1
        continue
    fi

    if grep -Eq '#include <(stdio|stdlib|wasi)|__wasi_|proc_exit|fd_write' "$source" || [[ "$name" == wasi_* ]]; then
        "$WASM_CC" --target=wasm32-wasi -O0 -o "$wasm" "$source" 2>&1 | tee -a "$log" || { failed=1; continue; }
    else
        mapfile -t exports < <(grep -Eo '\bapp_main[A-Za-z0-9_]*[[:space:]]*\(' "$source" | sed -E 's/[[:space:]]*\($//' | sort -u)
        [ "${#exports[@]}" -gt 0 ] || exports=(app_main)
        export_flags=()
        for export_name in "${exports[@]}"; do export_flags+=("-Wl,--export=$export_name"); done
        "$WASM_CC" --target=wasm32 -nostdlib -Wl,--no-entry -O0 "${export_flags[@]}" -o "$wasm" "$source" 2>&1 | tee -a "$log" || { failed=1; continue; }
    fi

    "$CC_BIN" -std=c11 -Wall -Wextra -Werror -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -Dd_m3HasWASI \
        -Iwasm-rtos -Itests/support \
        "$dir/main.c" tests/support/test_support.c wasm-rtos/os.c wasm-rtos/hal.c wasm-rtos/wasm3/source/*.c \
        -lm -o "$runner" 2>&1 | tee -a "$log" || { failed=1; continue; }

    "$runner" 2>&1 | tee -a "$log" || failed=1
    rm -f "$runner"
done

exit "$failed"
