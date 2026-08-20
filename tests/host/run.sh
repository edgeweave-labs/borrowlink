#!/bin/sh
set -eu

borrowlink_test_dir="$(mktemp -d /tmp/borrowlink-test.XXXXXX)"
trap 'rm -rf "$borrowlink_test_dir"' EXIT HUP INT TERM

c_flags="-std=c11 -Wall -Wextra -Werror -Iinclude"
cxx_flags="-x c++ -std=c++17 -Wall -Wextra -Werror -Iinclude"

cc $c_flags -c src/wire.c -o "$borrowlink_test_dir/wire.o"

for test_source in \
    tests/host/test_protocol_constants.c \
    tests/host/test_wire.c
do
    test_name="$(basename "$test_source" .c)"
    cc $c_flags "$test_source" "$borrowlink_test_dir/wire.o" \
        -o "$borrowlink_test_dir/$test_name-c"
    "$borrowlink_test_dir/$test_name-c"

    c++ $cxx_flags -c "$test_source" \
        -o "$borrowlink_test_dir/$test_name-cpp.o"
    c++ "$borrowlink_test_dir/$test_name-cpp.o" \
        "$borrowlink_test_dir/wire.o" \
        -o "$borrowlink_test_dir/$test_name-cpp"
    "$borrowlink_test_dir/$test_name-cpp"
done
