#!/bin/zsh
# 获取固定版本的原生转发依赖。
set -eu
cd "${0:A:h:h}"
mkdir -p refrence

# 固定检出上游及其直接依赖。
dps() {
    local target="$1" url="$2" revision="$3"
    if [[ ! -d "$target/.git" ]]; then git clone --depth 1 "$url" "$target"; fi
    if [[ "$(git -C "$target" rev-parse HEAD)" != "$revision" ]]; then
        git -C "$target" fetch --depth 1 origin "$revision"
        git -C "$target" checkout --detach "$revision"
    fi
}
base=refrence/hev-socks5-tunnel
dps "$base" https://github.com/heiher/hev-socks5-tunnel.git 9b2c2320b26046b307abd6ddb5f102b2ccd1d4a4
dps "$base/src/core" https://github.com/heiher/hev-socks5-core.git ef82008f7e2058e56d76a79543a585bba4b7feef
dps "$base/third-part/hev-task-system" https://github.com/heiher/hev-task-system.git 935f3c14d2d2035d5b1d16ef0d426a6a0fd34572
dps "$base/third-part/lwip" https://github.com/heiher/lwip.git e6d5fc6cdc7c2ab379ffffd13dfdc99fb826a86c
dps "$base/third-part/yaml" https://github.com/heiher/yaml.git 94441ec8777eb319936735a58b376963d09c73d4
