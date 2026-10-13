# 当前 0.1.0 软件命名盘点与后续方案

当前展示定位按所有者授权使用 **JUST 0.1.0 正式版**。这项定位不改变签名状态，也不把有限范围的检查写成完整宿主/音频验收。本文件只提出当前软件可见名称的后续处理方案；本轮没有改代码、脚本、安装器、二进制、线上 Release 状态或任何已有附件名。

## 当前 EXE：文件身份和静态资源

在这台 Mac 上只读核对了两个已有文件，没有运行软件或使用远程 Windows：

```text
/Users/yeehuang/Documents/Codex/2026-10-12/task-2/publication-download-verify-20261013-host-fix/JUST-0.1.0-Windows-x64-preview-setup.exe
/Users/yeehuang/Documents/Codex/2026-10-12/task-2/release-assets-20261013-host-fix/JUST-0.1.0-Windows-x64-preview-setup.exe
```

两份文件均为 **4,031,050 字节**，SHA-256 均为：

```text
22946543862a919f32621aabee3c00d294d97f6e11cb8e6c4e525a92373c1040
```

它们与 [Release 附件核对记录](../releases/proposed/ASSET-CHECKS.json) 中的 host-fix 附件一致，真实文件名仍是 `JUST-0.1.0-Windows-x64-preview-setup.exe`。

本会话没有接入 REA 工具，终端也没有可用的 `rea` CLI；没有安装或修复工具。以下二进制证据来自本机 Python 对 PE 资源目录和 `VS_VERSION_INFO` 的只读解析，不是 REA Evidence，也不是实际运行或窗口验收。

| 静态字段 | 当前实际值 | 二进制位置 |
| --- | --- | --- |
| PE Machine | `0x014c`，x86 安装器启动程序 | PE header `0xD8`；不代表内含插件的架构 |
| 固定文件版本 | `0.1.0.0` | 资源类型/名称/语言 `[16, 1, 1033]`，文件偏移 `0x2EEF8`，704 字节 |
| ProductName | `JUST Audio Plugin Bundle Windows Preview` | UTF-16LE 值偏移 `0x2F0F0` |
| FileDescription | `Unsigned x64 VST3 preview installer; REAPER acceptance pending` | UTF-16LE 值偏移 `0x2EFEC` |
| ProductVersion / FileVersion | `0.1.0` / `0.1.0` | 值偏移 `0x2F168` / `0x2F08C` |
| CompanyName / LegalCopyright | `Yee Huang` / `Yee Huang` | 值偏移 `0x2EFB0` / `0x2F0BC` |

因此，只改文件在下载页显示的名称不会改掉资源里的 `Preview`。安装器欢迎页、完成页和错误窗口的文案由下述源码确定；本次没有解压并运行安装器来验证实际排版。

## 软件中可见的 preview：精确修改候选

位置来自当前未改动的 [JustPreview.nsi](../../installers/windows/JustPreview.nsi) 和 [PREVIEW-NOTICE.txt](../../installers/windows/PREVIEW-NOTICE.txt)。下面只替换指明的文字；其余安装、文件锁定、备份、恢复和隐私说明原样保留。新文案继续披露未签名、架构和验证覆盖限制；当前正式版本不再定位为等待用户验收。左栏与 PE 字段中的旧文案作为真实盘点证据保留。

| 精确位置 | 当前旧文案 | 正式 0.1.0 定位的候选新文案 |
| --- | --- | --- |
| `installers/windows/JustPreview.nsi:30`，`Name` | `JUST 0.1.0 Windows x64 Preview` | `JUST 0.1.0 Windows x64` |
| `installers/windows/JustPreview.nsi:37`，`BrandingText` | `Yee Huang \| Unsigned Windows preview` | `Yee Huang \| Unsigned Windows installer` |
| `installers/windows/JustPreview.nsi:39`，PE `ProductName` | `JUST Audio Plugin Bundle Windows Preview` | `JUST Audio Plugin Bundle Windows` |
| `installers/windows/JustPreview.nsi:43`，PE `FileDescription` | `Unsigned x64 VST3 preview installer; REAPER acceptance pending` | `Unsigned x64 VST3 installer; DAW compatibility coverage unverified` |
| `installers/windows/JustPreview.nsi:122`，欢迎页标题 | `JUST Windows x64 Preview` | `JUST 0.1.0 Windows x64` |
| `installers/windows/JustPreview.nsi:123`，欢迎页正文中的句子 | `Version 0.1.0 by Yee Huang. Unsigned preview; full host/audio acceptance is pending.` | `Version 0.1.0 by Yee Huang. Unsigned installer; full DAW compatibility and audio coverage have not been verified.` |
| `installers/windows/JustPreview.nsi:125`，公告页顶部 | `Read the preview and backup notice before installing.` | `Read the installation and backup notice before installing.` |
| `installers/windows/JustPreview.nsi:131`，完成页标题 | `Selected JUST preview plugins installed` | `Selected JUST plugins installed` |
| `installers/windows/JustPreview.nsi:132`，完成页正文中的句子 | `This unsigned preview still requires host/audio acceptance.` | `This installer is unsigned. Compatibility across all DAW hosts has not been verified.` |
| `installers/windows/JustPreview.nsi:352`，架构不符合时的错误窗 | `This preview requires native Windows x64.` | `This installer requires native Windows x64.` |
| `installers/windows/PREVIEW-NOTICE.txt:1`，公告标题 | `JUST 0.1.0 -- Windows x64 preview` | `JUST 0.1.0 -- Windows x64` |
| `scripts/windows/Build-Installer.ps1:34`，输出文件名 | `JUST-0.1.0-Windows-x64-preview-setup.exe` | 当前版本的候选新副本名 `JUST-0.1.0-Windows-x64-Setup.exe`；如所有者保留文件名警告，则 `JUST-0.1.0-Windows-x64-Setup-unsigned.exe` |
| `scripts/windows/Build-Installer.ps1:51`，构建终端输出 | `Built unsigned preview installer: $exe` | `Built unsigned installer: $exe`；这只影响下次构建日志，不会改变已有 EXE |

