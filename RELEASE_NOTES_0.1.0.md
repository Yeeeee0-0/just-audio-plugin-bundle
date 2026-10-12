# JUST 0.1.0

The recommended [2026-10-12 installer prerelease](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/tag/v0.1.0-installers.20261012) contains the updated Windows x64 preview Setup and the unchanged macOS Apple silicon installer from `v0.1.0`. Product version remains **0.1.0**. Old releases and assets are retained.

## Changes and source

The Windows editor update stabilizes open menus and normal/slow selections, avoids redundant placement/text updates, and buffers button painting. Approved installer icons and welcome/finish artwork are included. This final UI change preserves DSP, parameter/UID/state contracts, layout, the 33 ms refresh interval and the established installer transaction logic.

The Windows source is recorded by the new release tag. The macOS package remains based on [`f2a710510415395e13e548400143604c455df8fe`](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/tree/f2a710510415395e13e548400143604c455df8fe), with frozen provenance `b99abd3be4ad89b21e3a0c3caf1f12904503f168`. It was not rebuilt for this publication.

The original 0.1.0 baseline reports version 0.1.0 across ten effects, removes EQ's main-graph explanatory tooltip and defaults fresh/legacy-fallback EQ display ranges to 120 dB. Explicit current-format 60/90/120 dB choices are preserved. Wider keeps its compatible `fake_stereo` identity.

## Verification and limits

The final Windows set passed ten bounded native UI scopes and eight directed REAPER checks (six Flanger menu checks and Delay stopped/playing phases). Three bounded new-Setup cases covered simulated installation, occupied-file refusal and uninstall content restoration. Earlier 51 installation and 40 uninstallation cases were reused for unchanged logic. Setup/uninstaller seven-frame icons, welcome/finish artwork and default ten-component selection were checked. Physical Cancel was not retested for this package. Uninstall restored original contents and directories; stable metadata did not all match.

The macOS installer retains its previous ten-choice/14-CLI-scenario and native selection GUI record. That GUI check stopped at the pre-install confirmation page. No new macOS installation, rebuild or host/audio matrix was run for publication. These records do not establish compatibility with every DAW or system.

Windows Setup and plugins are unsigned. The macOS PKG is unsigned and not notarized; its arm64 plugins carry development ad-hoc signatures, not Developer ID signatures. Intel macOS and Universal binaries are not included. The macOS 11.0 deployment target alone is not runtime certification. Existing DSP limitations remain described in the [README](README.md) and module documentation.

See [exact asset names, sizes and SHA-256 values](docs/releases/0.1.0-installers-20261012.md), [Windows installation/removal](docs/windows/START-HERE-zh.md) and [macOS installation/removal](INSTALLING.md). MIT and third-party notices are unchanged.

## 中文

本次推荐发布保持版本 **0.1.0**：更新 Windows x64 预览安装包，同时提供原 macOS Apple silicon arm64 安装包。Windows 修复菜单秒关、正常/慢速选择回退及按钮闪烁，并包含批准的安装器图标与欢迎/完成页；DSP、参数、UID、状态、布局、刷新间隔和既有安装事务逻辑保持不变。macOS 文件和原源码来源均未改变，两端没有共同重编。

Windows 十款有限范围原生 UI 检查、REAPER 8 项定向检查及 3 项新安装包检查通过；未变的安装/卸载逻辑沿用旧 51/40 项证据。本包未重测物理点击 Cancel；卸载恢复内容和目录，但文件元数据并非全部一致。macOS 原 Installer GUI 检查停在安装前确认页。本次未扩大完整音频/宿主矩阵，不能据此保证所有 DAW。Windows 未签名，macOS PKG 未签名、未公证，插件为 arm64 开发 ad-hoc 签名，不含 Intel/Universal 二进制。具体下载、摘要及安装/卸载方法见上方链接。
