#!/bin/zsh
# 编译原生核心和独立前台并打包 TrollStore 安装包。
set -eu
cd "${0:A:h}"
root="$PWD"
[[ ! -L build && ! -L dist ]] || { print -u2 'Refusing linked output directory'; exit 1; }
mkdir -p build dist
app=build/Payload/TunProxy.app
if [[ -d "$app" || -e dist/TunProxy.tipa ]]; then
    backup="$HOME/backup/$(date +%Y-%m-%d)_tunproxy/build-$(date -u +%Y%m%dT%H%M%SZ)"
    mkdir -p "$backup"
    [[ ! -d "$app" ]] || cp -R "$app" "$backup/"
    [[ ! -e dist/TunProxy.tipa ]] || cp dist/TunProxy.tipa "$backup/"
fi
mkdir -p "$app"
/bin/zsh scripts/deps.sh
dep="$root/refrence/hev-socks5-tunnel"
sdk="$(xcrun --sdk iphoneos --show-sdk-path)"
cc="$(xcrun --sdk iphoneos --find clang)"
flags="-target arm64-apple-ios15.0 -isysroot $sdk"
make -C "$dep" -j 8 SHELL=/bin/zsh PP="$cc" CC="$cc" CFLAGS="$flags" LFLAGS="$flags" static
xcrun --sdk iphoneos clang -target arm64-apple-ios15.0 -isysroot "$sdk" -O2 -Wall -fobjc-arc \
    -c src/helper/dns.m -o build/DNS.o
xcrun --sdk iphoneos clang -target arm64-apple-ios15.0 -isysroot "$sdk" -O2 -Wall \
    -I "$dep/src" src/helper/proxy.c src/common/utun_sys.c src/common/probe_protocol.c \
    "$dep/bin/libhev-socks5-tunnel.a" "$dep/third-part/lwip/bin/liblwip.a" \
    "$dep/third-part/yaml/bin/libyaml.a" "$dep/third-part/hev-task-system/bin/libhev-task-system.a" \
    build/DNS.o -framework Foundation -Wl,-dead_strip -o "$app/tunproxyd"
xcrun --sdk iphoneos clang -target arm64-apple-ios15.0 -isysroot "$sdk" -O2 -Wall -fobjc-arc \
    -c src/app/TPSpawn.m -o build/TPSpawn.o
xcrun --sdk iphoneos swiftc -parse-as-library -target arm64-apple-ios15.0 -sdk "$sdk" -O \
    -import-objc-header src/app/TPSpawn.h build/TPSpawn.o src/ui/TunProxy.swift \
    -framework UIKit -framework SwiftUI -framework Foundation -o "$app/TunProxy"
cp src/app/Info.plist "$app/Info.plist"
xcrun swift scripts/icons.swift
cp src/app/icons/AppIcon*.png "$app/"
mkdir -p "$app/licenses"
cp THIRD_PARTY_NOTICES.md "$app/"
for item in "$dep/LICENSE" "$dep/src/core/LICENSE" "$dep/third-part/hev-task-system/LICENSE" "$dep/third-part/lwip/LICENSE" "$dep/third-part/yaml/LICENSE"; do
    [[ ! -f "$item" ]] || cp "$item" "$app/licenses/$(basename "${item:h}")-$(basename "$item")"
done
chmod 755 "$app/TunProxy" "$app/tunproxyd"
/opt/homebrew/bin/ldid -Sentitlements/helper.plist "$app/tunproxyd"
/opt/homebrew/bin/ldid -Sentitlements/app_b.plist "$app/TunProxy"
codesign -s - --force --entitlements entitlements/helper.plist --generate-entitlement-der "$app/tunproxyd"
codesign -s - --force --deep --preserve-metadata=entitlements "$app"
codesign --verify --deep --strict "$app"
codesign --verify --strict "$app/tunproxyd"
archive="$root/dist/TunProxy-$(date -u +%Y%m%dT%H%M%SZ).tipa"
(cd build && /usr/bin/zip -qr -9 "$archive" Payload)
cp "$archive" dist/TunProxy.tipa
shasum -a 256 dist/TunProxy.tipa > dist/SHA256SUMS.txt
cat dist/SHA256SUMS.txt
