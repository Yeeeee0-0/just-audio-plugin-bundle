# Release 展示整理审核 / Release presentation review

此目录是 T3/T4 的可审阅交付。本轮只提交仓库文档：没有执行任何线上 Release 编辑，没有修改 tag、Pre-release / Latest 状态或附件。
This is the reviewable T3/T4 handoff. This change only adds repository documents; no online release, tag, prerelease/latest state or asset was edited.

## 当前发布定位 / Current release status

按所有者最新确认，当前产品展示为正式 **0.1.0**，Windows 下载链接使用“JUST 0.1.0 Windows x64 Setup”，并明确未签名。历史 tag、附件名和原文中的 `preview` 均保留。线上 GitHub 记录仍保留归档中的 Pre-release / Latest 元数据，本轮没有修改；所有者后续单独决定其与当前产品定位的对齐方式。这次文案修订没有新增测试，也不表示已验证完整的 DAW 兼容性覆盖。
Per the owner’s latest confirmation, the current product release is **0.1.0**. Windows download links use “JUST 0.1.0 Windows x64 Setup” and explicitly state that it is unsigned. Historical tags, asset names and original text retain `preview`. Online GitHub prerelease/latest metadata remains as archived; the owner must separately decide how to align it with the current product status. This text revision adds no tests and does not claim full DAW compatibility coverage.

## 交付文件 / Deliverables

- [TEMPLATE.md](TEMPLATE.md)：基于策划书 §6.4；按最新授权将 Windows 默认状态改为“未签名”，未来正式/预览状态按实际填写。
- [NAMING.md](NAMING.md)：§6.1、§6.5、§6.6 的未来命名提案，顶部明确标出尚未获准采用。
- [原始 API 快照](archive/releases-api.original.json)：本次读取的 6 个 Release 完整 API 返回。
- [归档索引](archive/INDEX.json)：原标题、原正文 UTF-8 字节数 / SHA-256 / CRLF 数、归档前缀长度、附件和状态。
- [附件核对记录](proposed/ASSET-CHECKS.json)：附录 A 所列名称、直链、字节数与 API SHA-256 digest 的精确比较，未发现不匹配项。

| Tag | 拟上线文件 / Proposed | 完整原文归档 / Original |
| --- | --- | --- |
| `v0.1.0-installers.20261013-host-fix` | [标题](proposed/v0.1.0-installers.20261013-host-fix.title.txt) · [正文](proposed/v0.1.0-installers.20261013-host-fix.body.md) | [原标题 + 原正文](archive/v0.1.0-installers.20261013-host-fix.original.md) |
| `v0.1.0-installers.20261012` | [标题](proposed/v0.1.0-installers.20261012.title.txt) · [正文](proposed/v0.1.0-installers.20261012.body.md) | [原标题 + 原正文](archive/v0.1.0-installers.20261012.original.md) |
| `v0.1.0` | [标题](proposed/v0.1.0.title.txt) · [正文](proposed/v0.1.0.body.md) | [原标题 + 原正文](archive/v0.1.0.original.md) |
| `v0.1.0-windows-preview.17` | [标题](proposed/v0.1.0-windows-preview.17.title.txt) · [正文](proposed/v0.1.0-windows-preview.17.body.md) | [原标题 + 原正文](archive/v0.1.0-windows-preview.17.original.md) |
| `v0.1.0-windows-preview.13` | [标题](proposed/v0.1.0-windows-preview.13.title.txt) · [正文](proposed/v0.1.0-windows-preview.13.body.md) | [原标题 + 原正文](archive/v0.1.0-windows-preview.13.original.md) |
| `v0.1.0-windows-preview.12` | [标题](proposed/v0.1.0-windows-preview.12.title.txt) · [正文](proposed/v0.1.0-windows-preview.12.body.md) | [原标题 + 原正文](archive/v0.1.0-windows-preview.12.original.md) |

归档 `.original.md` 的第一行是原标题，空行后是原正文 UTF-8 字节。原正文共 18,076 字节，保留全部 CRLF 及末尾换行；原始 JSON 也保存完整 `name` / `body`。归档及旧 preview 正文使用局部 `.gitattributes` 避免 Git 转换换行。三个旧 preview 的拟上线正文仅增加附录 A.4 提示，并按最新授权把提示下载引导改为当前正式 0.1.0；原正文是逐字节相同的后缀，未改写任何历史结论。

## 已核对 / Verified against the API snapshot

附录 A.0 的 6 个原标题与 API 相符。A.1/A.2 的 `SHA256SUMS.txt` 和 `release-manifest.json` 均存在；A.3 表格列出的八个附件名与 `v0.1.0` 的八个实际附件完全对应。所有待核对标记已从拟上线正文去除，附件名、哈希和字节数未自行替换。

| Release / asset | 字节 Bytes | SHA-256 | 结果 Result |
| --- | ---: | --- | --- |
| `v0.1.0-installers.20261013-host-fix` Windows Setup | 4,031,050 | `22946543862a919f32621aabee3c00d294d97f6e11cb8e6c4e525a92373c1040` | 与 API 相符 / Match |
| `v0.1.0-installers.20261012` Windows UI-fix Setup | 4,013,818 | `4fd10096f4135fc1520875eb78eda7e5ff82347bcdd8f85b8bc52d23712d8bab` | 与 API 相符 / Match |
| `v0.1.0-installers.20261012` macOS PKG | 11,254,806 | `c1359f53d5307ff994c4d8fd1e8930e0f8a5996c3140dc31a09fea69cc4f56cc` | 与 API 相符 / Match |
| `v0.1.0` macOS PKG | 11,254,806 | `c1359f53d5307ff994c4d8fd1e8930e0f8a5996c3140dc31a09fea69cc4f56cc` | 与 API 相符 / Match |

