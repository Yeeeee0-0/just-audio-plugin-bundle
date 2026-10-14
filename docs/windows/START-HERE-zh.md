# JUST 0.1.0 正式版 Windows x64：下载、安装与撤销

推荐下载 [JUST 0.1.0 安装器名称／包装修订（2026-10-14）](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/tag/v0.1.0-installers.20261014-formal-name) 中的 [JUST 0.1.0 Windows x64 Setup](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/download/v0.1.0-installers.20261014-formal-name/JUST-0.1.0-Windows-x64-Setup.exe)，大小 **4,028,911 字节**，SHA-256：

```text
2ecf9337208a2d44df026fee2f7819b3e50008cc0e8b77a790d4f950ab662ca2
```

这是 JUST **0.1.0 正式版**的未签名 Windows x64 VST3 安装包，包装修订日期为 **2026-10-14**。本次只更新安装器名称、文案和输出文件名；产品版本仍为 0.1.0，十个插件 DLL 和 151 项载荷文件与上一版宿主修复包完全一致。完整 DAW 兼容性与音频覆盖尚未验证。安装器启动程序为 x86，内含十款 AMD64/PE32+ 插件，不含 Windows ARM64 插件。详见[本次发布记录](../releases/0.1.0-installers-20261014-formal-name.md)；旧安装包和发布历史保留。

## 安装

1. 保存工程。宿主可保持运行，但所选插件目录须允许安全移动；若插件编辑窗口锁住文件，先关闭该插件编辑窗口。
2. 运行 Setup；默认目标是系统 64 位 Common Files 下的 `VST3` 目录，机器级安装需管理员提升。
3. 十款默认全选，可分别取消勾选。Wider 保留文件名 `Just_fake_stereo.vst3`。取消勾选只跳过，不卸载已有副本。
4. 保存实际报告和事务备份。完成后正常重新打开宿主或重新加载插件，再扫描 VST3 使用新副本。

安装器只替换选中的 JUST bundle。同卷事务保留旧目录，再换入新副本；未选中的第三方链接目录不遍历、不修改，选中的 JUST bundle 若为链接仍拒绝。文件硬锁会安全拒绝并保留原内容，不强制关闭宿主。用户预设、宿主设置、许可证和工程不在操作范围内。默认报告在 `%ProgramData%\JUST\Windows-preview-logs`，备份在 `%ProgramData%\JUST\Windows-preview-backups`。

## 卸载／撤销

运行安装报告记录的 `JUST-uninstall.exe`，保留它与相邻 `transaction-binding.json` 及备份的原目录。确认后撤销该事务全部选中项：移除首次安装项，恢复替换前副本，保留备份。绑定不一致、未知变更或文件硬锁会拒绝；不删除整个 VST3 目录。参见[安装器说明](../../installers/windows/README.md)。

## 验证范围

本次新 EXE 通过 **51 项安装检查**，覆盖默认十组件完整 SHA、只选 EQ、备份／恢复、未选组件与第三方/private 测试哨兵保持、注入失败回滚及未选载荷篡改拒绝。实际查看欢迎页正式名称与默认十项选择，并通过 Esc → Yes 正常取消；隔离夹具验证锁定文件拒绝与回滚；实际生成的卸载器通过正常 GUI 取消及有界静默恢复。

本轮未重跑 DSP、真实 DAW/REAPER 音频检查或完整卸载套件，未观察 Setup 安装完成页。插件 DLL、插件 UI、DSP、产品版本与美术未变。[旧宿主修复记录](../releases/0.1.0-installers-20261013-host-fix.md) 保留此前有限 REAPER 检查和恢复证据，不代表对本次 EXE 的重测。此结果不承诺所有 DAW 热替换或音频无间断。

## English quick start

Download the Setup above and verify SHA-256. Save your project. Hosts may remain running if selected bundles can be moved safely; close a locking plugin editor and retry if installation refuses. Select components (all ten by default), keep the report and transaction backup, then normally reopen the host or reload plugins. Undo using the exact original transaction's uninstaller. Private projects, settings and licenses are outside the installer's scope. This naming/packaging revision keeps product version 0.1.0 and the same ten DLLs and 151 payload files. The new EXE passed 51 installer checks, with observed ten-component selection and normal GUI cancel, locked-file refusal/rollback, and actual-uninstaller cancel/restore checks. DSP tests, real DAW/REAPER audio checks and the full uninstall suite were not rerun; the Setup completion page was not observed. Full DAW compatibility and audio coverage have not been verified. See the [release record](../releases/0.1.0-installers-20261014-formal-name.md).
