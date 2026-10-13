# Release 和附件命名提案 / Release and asset naming proposal

> **待所有者确认，尚未启用 / Pending owner confirmation; not adopted.**
> 本文记录策划书 §6.1、§6.5、§6.6 的未来发布方案。策划书 §9 要求所有者决定是否采用 tag 规则、安装包规则、是否保留 `-unsigned` 后缀，以及是否修改打包脚本。示例不是已分配的新 tag 或文件名。
> This records a proposal for future releases. The owner must decide whether to adopt the tag and asset rules, retain `-unsigned`, and change packaging scripts. Examples do not allocate new tags or filenames.

现有 Release、tag、附件名、附件内容和脚本保持原样。无签名状态仍须在下载表、发布说明和清单中明确披露；在所有者决定前，不能把下文去掉 `unsigned` 的示例当作已批准规则。
Existing releases, tags, asset names, asset bytes and scripts stay as they are. Unsigned status must remain explicit in download tables, release notes and manifests. Examples omitting `unsigned` await the owner's decision.

## Tag 和标题 / Tags and titles

| 场景 | tag 格式 | 例子 | 类型 |
| --- | --- | --- | --- |
| 正式版（插件有改动） | `vMAJOR.MINOR.PATCH` | `v0.2.0` | Release，设为 Latest |
| 预览 / 候选 | `vX.Y.Z-preview.N` | `v0.2.0-preview.1` | Pre-release |
| 插件没改、只更新安装器或打包 | `vX.Y.Z-pkg.N` | `v0.1.0-pkg.3` | 预览期用 Pre-release，验收后可以改成正式版 |

标题统一为：`JUST <版本> — <一句话要点> / <中文要点>`。

现有 tag **一律不改名、不删除**。在 `NAMING.md` 里写一张"历史 tag 对照表"，说明每个旧 tag 是什么，对应关系见下表：

| 旧 tag | 说明 |
| --- | --- |
| `v0.1.0` | 首个正式版，仅 macOS |
| `v0.1.0-windows-preview.12/13/17` | 早期 Windows 预览，已被取代 |
| `v0.1.0-installers.20261012` | Windows UI 修复，同时重新挂出 macOS 原包 |
| `v0.1.0-installers.20261013-host-fix` | 当前推荐的 Windows 安装器 |

## 附件规范 / Release assets

- 安装包怎么命名，见下文“安装包命名规范”。
- 每个 Release 都附上 `SHA256SUMS.txt`。现在已经这样做了，继续保持。
- 平台包没有变化时，不要重复上传同一个文件，直接在下载表里链接到原来的 Release 附件。host-fix 那次就是这样处理 macOS 包的，做法正确。
- GitHub 会自动附加 "Source code (zip/tar.gz)"，这两个无法移除。正文里不需要再解释它们。

## 安装包命名规范 / Installer asset names

#### 现有名称的问题

| 现有附件名 | 问题 |
| --- | --- |
| `JUST-Audio-Plugin-Bundle-0.1.0-macOS-arm64-b99abd3-selectable-unsigned.pkg` | 太长（70 多个字符）；前缀 `JUST-Audio-Plugin-Bundle-` 和其他文件的 `JUST-` 不统一；带了提交哈希 `b99abd3`，普通用户看不懂；`selectable`、`unsigned` 是状态说明，不该放进文件名 |
| `JUST-0.1.0-Windows-x64-preview-setup.exe`（host-fix） | 没有预览序号。用户下载多次之后，分不清哪个是新的、哪个是旧的 |
| `JUST-0.1.0-Windows-x64-preview-UI-fix-setup.exe` | 把修复内容（`UI-fix`）写进了文件名。每次修复都换一种写法，命名无法预测 |
| `JUST-0.1.0-Windows-x64-preview.12-Setup.exe` | `Setup` 首字母大写，另一个 Windows 包是小写 `setup`，大小写不统一 |
| `JUST-0.1.0-macOS-arm64-VST3.zip`、`JUST-0.1.0-source.zip` | 基本合理，只是 `source` 和 `VST3` 的大小写风格不一致 |

#### 新规则

```
JUST-<版本>-<系统>-<架构>-<类型>.<扩展名>
```

