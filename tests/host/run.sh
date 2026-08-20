#!/bin/sh
set -eu

borrowlink_test_dir="$(mktemp -d "${TMPDIR:-/tmp}/borrowlink-test.XXXXXX")"
trap 'rm -rf "$borrowlink_test_dir"' EXIT HUP INT TERM

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Iinclude \
    tests/host/test_protocol_constants.c -o "$borrowlink_test_dir/test-c"
"$borrowlink_test_dir/test-c"

"${CXX:-c++}" -x c++ -std=c++17 -Wall -Wextra -Werror -Iinclude \
    tests/host/test_protocol_constants.c -o "$borrowlink_test_dir/test-cpp"
"$borrowlink_test_dir/test-cpp"
