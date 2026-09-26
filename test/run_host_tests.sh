#!/bin/sh
# Build and run the host tests of KNX Secure: the frames and cryptography,
# and the management access policies.
#
# Needs a C++ compiler and one PlatformIO build before it, which fetches the
# KNX stack: its portable AES stands in for mbedTLS here.
#
#   test/run_host_tests.sh
set -e

root=$(cd "$(dirname "$0")/.." && pwd)
stack=$(ls -d "$root"/.pio/libdeps/*/knx/src/knx 2>/dev/null | head -n 1)

if [ -z "$stack" ]; then
    echo "KNX stack not found - run 'pio run' once first" >&2
    exit 1
fi

out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

# ECB only: the stack's defaults would also build CBC and CTR, which the
# test does not use.
${CC:-gcc} -O1 -DECB=1 -DCBC=0 -DCTR=0 -c "$stack/aes.c" -o "$out/aes.o"

${CXX:-g++} -std=c++17 -Wall -Wextra -O1 -DKNXSEC_HOST_TEST \
    -DECB=1 -DCBC=0 -DCTR=0 \
    -I "$root/src" -I "$stack" \
    "$root/test/secure_crypto_test.cpp" \
    "$root/src/knx_secure_crypto.cpp" \
    "$root/src/knxip_secure_frames.cpp" \
    "$out/aes.o" \
    -o "$out/secure_crypto_test"

${CXX:-g++} -std=c++17 -Wall -Wextra -O1 \
    -I "$root/src" \
    "$root/test/access_policy_test.cpp" \
    "$root/src/knx_access_policy.cpp" \
    -o "$out/access_policy_test"

"$out/secure_crypto_test"
echo
"$out/access_policy_test"
