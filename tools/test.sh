#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p .test-build
if [ "$(uname -s)" = Darwin ]; then
    sdk=$(xcrun --show-sdk-path)
    export SDKROOT="$sdk"
    # Some Command Line Tools installations don't search the SDK's libc++ headers.
    set -- -isystem "$sdk/usr/include/c++/v1"
else
    set --
fi
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
    "$@" -Iinclude test/test_logic.cpp -o .test-build/test_logic
.test-build/test_logic

subjectlab=${SUBJECTLAB_DIR:-.pio/libdeps/esp32-usb/subjectLab}
if [ ! -f "$subjectlab/src/Async.cpp" ]; then
    echo 'Install dependencies with pio pkg install -e esp32-usb, or set SUBJECTLAB_DIR' >&2
    exit 1
fi
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
    "$@" -Itest/support -I"$subjectlab/src" test/test_async.cpp "$subjectlab/src/Async.cpp" \
    -o .test-build/test_async
.test-build/test_async
