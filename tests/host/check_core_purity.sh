#!/bin/sh
# The core (src/, profiles/, include/) must stay free of heap, I/O, clock,
# randomness, threads and platform headers (PROTOCOL.md section 14).
# Three independent checks: source text, header text, and the object files'
# undefined symbols.
#   usage: check_core_purity.sh [dir with the core .o files]
set -eu
cd "$(dirname "$0")/../.."

fail=0
core_dirs="src profiles include"

# 1. Includes: only freestanding headers plus <string.h>.
allowed_includes='stddef.h|stdint.h|string.h|borrowlink/[a-z_]+\.h'
bad="$(grep -rhE '^[[:space:]]*#[[:space:]]*include' $core_dirs |
    sed -E 's/^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"]([^>"]+)[>"].*/\1/' |
    grep -vE "^($allowed_includes)\$" || true)"
if [ -n "$bad" ]; then
    echo "FAIL: core includes headers outside the allowed set:" >&2
    echo "$bad" >&2
    fail=1
fi

# 2. Identifiers that mean heap, I/O, time, randomness, threads, platforms.
# (Comments are stripped first so prose may mention them.)
forbidden='\b(malloc|calloc|realloc|free|printf|fprintf|snprintf|sprintf|puts|fopen|fread|fwrite|read|write|open|close|send|recv|socket|connect|select|poll|clock|clock_gettime|gettimeofday|time|rand|srand|random|getrandom|esp_random|pthread_[a-z_]+|errno|abort|exit|assert|ESP_[A-Z_]+|FreeRTOS|xTask[A-Za-z]+|vTask[A-Za-z]+)\b'
for f in $(find $core_dirs -name '*.c' -o -name '*.h'); do
    hits="$(sed -e ':a' -e 's@/\*[^*]*\*\+\([^/*][^*]*\*\+\)*/@@g;/\/\*/{N;ba}' "$f" |
        sed -E 's@//.*$@@' | grep -nE "$forbidden" || true)"
    if [ -n "$hits" ]; then
        echo "FAIL: $f uses a forbidden identifier:" >&2
        echo "$hits" >&2
        fail=1
    fi
done

# 3. The compiled core may only import the memory/string helpers (and its own bl_* functions).
objdir="${1:-}"
if [ -n "$objdir" ]; then
    for o in wire session binding message http; do
        und="$(nm -u "$objdir/$o.o" | awk '{print $2}' |
            grep -vE '^(memcpy|memmove|memset|memcmp|bl_[a-z_]+)$' || true)"
        if [ -n "$und" ]; then
            echo "FAIL: $o.o imports unexpected symbols:" >&2
            echo "$und" >&2
            fail=1
        fi
    done
    # no writable global state: .data and .bss must be empty
    for o in wire session binding message http; do
        sz="$(size -A "$objdir/$o.o" | awk '$1==".data"||$1==".bss"{s+=$2} END{print s+0}')"
        if [ "$sz" != "0" ]; then
            echo "FAIL: $o.o has $sz bytes of writable static data" >&2
            fail=1
        fi
    done
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "core purity OK (includes, identifiers, undefined symbols, no static data)"
