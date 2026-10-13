# JUST 0.1.0 正式版 Windows x64：下载、安装与撤销

推荐下载 [JUST 0.1.0 Windows 安装器宿主修复](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/tag/v0.1.0-installers.20261013-host-fix) 中的 [JUST 0.1.0 Windows x64 Setup](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/download/v0.1.0-installers.20261013-host-fix/JUST-0.1.0-Windows-x64-preview-setup.exe)，大小 **4,031,050 字节**，SHA-256：

```text
22946543862a919f32621aabee3c00d294d97f6e11cb8e6c4e525a92373c1040
```

这是 JUST **0.1.0 正式版**的未签名 Windows x64 VST3 安装包。完整 DAW 兼容性与音频覆盖尚未验证。安装器启动程序为 x86，内含十款 AMD64/PE32+ 插件，不含 Windows ARM64 插件。详见[本次发布记录](../releases/0.1.0-installers-20261013-host-fix.md)；旧安装包和发布历史保留。

## 安装

1. 保存工程。宿主可保持运行，但所选插件目录须允许安全移动；若插件编辑窗口锁住文件，先关闭该插件编辑窗口。
2. 运行 Setup；默认目标是系统 64 位 Common Files 下的 `VST3` 目录，机器级安装需管理员提升。
3. 十款默认全选，可分别取消勾选。Wider 保留文件名 `Just_fake_stereo.vst3`。取消勾选只跳过，不卸载已有副本。
4. 保存实际报告和事务备份。完成后正常重新打开宿主或重新加载插件，再扫描 VST3 使用新副本。

安装器只替换选中的 JUST bundle。同卷事务保留旧目录，再换入新副本；未选中的第三方链接目录不遍历、不修改，选中的 JUST bundle 若为链接仍拒绝。文件硬锁会安全拒绝并保留原内容，不强制关闭宿主。用户预设、宿主设置、许可证和工程不在操作范围内。默认报告在 `%ProgramData%\JUST\Windows-preview-logs`，备份在 `%ProgramData%\JUST\Windows-preview-backups`。

## 卸载／撤销

运行安装报告记录的 `JUST-uninstall.exe`，保留它与相邻 `transaction-binding.json` 及备份的原目录。确认后撤销该事务全部选中项：移除首次安装项，恢复替换前副本，保留备份。绑定不一致、未知变更或文件硬锁会拒绝；不删除整个 VST3 目录。参见[安装器说明](../../installers/windows/README.md)。

## 验证范围

最终 EXE 的八项定向检查完成，覆盖真实取消、运行中安装／还原、编辑窗口锁定拒绝与正常重开；复用旧包七项未受影响证据，未重跑完整 DSP/UI 矩阵。实际还原的 153 文件、71 目录及身份记录一致；顶层目录修改时间会变化。检查器原误判记录保留，以原始记录核清。DLL、UI、DSP、版本与美术未变。此结果不承诺所有 DAW 热替换或音频无间断。

## English quick start

Download the Setup above and verify SHA-256. Save your project. Hosts may remain running if selected bundles can be moved safely; close a locking plugin editor and retry if installation refuses. Select components (all ten by default), keep the report and transaction backup, then normally reopen the host or reload plugins. Undo using the exact original transaction's uninstaller. Private projects, settings and licenses are outside the installer's scope. Eight directed installer/REAPER branches were checked; full DAW compatibility and audio coverage have not been verified. See the [release record](../releases/0.1.0-installers-20261013-host-fix.md).