| 字段 | 取值 | 说明 |
| --- | --- | --- |
| `JUST` | 固定 | 所有附件统一用这个前缀，不再写 `Audio-Plugin-Bundle` |
| `<版本>` | 和 tag 一致、去掉开头的 `v`：`0.2.0`、`0.2.0-preview.1`、`0.1.0-pkg.3` | 预发布序号写在版本号里，让文件名和 tag 一一对应 |
| `<系统>` | `Windows` / `macOS` | 大小写固定 |
| `<架构>` | `x64` / `arm64` / `universal` | 必须写，不能省略 |
| `<类型>` | `Setup.exe`（Windows 安装器）/ `Installer.pkg`（macOS 安装器）/ `VST3.zip`（手动安装包）/ `Source.zip`（源码，不带系统和架构字段） | 首字母大写，固定这四种 |

**不写进文件名的内容**：提交哈希、修复内容、`selectable`、`unsigned`。这些放进 `release-manifest.json` 和 Release 正文的"状态"列里。

#### 示例（以后的版本）

| 场景 | 文件名 |
| --- | --- |
| 正式版 Windows 安装器 | `JUST-0.2.0-Windows-x64-Setup.exe` |
| 正式版 macOS 安装器 | `JUST-0.2.0-macOS-arm64-Installer.pkg` |
| macOS 通用二进制（如果以后提供） | `JUST-0.2.0-macOS-universal-Installer.pkg` |
| 手动安装包 | `JUST-0.2.0-Windows-x64-VST3.zip`、`JUST-0.2.0-macOS-arm64-VST3.zip` |
| 预览版 | `JUST-0.2.0-preview.1-Windows-x64-Setup.exe` |
| 插件没变、只更新安装器 | `JUST-0.1.0-pkg.3-Windows-x64-Setup.exe` |
| 源码 | `JUST-0.2.0-Source.zip` |
| 校验与清单 | `SHA256SUMS.txt`、`release-manifest.json`（保持不变） |

#### 已发布的附件：不改名（重要）

GitHub 允许修改附件的显示名，文件内容不会变，但**不要这样做**，原因有三个：

1. README、`INSTALLING.md`、`docs/windows/START-HERE-zh.md` 和各发布记录里的直链都会失效。
2. `SHA256SUMS.txt` 里记录的是原文件名，改名之后 `sha256sum -c` 校验会失败。
3. 已经下载过的用户会对不上号。

所以新规范**只用于下一次发布**，现有附件保持原名。

在 `NAMING.md` 里附一张"旧名 → 若按新规范应叫什么"的对照表，只作参考，不执行改名：

| 旧名 | 按新规范 |
| --- | --- |
| `JUST-Audio-Plugin-Bundle-0.1.0-macOS-arm64-b99abd3-selectable-unsigned.pkg` | `JUST-0.1.0-macOS-arm64-Installer.pkg` |
| `JUST-0.1.0-Windows-x64-preview-setup.exe`（host-fix） | `JUST-0.1.0-pkg.2-Windows-x64-Setup.exe` |
| `JUST-0.1.0-Windows-x64-preview-UI-fix-setup.exe` | `JUST-0.1.0-pkg.1-Windows-x64-Setup.exe` |
| `JUST-0.1.0-Windows-x64-preview.12-Setup.exe` | `JUST-0.1.0-preview.12-Windows-x64-Setup.exe` |
| `JUST-0.1.0-macOS-arm64-VST3.zip` | 不变 |
| `JUST-0.1.0-source.zip` | `JUST-0.1.0-Source.zip` |

表中 `pkg.1`、`pkg.2` 的编号只是示意。如果所有者采用 上文 tag 规范 的规范，由所有者决定正式编号。

#### 执行范围

- 已核对：Windows 默认输出名定义在 [`scripts/windows/Build-Installer.ps1`](../../scripts/windows/Build-Installer.ps1) 第 34 行，默认输出目录定义在第 12 行；[`scripts/windows/prepare-preview-release.py`](../../scripts/windows/prepare-preview-release.py) 第 70 行引用同一个固定路径。macOS PKG 文件名定义在 [`installers/macos/build_installer.py`](../../installers/macos/build_installer.py) 第 12 行的 `NAME` 常量，输出目录由 `--output-dir` 指定。已发布 `-UI-fix` 名称与当前默认名不同；当前源码不能证明当时的改名方式。
- 按 §0.3，执行者**不改这些脚本**。只在 PR 描述里列出需要改的具体位置和改法，由所有者在下次发版前决定。
- Windows 安装器显示的版本是 `0.1.0`，厂商是 `Yee Huang`。安装器不写注册表，也不创建开始菜单快捷方式，所以"添加或删除程序"里没有 JUST 这一项。这些都属于安装器代码，不在这次修改范围内。
