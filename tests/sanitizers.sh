#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
restore() {
    make -C src clean >/dev/null
    make -C src >/dev/null
}
trap restore EXIT
make -C src clean
make -C src CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -Werror -D_POSIX_C_SOURCE=200809L -g -fsanitize=address,undefined -fno-omit-frame-pointer -no-pie'
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 python3 tests/test_lab4.py >report/inc/sanitizer-tests.log 2>&1
cat report/inc/sanitizer-tests.log
