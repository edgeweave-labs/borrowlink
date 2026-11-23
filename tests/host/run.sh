#!/bin/sh
# Host checks: C11 and C++17 builds, the unit tests, then the same tests again
# under ASan + UBSan.  Needs only cc and c++.
#
# Sanitizers: if the system has no libasan/libubsan the sanitizer pass looks
# for them in $BL_SAN_LIBDIR (default /var/tmp/bl-sanlibs).  If they cannot be
# linked the script fails; set BL_SKIP_SANITIZERS=1 to skip on purpose.
set -eu

cd "$(dirname "$0")/../.."
borrowlink_test_dir="$(mktemp -d "${TMPDIR:-/var/tmp}/borrowlink-test.XXXXXX")"
trap 'rm -rf "$borrowlink_test_dir"' EXIT HUP INT TERM

c_flags="-std=c11 -Wall -Wextra -Werror -Iinclude -Iadapters/common"
strict_flags="-pedantic -Wconversion -Wshadow"
cxx_flags="-x c++ -std=c++17 -Wall -Wextra -Werror -Iinclude"

core_sources="src/wire.c src/session.c src/binding.c profiles/message.c \
profiles/http.c"
driver_sources="adapters/common/bl_driver.c"

# Tests that are also compiled as C++17 (header compatibility).
dual_tests="tests/host/test_protocol_constants.c tests/host/test_wire.c"
c_tests="tests/host/test_session.c tests/host/test_message.c \
tests/host/test_binding.c tests/host/test_http.c tests/host/test_fake_adapter.c"

build_core() { # $1 = extra flags, $2 = output dir
    for src in $core_sources $driver_sources; do
        # shellcheck disable=SC2086
        cc $c_flags $strict_flags $1 -c "$src" \
            -o "$2/$(basename "$src" .c).o"
    done
}

echo "== plain build (C11 -pedantic -Wconversion -Wshadow)"
plain="$borrowlink_test_dir/plain"
mkdir -p "$plain"
build_core "" "$plain"
objs="$plain/wire.o $plain/session.o $plain/binding.o $plain/message.o \
$plain/http.o $plain/bl_driver.o"

for test_source in $dual_tests; do
    test_name="$(basename "$test_source" .c)"
    cc $c_flags "$test_source" $plain/wire.o -o "$plain/$test_name-c"
    "$plain/$test_name-c"
    c++ $cxx_flags -c "$test_source" -o "$plain/$test_name-cpp.o"
    c++ "$plain/$test_name-cpp.o" $plain/wire.o -o "$plain/$test_name-cpp"
    "$plain/$test_name-cpp"
done

for test_source in $c_tests; do
    test_name="$(basename "$test_source" .c)"
    # shellcheck disable=SC2086
    cc $c_flags -Itests/fake_adapter "$test_source" tests/fake_adapter/fake_host.c \
        $objs -o "$plain/$test_name"
    "$plain/$test_name"
done

echo "== C++17 header compatibility"
c++ -std=c++17 -Wall -Wextra -Werror -Iinclude -Iadapters/common \
    -c tests/host/test_cxx_headers.cpp -o "$plain/test_cxx_headers.o"
c++ "$plain/test_cxx_headers.o" $objs -o "$plain/test_cxx_headers"
"$plain/test_cxx_headers"

echo "== core purity"
tests/host/check_core_purity.sh "$plain"

if [ "${BL_SKIP_SANITIZERS:-0}" = "1" ]; then
    echo "== sanitizers SKIPPED (BL_SKIP_SANITIZERS=1)"
    echo "OK"
    exit 0
fi

echo "== ASan + UBSan"
san_flags="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g -O1"
san_link=""
probe="$borrowlink_test_dir/probe"
printf 'int main(void){return 0;}\n' > "$probe.c"
# shellcheck disable=SC2086
if ! cc $san_flags "$probe.c" -o "$probe" 2>/dev/null; then
    libdir="${BL_SAN_LIBDIR:-/var/tmp/bl-sanlibs}"
    san_link="-L$libdir -Wl,-rpath,$libdir"
    # shellcheck disable=SC2086
    if ! cc $san_flags $san_link "$probe.c" -o "$probe" 2>/dev/null; then
        echo "error: cannot link ASan/UBSan (no libasan in the system or in $libdir)." >&2
        echo "       Install libasan/libubsan, point BL_SAN_LIBDIR at a directory with" >&2
        echo "       libasan.so and libubsan.so, or set BL_SKIP_SANITIZERS=1." >&2
        exit 1
    fi
fi
san="$borrowlink_test_dir/san"
mkdir -p "$san"
build_core "$san_flags" "$san"
sobjs="$san/wire.o $san/session.o $san/binding.o $san/message.o $san/http.o \
$san/bl_driver.o"
export ASAN_OPTIONS="detect_leaks=1:abort_on_error=0:halt_on_error=1"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"
for test_source in $dual_tests $c_tests; do
    test_name="$(basename "$test_source" .c)"
    # shellcheck disable=SC2086
    cc $c_flags $san_flags $san_link -Itests/fake_adapter "$test_source" \
        tests/fake_adapter/fake_host.c $sobjs -o "$san/$test_name"
    "$san/$test_name"
    echo "  sanitized $test_name ok"
done
echo "OK"
