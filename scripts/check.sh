#!/bin/zsh
# 检查参数、路由编码和内核路由查询，不改变系统网络。
set -eu
cd "${0:A:h:h}"
mkdir -p build
xcrun --sdk macosx clang -DTP_CHECK -Wall -Werror src/helper/proxy.c -o build/check
./build/check
xcrun --sdk macosx clang -Wall -Werror -I refrence/hev-socks5-tunnel/src \
    tests/check.c tests/stubs.c src/common/probe_protocol.c -o build/check-config
./build/check-config
xcrun --sdk macosx clang -Wall -Werror -I refrence/hev-socks5-tunnel/src \
    tests/kernel.c tests/stubs.c src/common/utun_sys.c src/common/probe_protocol.c -o build/check-kernel
xcrun --sdk macosx clang -Wall -Werror -fobjc-arc tests/dns.m \
    -framework Foundation -framework SystemConfiguration -o build/check-dns
