#!/bin/zsh
# 在独立构建目录编译宿主核心并执行隔离流量检查。
set -eu
cd "${0:A:h:h}"
dep="$PWD/build/native/hev-socks5-tunnel"
if [[ ! -f "$dep/Makefile" ]]; then
    mkdir -p "$dep"
    git -C refrence/hev-socks5-tunnel archive HEAD | tar -x -C "$dep"
    for item in src/core third-part/hev-task-system third-part/lwip third-part/yaml; do
        mkdir -p "$dep/$item"
        git -C "refrence/hev-socks5-tunnel/$item" archive HEAD | tar -x -C "$dep/$item"
    done
fi
make -C "$dep" -j 8 SHELL=/bin/zsh PP=clang CC=clang static > build/native-check.log 2>&1
xcrun --sdk macosx clang -Wall -Werror -fobjc-arc -c src/helper/dns.m -o build/DNS-host.o
xcrun --sdk macosx clang -Wall -Werror -I "$dep/src" \
    tests/forward.c src/common/utun_sys.c src/common/probe_protocol.c \
    "$dep/bin/libhev-socks5-tunnel.a" "$dep/third-part/lwip/bin/liblwip.a" \
    "$dep/third-part/yaml/bin/libyaml.a" "$dep/third-part/hev-task-system/bin/libhev-task-system.a" \
    build/DNS-host.o -framework Foundation -o build/check-forward
python3 tests/forward.py