NSIS 的 `Name`、`BrandingText` 和版本字段在同一安装器定义中声明。卸载入口使用 `UninstPage` / `MUI_UNPAGE_INSTFILES`（`:134–136`），没有另一组独立的产品 `Preview` 字面量，也没有显式 `UninstallCaption`。按 [NSIS 默认卸载标题规则](https://nsis.sourceforge.io/Docs/Chapter4.html#uninstallcaption)，当前默认基础标题由 `Name` 生成，为 `JUST 0.1.0 Windows x64 Preview Uninstall`；方案 B 中对应变为 `JUST 0.1.0 Windows x64 Uninstall`。后续仍需检查实际卸载窗口及页面子标题；本次没有生成或运行卸载器。

卸载确认页的 `Undo this installation`（`:314`）、`Undo ALL plugins selected in this transaction`（`:320`）和 `Selected plugin identities (Wider = fake_stereo)`（`:327`）不含版本定位的 preview，保持原文。公告正文 `PREVIEW-NOTICE.txt:4–5` 的未签名、未知发布者和 SHA256 核对说明保留。第 7–10 行目前写用户 REAPER 未验收；最小显示调整方案应改为下方覆盖限制文案，不再用等待用户验收作为版本定位，其余技术与安装说明保留：

```text
Native automated checks and recorded host checks cover limited cases.
Full DAW compatibility and audio coverage have not been verified.
Try changes in a separate test project before changing your working project.
Do not replace a production project with a test project.
```

欢迎/完成页使用 `installers/windows/art-v2/JUST-Welcome-164x314.bmp`（NSI `:120`）。本次已查看这张现有图片：可见文字为 `JUST`、`Audio Plugin Bundle`、`10 PLUGINS`，没有 preview；无须修改或新生成美术。

## 插件 DLL / VST3 显示名

源码中的十款 VST3 注册名如下，均不含 preview，保持不变：

| 注册表位置 | 当前显示名 |
| --- | --- |
| `common/vst3/PluginIdentities.hpp:11` | JUST EQ |
| `common/vst3/PluginIdentities.hpp:12` | JUST Reverb |
| `common/vst3/PluginIdentities.hpp:13` | JUST Delay |
| `common/vst3/PluginIdentities.hpp:14` | JUST Tremolo |
| `common/vst3/PluginIdentities.hpp:15` | JUST Compressor |
| `common/vst3/PluginIdentities.hpp:16` | JUST Limiter |
| `common/vst3/PluginIdentities.hpp:17` | JUST Gate |
| `common/vst3/PluginIdentities.hpp:18` | JUST Flanger |
| `common/vst3/PluginIdentities.hpp:19` | JUST Wider |
| `common/vst3/PluginIdentities.hpp:20` | JUST Distortion |

证据链：`common/vst3/Factory.cpp:11,15–18` 使用 `product.name` 注册处理器和控制器，版本均为 `0.1.0`；同样的名称也记录在 `common/vst3/plugin-identities.json`。`cmake/JustPlugin.cmake:9,47–53` 生成 `Just_<slug>` 目标和 Windows 产品名，Wider 专门使用 `JUST Wider`；`cmake/JustVersionWindows.rc.in:16,18–21` 将这些值写入插件的文件说明、原文件名和产品字段。这里没有 preview 后缀。

`fake_stereo`、`Just_fake_stereo.vst3`、bundle ID、processor/controller UID 和参数 ID 均保持不变。以上是源码命名核对，没有重新读取或运行十个已发布 DLL，不能当作新一次插件验证结果。

## 保留的 preview：内部路径、历史资料和诊断截取

这些不是产品是否正式的命名开关，不列入上述显示文案修改范围：

- `installers/windows/JustPreview.nsi:286`：`$APPDATA\JUST\Windows-preview-logs\uninstall-$EntryProcessId.json`。
- `installers/windows/JustPreview.nsi:360`：`$APPDATA\JUST\Windows-preview-backups`。
- `scripts/windows/Installer-Binding.ps1:565`：`%ProgramData%` 下的 `JUST\Windows-preview-logs\installer-<invocation>.json`。
- NSI `:62,67–68,99,242,249` 的 `BindingPreview`、`InstallEntryErrorPreview`、`InstallEntryRecoveryPreview` 是诊断输出截取变量。`:257` 的 `Original entry diagnostic (preview):` / `Recovery diagnostic (preview):` 指诊断摘要，不是软件预览版本；保留原文和逻辑。
- `transaction-binding.json`、安装器/卸载器引擎名、`JUST-uninstall.exe`、原事务备份和旧日志路径全部保留。正式命名不迁移日志，不重解释历史事务。
- 源文件名 `JustPreview.nsi`、`PREVIEW-NOTICE.txt` 以及 `scripts/windows/Installer-Engine.ps1:1` 的历史注释无需为可见文案变化重命名。
- `plugins/delay/manifest.json:3`、`plugins/reverb/manifest.json:3` 的历史 implementation status，以及测试、布局类、获批美术来源中的 preview 字样不是 VST3 产品名；本轮不改这些字段或测试。

## 当前 Mac 能否重新打包

本机只读检查 `command -v pwsh`、`command -v makensis`、`command -v makensis.exe` 未找到工具，常见 `/opt/homebrew/bin` 与 `/usr/local/bin` 中也没有对应入口。没有安装任何打包工具。

既有 [Build-Installer.ps1](../../scripts/windows/Build-Installer.ps1) 第 9 行明确要求 `Win32NT` 与 64 位 PowerShell，非 Windows 进程直接拒绝；第 14–24 行查找已安装的 `makensis.exe`，不自动下载。因此**当前 Mac 不能沿用现有发布流程完成重新打包**。

方案 A 的本地同字节副本可以在这台 Mac 准备，不涉及编译；本轮未创建或上传。方案 B 若保持既有流程和最小范围，需要 Windows x64 构建环境，并在 Windows 核对新 EXE 的窗口、安装与撤销行为。改写为 Mac 交叉打包流程尚未验证，会扩大任务范围，不属于当前最小方案。本轮没有启用 UU、连接或操作 Windows。

## 方案 A：保留原字节，另提供正式名称副本

所有者可选择把原文件作为**新名称的额外附件**提供，例如 `JUST-0.1.0-Windows-x64-Setup.exe`。保留原 `JUST-0.1.0-Windows-x64-preview-setup.exe` 附件和全部旧直链；不修改它的显示名、内容或历史校验文件。

新副本仍是 **4,031,050 字节**，SHA-256 仍应为 `22946543862a919f32621aabee3c00d294d97f6e11cb8e6c4e525a92373c1040`。上传前后需重新核对实际新副本，不以文件名推断字节相同。新名称应在新的补充校验文件或补充清单里记录；旧 `SHA256SUMS.txt` 和 `release-manifest.json` 保留原样，避免覆盖原发布证据。

此方案使下载文件名符合正式 0.1.0 定位，**PE 产品名、窗口标题、欢迎/完成页和嵌入公告仍保留原 Preview 文案**。不能据此宣称安装器软件内的 preview 已去除。签名和验证范围完全沿用原包，本轮也没有创建、上传这个副本。

## 方案 B：下次专门授权后，最小调整安装器可见文案

在另一次明确授权的 Windows 安装器构建任务中，仅应用上表的产品/页面文案变化及输出命名。当前 Mac 文档整理不执行 Windows 构建，也不远程操作 Windows。

拟保留产品/插件版本 `0.1.0`、固定版本资源 `0.1.0.0`、厂商 `Yee Huang` 和全部兼容性身份；不得修改 DSP、参数、状态、插件 DLL、安装/撤销事务逻辑、日志/备份路径或美术。安装器包装输出的改变应单独记录，不用插件版本号伪装成插件功能更新。后续的 tag、附件发布位置和安装器修订编号由所有者决定。

构建后需要重新取得 EXE 的真实字节数和 SHA-256，并更新该新包自己的校验与清单；不能沿用旧值。至少核对 PE 字段、欢迎/公告/完成页、架构拒绝窗口、组件选择和实际卸载入口中的标题与限制文案，并证明插件载荷逐字节未变。安装、文件锁定拒绝、取消和恢复等相关检查仍需针对新 EXE 完成，不能把旧 EXE 的结果自动算作新包验收。完整宿主/音频验收仍按实际完成范围描述。

本轮只交付这份方案，未应用上表任何源码替换，也没有产生新 EXE。

## GitHub Release 状态仍需后续决定

归档 API 中 `v0.1.0-installers.20261013-host-fix` 的 `prerelease` 为 **true**，`draft` 为 false，发布时间为 `2026-10-13T09:53:55Z`。该历史 tag、附件名与现有软件 Preview 文案在本轮均未改变。

所有者授权当前正式 0.1.0 的展示定位后，线上 `prerelease` 和 Latest 的处理仍需作为后续 metadata 操作单独决定。本轮不直接改这两个字段，也不把正式定位解释为已经补齐签名或全部测试。
