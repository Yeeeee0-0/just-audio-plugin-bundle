# Owner review — GitHub presentation

本文件汇总策划书 §9 的全部待确认项，并提供 §8 要求的所有者操作步骤。下列设置和草稿尚未启用；本轮不修改线上 About、Topics、社交预览、Release 标题/正文或任何 Release 状态。

当前所有者已明确：十款插件是正式 **0.1.0**。当前产品名称、About 拟稿与下载显示文案据此更新；未签名、架构、DSP 和已记录的测试覆盖限制仍保留。软件/附件的实际遗留名称与后续最小方案见 [CURRENT-NAMING.md](CURRENT-NAMING.md)。线上设置与 Release 状态本轮仍未改变。

## 12 项待确认 / Decisions from §9

| # | 所有者需要决定 | 当前交付与边界 |
| --- | --- | --- |
| 1 | README 是否增加项目起因？如需要，请所有者提供一两句原文。 | README 留有 `TODO(owner)` 注释；没有编写作者动机。 |
| 2 | README 头图用图标总览，还是一行十个 40 px 小图标？ | 原总览图为 **1536 × 1024**；[HEADER-PREVIEW.md](HEADER-PREVIEW.md) 提供两种候选，首页头图暂未启用。只使用仓库现有图片。 |
| 3 | About 用中英合一，还是纯英文？ | 两版完整文案见下文；线上描述保持原状。 |
| 4 | Latest 选方案 A，还是方案 B？ | A：保留 `v0.1.0` 为 Latest，在其正文提示 Windows 下载入口。B：另提供正式命名的发布/附件，并由所有者选择是否设为 Latest；这不增加新的测试通过声明。本轮不改预发布或 Latest 状态。 |
| 5 | 未来 tag 是否采用 §6.1 的规范？ | [NAMING.md](../releases/NAMING.md) 记录建议。现有 tag 均保持不变；`pkg.1`、`pkg.2` 等对应编号仅作示意，正式编号由所有者确定。 |
| 6 | BUILDING 的插件源码与当前安装器源码各对应哪个 tag？ | 原文与候选替换文字、依据见下文。`BUILDING.md` 本轮保留原文，待确认后再改。 |
| 7 | `WINDOWS-CODEX-PROMPT-zh.txt` 移到 `docs/dev/`、删除，还是保留？ | 本轮保留根目录文件；未移动、未删除。 |
| 8 | 社交预览图由谁制作？ | 现有总览图可作素材，目标 1280 × 640，拟标题 `JUST — 10 VST3 effects`。本轮没有制作或上传新图。 |
| 9 | 是否需要 T7 CHANGELOG 和 T8 Issue 模板？ | [CHANGELOG 草稿](drafts/CHANGELOG.md)、[bug_report.yml 草稿](drafts/issue-template/bug_report.yml)、[config.yml 草稿](drafts/issue-template/config.yml) 可审阅；根目录 CHANGELOG 和 `.github/ISSUE_TEMPLATE` 均未启用。 |
| 10 | 下一次发版是否采用 §6.6 文件名规范？是否同时修改打包脚本？ | 只读定位和后续修改建议见下文。当前脚本和已发布文件名均未修改。 |
| 11 | 未来文件名是否保留 `-unsigned`？ | 两种形式：`JUST-0.2.0-Windows-x64-Setup.exe` 或 `JUST-0.2.0-Windows-x64-Setup-unsigned.exe`。即使文件名不含警告，Release 状态栏和清单仍须如实记录签名状态。尚未选择。 |
| 12 | preview.13 报告的 REAPER 7.41 COMCTL32 崩溃是否已确认修复？ | preview.17 原正文仍明确写实机复验待完成；当前材料不足以宣称已确认修复。旧 preview 的拟提示只说“这是早期 Windows 测试版”，不写“问题已在后续版本中修复”。 |

Website 字段与 Packages / Deployments 侧栏显示也保留待所有者决定：Website 可留空或使用当前推荐 Release 页；只有确认相关功能没有使用后，才取消对应侧栏显示。

## T2：About、Topics 与网页操作

### About 两种文案

中英合一（按策划书 §5.1）：

```text
Ten free open-source VST3 effects (EQ, reverb, delay, compressor, limiter, gate, flanger, tremolo, wider, distortion) in C++17. macOS Apple silicon + Windows x64. Version 0.1.0. MIT. | 十款开源 VST3 效果器
```

纯英文：

```text
Ten free open-source VST3 effects (EQ, reverb, delay, compressor, limiter, gate, flanger, tremolo, wider, distortion) in C++17. macOS Apple silicon + Windows x64. Version 0.1.0. MIT.
```

中英合一 198 字符，纯英文 182 字符，两版均在 350 字符上限内。两版均按所有者最新要求使用正式 0.1.0 定位，语言选择仍待确认。

