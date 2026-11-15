#!/bin/sh
# One command: build the C node, take a read-only snapshot of the EdgeWeave
# Python side, start a Hub from it and run the C node against it.
#
#   tests/interop/run.sh            # everything
#   EDGEWEAVE_REPO=/path tests/interop/run.sh
#   EDGEWEAVE_REF=<rev> ...         # which EdgeWeave commit to test against
#
# The EdgeWeave checkout is never modified: `git archive` exports the chosen
# commit into a scratch directory and the Python side runs from there.
set -eu

here="$(cd "$(dirname "$0")/../.." && pwd)"
repo="${EDGEWEAVE_REPO:-$HOME/Projects/edgeweave}"
ref="${EDGEWEAVE_REF:-HEAD}"
work="$(mktemp -d "${TMPDIR:-/var/tmp}/borrowlink-interop.XXXXXX")"
trap 'rm -rf "$work"' EXIT HUP INT TERM

mkdir -p "$work/ew" "$work/build"
git -C "$repo" archive "$ref" | tar -x -C "$work/ew"
echo "EdgeWeave: $(git -C "$repo" rev-parse --short "$ref") ($ref), exported to a scratch copy"

cc_flags="-std=c11 -O1 -g -Wall -Wextra -Werror -pedantic"
ld_flags=""
if [ "${BL_INTEROP_SAN:-0}" = "1" ]; then
    # Build the C side with ASan + UBSan (same library lookup as tests/host/run.sh).
    libdir="${BL_SAN_LIBDIR:-/var/tmp/bl-sanlibs}"
    cc_flags="$cc_flags -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
    if ! printf 'int main(void){return 0;}\n' | cc -x c - $cc_flags -o "$work/probe" 2>/dev/null; then
        ld_flags="-L$libdir -Wl,-rpath,$libdir"
    fi
    echo "C side built with ASan + UBSan"
fi
make -s -C "$here/examples/linux-node" OUT="$work/build" CFLAGS="$cc_flags" LDFLAGS="$ld_flags"
cc $cc_flags $ld_flags -I"$here/include" "$here/tests/host/crosscheck_tool.c" \
    "$here/src/wire.c" "$here/src/session.c" "$here/src/binding.c" \
    "$here/profiles/message.c" -o "$work/build/crosscheck_tool"

export EDGEWEAVE_DIR="$work/ew"
export BL_NODE_BIN="$work/build/borrowlink-linux-node"
export BL_CROSSCHECK_BIN="$work/build/crosscheck_tool"
export BL_INTEROP_OUT="${BL_INTEROP_OUT:-$work/out}"
export PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH="$here/tests:$work/ew"
mkdir -p "$BL_INTEROP_OUT"

cd "$here"
/usr/bin/python3 -m unittest -v interop.test_crosscheck interop.test_interop

if [ -n "${BL_KEEP_TRACE:-}" ]; then
    mkdir -p "$BL_KEEP_TRACE"
    cp "$BL_INTEROP_OUT"/trace-*.jsonl "$BL_KEEP_TRACE"/
fi
