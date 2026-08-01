#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

CC_BIN="${CC:-cc}"
WASM_CC="${WASM_CC:-clang}"
WASM_LD="${WASM_LD:-wasm-ld}"
failed=0
found=0

if [ -d tests/support ]; then
    echo "FAIL tests/support is forbidden; every test must be self-contained"
    exit 1
fi

for dir in tests/*; do
    [ -d "$dir" ] || continue
    found=1

    name="$(basename "$dir")"
    source="$dir/$name.c"
    main="$dir/main.c"
    wasm="$dir/$name.wasm"
    runner="$dir/$name.runner"
    log="$dir/$name.log"
    build_log="$dir/.build.log"

    echo "=== $name ==="
    rm -f "$wasm" "$runner" "$build_log"
    : > "$log"

    if [ ! -f "$main" ]; then
        echo "FAIL missing $main" | tee "$log"
        failed=1
        continue
    fi

    if [ ! -f "$source" ]; then
        echo "FAIL missing $source" | tee "$log"
        failed=1
        continue
    fi

    if [ "$name" = "dylink0" ]; then
        library_source="$dir/library.c"
        app_object="$dir/dylink0.o"
        library_object="$dir/library.o"
        library_wasm="$dir/libdylink0.so"
        if [ ! -f "$library_source" ]; then
            echo "FAIL missing $library_source" | tee "$log"
            failed=1
            continue
        fi

        pic_flags=()
        if "$WASM_LD" --help 2>&1 | grep -q -- '--experimental-pic'; then
            pic_flags+=(--experimental-pic)
        fi

        if ! "$WASM_CC" --target=wasm32 -nostdlib -fPIC -O0 -c \
                "$library_source" -o "$library_object" >"$build_log" 2>&1 \
            || ! "$WASM_LD" -m wasm32 "${pic_flags[@]}" --shared --no-entry \
                --unresolved-symbols=import-dynamic \
                --export=library_add --export=library_name \
                --export=library_fill --export=library_apply \
                --export=library_increment \
                -o "$library_wasm" "$library_object" >>"$build_log" 2>&1 \
            || ! "$WASM_CC" --target=wasm32 -nostdlib -fPIC -O0 -c \
                "$source" -o "$app_object" >>"$build_log" 2>&1 \
            || ! "$WASM_LD" -m wasm32 "${pic_flags[@]}" --pie --no-entry \
                --export=app_main --export=app_name_char \
                --export=app_struct --export=app_callback \
                --export=app_counter \
                -o "$wasm" "$app_object" "$library_wasm" \
                >>"$build_log" 2>&1; then
            { echo "FAIL building dylink.0 C modules"; cat "$build_log"; } | tee "$log"
            rm -f "$app_object" "$library_object" "$library_wasm"
            failed=1
            continue
        fi
        rm -f "$app_object" "$library_object"
    elif grep -Eq '#include <(stdio|stdlib|wasi)|__wasi_|proc_exit|fd_write' "$source" || [[ "$name" == wasi_* ]]; then
        if ! "$WASM_CC" --target=wasm32-wasi -O0 -o "$wasm" "$source" >"$build_log" 2>&1; then
            { echo "FAIL building $wasm"; cat "$build_log"; } | tee "$log"
            failed=1
            continue
        fi
    else
        mapfile -t exports < <(grep -Eo 'app_main[A-Za-z0-9_]*[[:space:]]*\(' "$source" | sed -E 's/[[:space:]]*\($//' | sort -u)
        [ "${#exports[@]}" -gt 0 ] || exports=(app_main)
        export_flags=()
        for export_name in "${exports[@]}"; do
            export_flags+=("-Wl,--export=$export_name")
        done

        if ! "$WASM_CC" --target=wasm32 -nostdlib -Wl,--no-entry -O0 "${export_flags[@]}" -o "$wasm" "$source" >"$build_log" 2>&1; then
            { echo "FAIL building $wasm"; cat "$build_log"; } | tee "$log"
            failed=1
            continue
        fi
    fi

    if [ ! -s "$wasm" ]; then
        echo "FAIL generated WASM is missing or empty: $wasm" | tee "$log"
        failed=1
        continue
    fi

    native_flags=(-Dd_m3HasWASI)
    if [ "$name" = "m3c_roundtrip" ]; then
        native_flags+=(-Dd_m3HasM3C=1)
    elif [ "$name" = "dylink0" ]; then
        native_flags+=(-Dd_m3HasDylink=1 -Dd_m3HasM3C=1)
    fi

    if ! "$CC_BIN" -std=c11 -Wall -Wextra -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L "${native_flags[@]}" \
        -Iwasm-rtos \
        "$main" wasm-rtos/os.c wasm-rtos/hal.c wasm-rtos/wasm3/source/*.c \
        -lm -o "$runner" >>"$build_log" 2>&1; then
        { echo "FAIL building $runner"; cat "$build_log"; } | tee "$log"
        failed=1
        continue
    fi

    if ! (cd "$dir" && "./$name.runner"); then
        failed=1
    fi

    if [ ! -s "$log" ]; then
        echo "FAIL generated log is missing or empty: $log"
        failed=1
    fi

    rm -f "$runner" "$build_log"
    if [ "$name" = "dylink0" ]; then
        rm -f "$dir/libdylink0.so"
    fi
done

if [ "$found" -eq 0 ]; then
    echo "FAIL no test directories found"
    exit 1
fi

exit "$failed"
