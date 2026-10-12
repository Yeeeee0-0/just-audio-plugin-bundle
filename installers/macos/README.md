# macOS 0.1.0 packaging / macOS 0.1.0 打包资料

These files preserve the selectable installer recipe used for the frozen
`b99abd3be4ad89b21e3a0c3caf1f12904503f168` macOS binaries. The corresponding
sanitized public source was published at
[`f2a710510415395e13e548400143604c455df8fe`](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/commit/f2a710510415395e13e548400143604c455df8fe).
The scripts now accept local paths as arguments. They were syntax checked only;
they were **not used to regenerate the package in this publication**.

这些资料保留冻结 macOS 二进制的原可选安装器构建方法。对应的净化公开源码为上面的
`f2a7105` 提交。此次仅把本机路径改成命令行参数并检查 Python 语法，**没有重新生成本次发布的安装包**。

## Published package / 已发布的安装包

[`JUST-Audio-Plugin-Bundle-0.1.0-macOS-arm64-b99abd3-selectable-unsigned.pkg`](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/download/v0.1.0/JUST-Audio-Plugin-Bundle-0.1.0-macOS-arm64-b99abd3-selectable-unsigned.pkg)

- Size / 大小: **11,254,806 bytes**
- SHA-256: `c1359f53d5307ff994c4d8fd1e8930e0f8a5996c3140dc31a09fea69cc4f56cc`
- Version / 版本: **0.1.0**, all ten plugins / 十款插件均相同。
- Architecture / 架构: **Apple Silicon arm64 only / 仅 arm64**; Intel, Universal and Rosetta host support are not included / 不包含 Intel、Universal 或 Rosetta 宿主支持。
- The PKG is **unsigned and not notarized**. The original plugin signatures are **ad-hoc**, not Developer ID signatures / PKG **未签名、未公证**；插件保留原有 **ad-hoc** 签名，不是 Developer ID 签名。
- macOS 11.0 is the binary deployment minimum, not proof of runtime acceptance on older macOS releases / macOS 11.0 是二进制构建最低版本，不代表旧系统运行验收。

The ten independent choices start selected. Only selected plugins are installed
for the current user at `~/Library/Audio/Plug-Ins/VST3`; clearing a choice keeps
an existing copy. Bundle identities, version checks and atomic bundle upgrades
are retained. There are no installation scripts. User presets at
`~/Library/Application Support/JUST/Presets/v1/` are outside the payload.

十项独立选项默认全选，只安装勾选项到当前用户的 VST3 目录；取消勾选保留现有副本。
保留精确 bundle 身份、版本检查及逐 bundle 原子升级，不包含安装脚本，用户预设不在载荷内。
卸载时先关闭宿主，仅移除安装说明列出的十个确切 `.vst3` bundle，保留用户预设。

Existing verification covered exact frozen payload equality, signatures and
metadata, fourteen native selection calculations, and the pre-install GUI choices
and current-user destination. **Actual installation, upgrade, completion-page
and post-install DAW scanning of this PKG were not performed.** The existing
plugin checks do not establish compatibility with every DAW or macOS release.

已有验证覆盖冻结载荷逐文件一致、签名及元数据、14 种原生选择计算和安装前 GUI
选项、当前用户目的地。**未执行该 PKG 的实际安装、升级、完成页及安装后 DAW 扫描**。
已有插件检查不代表所有 DAW 或所有 macOS 版本均受保证。

## Reusing the recipe / 复用构建方法

Use macOS with Python 3.9 or later, the system `pkgbuild`, `productbuild`,
`pkgutil`, `codesign`, `lipo`, `otool`, and Xcode command-line tools (`vtool`).
The input must be the original frozen handoff layout:

```text
frozen-b99abd3/
  release-handoff.json
  JUST-0.1.0-mac-arm64-b99abd3.zip
  VST3/Just_<slug>.vst3/...
```

```sh
python3 installers/macos/build_installer.py \
  --frozen-root /path/to/frozen-b99abd3 \
  --output-dir /path/to/new-output
```

The output directory receives the PKG, `build/` staging and `evidence/` reports;
the script refuses existing payload staging. It packages frozen bytes and does
not rebuild plugins, sign, notarize, install, or launch a DAW. Generated component
plists and Distribution XML stay in the output directory; this source directory
is not rewritten. Keep local handoff manifests and generated reports private
when they contain machine-specific paths.

输入须包含上述原冻结交付结构。输出目录保存 PKG、`build/` 和 `evidence/`，已有载荷暂存
会被拒绝。脚本只封装冻结字节，不重编插件、不签名公证、不安装或启动 DAW，也不会改写
本源码目录。冻结交付清单与生成报告可能包含本机路径，不应直接公开。

The original manifest requirements are unchanged: exact frozen commit, archive
SHA-256, all per-file bundle hashes, bundle identities, version fields, arm64,
macOS 11.0 deployment target and valid original ad-hoc signatures. Recorded
manifest paths are rebased to `--frozen-root` while retaining their relative
layout and hashes. Copying the input directory does not require editing its
historical absolute paths.

保留原冻结清单要求：精确 commit、归档散列、全部 bundle 文件散列、身份、版本、arm64、
macOS 11.0 部署目标及有效的原 ad-hoc 签名。仅将清单中的路径映射到 `--frozen-root`，
相对结构及散列不变，移动交付目录无需修改其历史绝对路径。

Optional native read-only selection queries, using a completed output directory:

```sh
python3 installers/macos/validate_choices.py --output-dir /path/to/new-output
```

This writes query results into `evidence/` without installing. It was not run as
part of this documentation update. / 此可选查询把结果写入 `evidence/`，不执行安装；
本次资料更新没有运行它。