上述结果核对 GitHub API 的附件元数据；不是重新下载所有历史二进制的证明。两份 macOS PKG 的 API 字节数和 SHA-256 相同。实际外链请求结果列在 PR 的链接验收记录中。
These checks compare GitHub API asset metadata, not fresh downloads of every historical binary. The two macOS PKG asset records have the same size and SHA-256. Actual external-link request results are recorded with the PR validation.

## 需要所有者确认 / Owner decisions

1. **未来 tag 规范（策划书 §9.5）**：是否采用 `vX.Y.Z-preview.N` / `vX.Y.Z-pkg.N`。示例 `pkg.1` / `pkg.2` 没有正式分配。
2. **未来附件规范及脚本（§9.10–11）**：是否采用新名称、是否保留 `-unsigned`，以及是否修改打包脚本。当前脚本和附件不动。
3. **GitHub Pre-release / Latest 元数据（§9.4）**：当前产品定位已由所有者确认为正式 0.1.0；线上记录尚未调整，本次只改文档。`v0.1.0` 拟上线正文提供 Windows 0.1.0 下载引导。所有者需单独决定是否调整线上 Pre-release / Latest 元数据或另发 Release；本轮不执行状态修改，也不声称新增验收。
4. **COMCTL32（§9.12）**：当前 API `preview.13` 正文没有 COMCTL32 条目，当前发布记录也没有足以确认其后续修复的证据。三个旧 preview 的提示按附录 A.4 的兜底要求省去“问题已在后续版本中修复”，仅称早期 Windows 测试版。仍需所有者提供可验证的修复结论。

## 下次发版的命名修改位置 / Future packaging locations

仅记录具体位置，不修改脚本：

- [`scripts/windows/Build-Installer.ps1`](../../scripts/windows/Build-Installer.ps1) 第 34 行 `$exe` 定义 Windows 默认文件名；第 12 行定义默认输出目录。若采用规则，可在下一次发版时从获准的版本/tag 生成对应 `JUST-<version>-Windows-x64-Setup[-unsigned].exe`，其中 `-unsigned` 取决于所有者选择。
- [`scripts/windows/prepare-preview-release.py`](../../scripts/windows/prepare-preview-release.py) 第 70 行引用当前固定安装包路径；若生成名改变，同步使用实际输出名，并核对脚本生成的清单和校验文件。
- [`installers/macos/build_installer.py`](../../installers/macos/build_installer.py) 第 12 行 `NAME` 定义现有 PKG 名；未来可在所有者确认后生成 `JUST-<version>-macOS-arm64-Installer[-unsigned].pkg`。`--output-dir` 负责输出目录。命名变化本身不改变签名状态。

## 所有者上线步骤 / Owner publication steps

在本地检出本 PR 分支，先审阅上表的标题与正文，并确认相关 §9 问题。执行前重新读取线上 6 个 Release；如果原标题、正文、附件、tag 或状态相较归档发生变化，先重新归档和核对，避免覆盖新内容。

下面仅是按策划书 §8 交给所有者的命令，**本轮没有执行**。每条只设置标题和正文，不带 tag、状态、Latest 或附件修改参数。所有者确认 Latest 方案后再应用 `v0.1.0` 对应草稿。

```bash
gh release edit 'v0.1.0-installers.20261013-host-fix' --repo Yeeeee0-0/just-audio-plugin-bundle \
  --title 'JUST 0.1.0 — Windows x64 安装器修复 / Windows x64 installer fix' \
  --notes-file 'docs/releases/proposed/v0.1.0-installers.20261013-host-fix.body.md'

gh release edit 'v0.1.0-installers.20261012' --repo Yeeeee0-0/just-audio-plugin-bundle \
  --title 'JUST 0.1.0 — Windows UI 修复 + macOS 原包 / Windows UI fix + macOS (Windows 已取代)' \
  --notes-file 'docs/releases/proposed/v0.1.0-installers.20261012.body.md'

gh release edit 'v0.1.0' --repo Yeeeee0-0/just-audio-plugin-bundle \
  --title 'JUST 0.1.0 — macOS Apple silicon 正式版 / macOS stable' \
  --notes-file 'docs/releases/proposed/v0.1.0.body.md'

gh release edit 'v0.1.0-windows-preview.17' --repo Yeeeee0-0/just-audio-plugin-bundle \
  --title '[已取代 Superseded] JUST 0.1.0 Windows x64 preview 17' \
  --notes-file 'docs/releases/proposed/v0.1.0-windows-preview.17.body.md'

gh release edit 'v0.1.0-windows-preview.13' --repo Yeeeee0-0/just-audio-plugin-bundle \
  --title '[已取代 Superseded] JUST 0.1.0 Windows x64 preview 13' \
  --notes-file 'docs/releases/proposed/v0.1.0-windows-preview.13.body.md'

gh release edit 'v0.1.0-windows-preview.12' --repo Yeeeee0-0/just-audio-plugin-bundle \
  --title '[已取代 Superseded] JUST 0.1.0 Windows x64 preview 12' \
  --notes-file 'docs/releases/proposed/v0.1.0-windows-preview.12.body.md'
```

执行后再次查看 6 个 Release，核对标题/正文和归档记录中的 tag、Pre-release 状态及完整附件列表；保持当前 Latest 设置。不要重命名、替换、删除或补传附件。
