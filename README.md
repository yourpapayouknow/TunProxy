<div align="center">

<img src="assets/readme-badge.png" alt="TunProxy项目横幅" />

# TunProxy

*让TrollStore设备通过已有尾网连接使用远端代理。*

<img src="https://img.shields.io/badge/平台-iOS%2015.0%2B%20%7C%20iPadOS%2015.0%2B-blue.svg?style=flat-square" alt="平台：iOS及iPadOS15.0以上" />
<img src="https://img.shields.io/badge/语言-Swift-orange.svg?style=flat-square" alt="语言：Swift" />
<img src="https://img.shields.io/badge/界面-SwiftUI-green.svg?style=flat-square" alt="界面：SwiftUI" />

</div>

---

TunProxy面向已安装TrollStore、使用Tunailscale连接尾网的iPhone与iPad用户，将设备流量转发到尾网内的SOCKS5代理。填写远端地址与端口后即可一键启停，无需设置Wi-Fi HTTP代理，也不占用系统VPN配置。

## 快速开始

将以下指令发给具备终端执行能力的编程助手：

```text
帮我安装这个仓库：https://github.com/yourpapayouknow/TunProxy。检查TrollStore支持情况，在Mac构建并通过TrollStore安装，配置已有尾网内的SOCKS5代理；必要授权由我本人确认。
```

---

## 传统开始

设备须运行iOS或iPadOS15.0以上、处于TrollStore支持的系统版本，并已安装TrollStore和[Tunailscale](https://github.com/yourpapayouknow/Tunailscale)。远端代理须支持SOCKS5及UDP转发，并允许尾网设备访问其监听端口。

源码构建需要macOS、Xcode及iOS SDK、Xcode命令行工具、Git、Make和Homebrew。构建脚本固定转发依赖的源码提交，首次运行需要联网。

```zsh
brew install ldid
git clone https://github.com/yourpapayouknow/TunProxy.git
cd TunProxy
/bin/zsh build.sh
```

将生成的`dist/TunProxy.tipa`传到设备，通过TrollStore打开并安装。

---

## 功能

| 功能 | 用途 |
|---|---|
| 全局流量转发 | 通过独立utun将TCP与UDP流量转发到远端SOCKS5代理 |
| 前台开关 | 一键启停后台转发并显示实际网络接口 |
| 代理设置 | 保存尾网内代理地址与端口，支持IPv4与IPv6地址 |
| 临时DNS | 开启时接入公共DNS，停止时撤销本应用的DNS服务 |

---

## 使用

1. 在远端开启SOCKS5代理及UDP转发，确认代理监听地址允许尾网连接；在手机的Tunailscale中连接同一尾网。
2. 打开TunProxy，进入右上角设置，填写远端的Tailscale地址和代理端口；使用Clash混合端口时可填写实际配置的7897等端口。
3. 点击“开启代理”，确认显示“已开启”和实际utun接口，再打开需要联网的应用。
4. 关闭TunProxy代理后再断开Tunailscale；更新或卸载TunProxy前，先点击“关闭代理”停止后台转发。
