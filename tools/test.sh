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
