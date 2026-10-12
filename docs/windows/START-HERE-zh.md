# JUST 0.1.0 Windows x64：下载、安装与撤销

推荐下载 [v0.1.0-installers.20261012](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/tag/v0.1.0-installers.20261012) 中的 [JUST-0.1.0-Windows-x64-preview-UI-fix-setup.exe](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/download/v0.1.0-installers.20261012/JUST-0.1.0-Windows-x64-preview-UI-fix-setup.exe)，大小 **4,013,818 字节**，SHA-256：

```text
4fd10096f4135fc1520875eb78eda7e5ff82347bcdd8f85b8bc52d23712d8bab
```

这是未签名 Windows x64 VST3 预览。安装器启动程序为 x86，内含十款 AMD64/PE32+ 插件；不提供 Windows ARM64 插件。系统可能显示未知发布者。请核对摘要并阅读[本次发布记录](../releases/0.1.0-installers-20261012.md)。旧编号 CI 预发布仍保留，不要求额外下载旧 Source/Evidence/Portable ZIP 才能使用本次 Setup。

## 安装

1. 保存工程，正常关闭 REAPER 和插件宿主。
2. 运行 Setup；默认目标是系统 64 位 Common Files 下的 `VST3` 目录，机器级安装需管理员提升。
3. 选择 EQ、Reverb、Delay、Tremolo、Compressor、Limiter、Gate、Flanger、Wider、Distortion 中需要的项目，默认十款全选。Wider 的文件身份仍是 `Just_fake_stereo.vst3`。取消勾选只跳过该项，不卸载已有副本。
4. 完成后保存实际报告和备份位置，再打开宿主并扫描 VST3。

安装器验证完整候选，备份并替换选中的 JUST bundle；不改用户预设、REAPER 设置、许可证或工程，也不终止进程。遇到宿主仍运行或文件占用时先正常关闭相关宿主，再根据实际报告处理。默认报告位于 `%ProgramData%\JUST\Windows-preview-logs`，事务备份位于 `%ProgramData%\JUST\Windows-preview-backups`。

## 卸载／撤销该次安装

运行安装报告记录的 `JUST-uninstall.exe`，保留它与相邻 `transaction-binding.json` 及备份的原目录。确认页列出该次目标及选择的插件。撤销针对该事务全部选中项：首次安装的 bundle 移除，被替换的 bundle 恢复；备份继续保留。它会拒绝移动入口、绑定不一致或未知目录/文件变化。不要删除整个 VST3 目录或其他厂商插件。详见[安装器技术说明](../../installers/windows/README.md)。

## 本次检查范围

最终十款 DLL 通过有限范围原生 UI 检查；REAPER 通过 Flanger 六项菜单检查和 Delay 停止/播放两项检查。新 Setup 通过模拟安装、占用拒绝及卸载内容恢复检查，图标/欢迎与完成页/默认十项全选已核对。未变逻辑沿用旧 51 项安装、40 例卸载证据。本包未重测物理点击 Cancel；内容和目录恢复通过，但文件元数据并非全部一致。完整音频矩阵、所有 DAW/系统和所有用户工程兼容性没有因此获保证。

源码对应本次 release tag；构建步骤见 [BUILDING.md](../../BUILDING.md)。`ACCEPTANCE-zh.md`、`KNOWN-DIFFERENCES-zh.md` 及 `WINDOWS-CODEX-PROMPT-zh.txt` 保留历史模板/差异记录，不是本次全部检查结果。私人宿主设置、授权、工程和原始诊断备份仅留本地，不提交到 Git 或发布资产。

## English quick start

Download the Setup linked above and verify its SHA-256. Save projects and close REAPER/plugin hosts normally. Choose components (all ten are selected by default), install to the intended x64 VST3 directory, keep the report/transaction backup, then rescan in your host. Run the exact recorded `JUST-uninstall.exe` in its original transaction directory to undo that installation; it restores replaced bundles and removes first-time additions. User presets and private host data are outside the installer's scope. This is an unsigned preview with bounded UI/REAPER evidence, not a full host/audio compatibility guarantee; see the [release record](../releases/0.1.0-installers-20261012.md).