### 20 个 Topics

```text
vst3 vst3-plugin audio-plugin audio-effects dsp cpp17 music-production
equalizer reverb delay compressor limiter noise-gate flanger tremolo
distortion stereo-widener macos apple-silicon windows
```

### 所有者确认后的操作步骤

1. 打开 [仓库首页](https://github.com/Yeeeee0-0/just-audio-plugin-bundle)，在 About 旁的齿轮中粘贴选定描述，添加以上 **20** 个 Topics。
2. 在同一设置区确认 Website：留空，或填入 [当前推荐 Windows Release](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/tag/v0.1.0-installers.20261013-host-fix)。如 Packages / Deployments 没有使用，再关闭其首页侧栏显示；保留实际在用的项。
3. 社交预览由选定负责人用 [现有总览图](../reference/icons/JUST-icon-board-original.png) 准备 1280 × 640 图片，居中放置于深色底并配标题 `JUST — 10 VST3 effects`。不得生成或引入新美术；由所有者审阅后到 Settings → General → Social preview 上传。
4. 保存后在首页检查描述、Topics、Website 和侧栏，在链接预览中检查社交预览。以上均是后续网页操作步骤，本轮未执行。

## T4：线上 Release 操作交接

原标题和完整原正文保存于 [archive/](../releases/archive/)。拟稿保存于 [proposed/](../releases/proposed/)，每个 tag 对应 `<tag>.title.txt` 和 `<tag>.body.md`；[Release 审阅记录](../releases/PRESENTATION-REVIEW.md) 提供逐文件链接、附件核对结果和备用 CLI 命令。先核对 PR 中的逐附件记录，再由所有者分别打开六个 Release 的编辑页：复制 `.title.txt` 全文到标题栏，复制 `.body.md` 全文到正文栏。

执行前重新读取线上标题、正文、状态和附件列表；如相较归档发生变化，先重新归档和核对，避免覆盖后续发布内容。

- `v0.1.0-installers.20261013-host-fix`：用按正式 0.1.0 定位更新的附录 A.1 拟正文；其 macOS 下载继续链接原 `20261012` 附件。
- `v0.1.0-installers.20261012`：用附录 A.2 经核对的拟正文，明确仅本页 Windows 包被取代，macOS 原包仍有效。
- `v0.1.0`：正文候选见附录 A.3。涉及 Latest 的处理仍需先确认第 4 项。
- `v0.1.0-windows-preview.17`、`.13`、`.12`：只在顶部插入“已取代”提示，随后完整保留原正文。按第 12 项，提示暂不声称 COMCTL32 崩溃已确认修复。

保存时保持原 tag、预发布状态、Latest 状态和全部附件不变。不要重命名、增删或重新上传附件。逐项检查下载链接，以及签名、架构、DSP 和已记录的测试覆盖限制仍然存在。若拟稿中的附件名、字节或摘要与线上不一致，暂停该项并交由所有者核清；不自行改数字。

## T5：BUILDING 源码说明候选（待第 6 项确认）

### 当前原文

`BUILDING.md` 开头两段为：

```text
For the Windows package in `v0.1.0-installers.20261012`, use that tag's source. The unchanged macOS arm64 package uses original public source commit `f2a710510415395e13e548400143604c455df8fe` (`v0.1.0`), derived from frozen snapshot `b99abd3be4ad89b21e3a0c3caf1f12904503f168`. The snapshot hash is provenance, not a public checkout ref. The packages were not rebuilt together. See [the release record](docs/releases/0.1.0-installers-20261012.md) for their distinct verification boundaries.

中文：重建本次 Windows 包请使用 `v0.1.0-installers.20261012` 的源码；macOS 原安装包对应上面的原公开提交，文件保持不变。锁定依赖和工具链会影响输出，源码一致不保证不同机器构建出的文件摘要一致。Intel macOS、Universal 和旧系统运行兼容性未由本发布验证。
```

### 拟替换文字

以下是供所有者确认的候选，尚未写回 `BUILDING.md`：

```text
The Windows plugin DLL set remains the set released with `v0.1.0-installers.20261012`; use that tag for its plugin source. The currently recommended Windows installer is in `v0.1.0-installers.20261013-host-fix`, which contains the matching installer source; that release did not change or rebuild the ten plugin DLLs. The unchanged macOS arm64 package uses original public source commit `f2a710510415395e13e548400143604c455df8fe` (`v0.1.0`), derived from frozen snapshot `b99abd3be4ad89b21e3a0c3caf1f12904503f168`. The snapshot hash is provenance, not a public checkout ref. The packages were not rebuilt together. See the [Windows installer update record](docs/releases/0.1.0-installers-20261013-host-fix.md) and [earlier package record](docs/releases/0.1.0-installers-20261012.md) for their distinct verification boundaries.

中文：Windows 插件 DLL 沿用 `v0.1.0-installers.20261012` 发布的版本，其插件源码使用该 tag；当前推荐 Windows 安装器对应 `v0.1.0-installers.20261013-host-fix` 的安装器源码，这次没有修改或重新编译十个插件 DLL。macOS 原安装包对应上面的原公开提交，文件保持不变。锁定依赖和工具链会影响输出，源码一致不保证不同机器构建出的文件摘要一致。Intel macOS、Universal 和旧系统运行兼容性未由本发布验证。
```

候选依据：

- [20261012 记录](../releases/0.1.0-installers-20261012.md#source-and-changes) 说明该 tag 记录最终 Windows 源码和安装器资源，macOS 对应原公开提交。
- [host-fix 记录](../releases/0.1.0-installers-20261013-host-fix.md) 和其原 Release 正文说明只改 Windows 安装器，十个 DLL 不变；原 Release 写明 `Matching Windows source is in this tag`。
- “用 20261012 tag 重建插件、用 host-fix tag 重建当前安装器”是供确认的来源分拆说法，不以本次文档整理替代二进制复现或源码归属确认。

## §6.6：文件名生成位置（只读定位）

下列行号以本次审阅基线为准。只定位和提出下一次发布的方案；未执行脚本，也未改脚本、打包输入、bundle slug 或兼容性 ID。

| 位置 | 当前行为 | 下一次发版待确认的改法 |
| --- | --- | --- |
| `scripts/windows/Build-Installer.ps1:34` | 固定生成 `JUST-0.1.0-Windows-x64-preview-setup.exe`；`:37` 用 `OUTPUT_FILE` 交给 NSIS；`:42–43` 从实际文件生成摘要。 | 第 10/11 项确认后，以单独的发布版本字段生成 `JUST-<release-version>-Windows-x64-Setup[-unsigned].exe`，插件版本仍按真实版本记录。 |
| `installers/windows/JustPreview.nsi:12–13,31` | 要求 `OUTPUT_FILE`，直接输出该路径；`:38–44` 产品版本 `0.1.0`、厂商 `Yee Huang` 和未签名说明。 | 文件名通过上游传入，无须为改文件名变更产品版本、厂商或兼容性身份。 |
| `scripts/windows/Test-Installer.ps1:12` | 默认使用固定的旧 Setup 路径。 | 若生成文件名变化，同步改默认入口，或让发布流程明确传入新文件路径；重新验证测试引用正确文件。 |
| `scripts/windows/prepare-preview-release.py:70,80–84` | 读取固定 Setup；`suffix` 拼为 `0.1.0-Windows-x64-preview.<run-number>`，再生成 Setup / Portable / Source / Evidence。 | 同步采用经确认的发布版本并使 tag 与附件一一对应；手动包类型按 §6.6 采用 `VST3.zip`，源码包为 `JUST-<release-version>-Source.zip`。Evidence 命名在 §6.6 未规定，另由所有者决定。 |
| `scripts/windows/prepare-preview-release.py:105` | 附件清单读取实际文件名。 | 保持从实际输出读取文件名与摘要，检查生成正文及下游引用同步。历史附件引用不改。 |
| `.github/workflows/windows-preview.yml:56,68–69,82` | 传入 workflow run number，并上传 `build/preview-release/`。 | 若将预览序号与 run number 解耦，需要同步调用参数和说明；本轮不改 workflow。 |
| `installers/macos/build_installer.py:12` | `NAME` 固定为 `JUST-Audio-Plugin-Bundle-0.1.0-macOS-arm64-b99abd3-selectable-unsigned.pkg`；`:121–122` 使用 `OUT/NAME` 调用 `productbuild`。 | 第 10/11 项确认后，将最终文件名改为 `JUST-<release-version>-macOS-arm64-Installer[-unsigned].pkg`。不改变 `VERSION`、`HEAD`、组件包名或 bundle 身份来实现纯文件名变更。 |
| `installers/macos/build_installer.py:164` | 清单记录实际 `final` 路径、字节和摘要。 | 保留实际输出的清单记录，并同步下一次发布说明、下载表和校验文件。冻结输入 archive `:53` 属于载荷来源，不能当作最终附件名随便更名。 |

没有发现本仓库脚本中写死 `-UI-fix-setup.exe` 的生成逻辑；当前发布记录和 Release 使用该附件名。现有材料不足以证明它由哪个外部发布步骤改名，故不把“手动改名”写成已确认事实。

下一次改名后，应检查打包、清单、校验、验证入口和下载表是否统一，并按新文件重新完成发布检查。旧附件、旧 tag、历史记录中的旧直链及其 `SHA256SUMS.txt` 保留原样。
