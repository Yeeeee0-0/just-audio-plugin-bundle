# Install JUST 0.1.0 on macOS

Download the installer from the [0.1.0 release](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/tag/v0.1.0):

`JUST-Audio-Plugin-Bundle-0.1.0-macOS-arm64-b99abd3-selectable-unsigned.pkg`

SHA-256: `c1359f53d5307ff994c4d8fd1e8930e0f8a5996c3140dc31a09fea69cc4f56cc`.

This release contains Apple silicon arm64 VST3 plugins. Use a native Apple
silicon VST3 host. Intel, Universal, Rosetta and Windows binaries are not included.
The build deployment minimum is macOS 11.0; older macOS runtime compatibility
has not been certified.

## Installation

1. Save your work and close audio hosts.
2. Open the PKG in macOS Installer. Installation is for the current user only.
3. On Installation Type / Customize, choose the plugins you want. All ten are
   selected by default. Unchecking a plugin skips it and leaves an existing copy
   untouched; it does not uninstall it.
4. Review the selection and continue with installation. Selected plugins go to
   `~/Library/Audio/Plug-Ins/VST3`.
5. After Installer reports success, reopen your host and rescan VST3 plugins.

The package is unsigned and not notarized. The plugins retain verified ad-hoc
signatures, which are not Apple Developer ID signatures. If macOS blocks the
package or plugin, obtain a Developer ID signed and notarized release while
keeping system security protections enabled.

## Included bundles

| Bundle | Effect |
| --- | --- |
| `Just_eq.vst3` | EQ |
| `Just_reverb.vst3` | Reverb |
| `Just_delay.vst3` | Delay |
| `Just_tremolo.vst3` | Tremolo |
| `Just_compressor.vst3` | Compressor |
| `Just_limiter.vst3` | Limiter |
| `Just_gate.vst3` | Gate |
| `Just_flanger.vst3` | Flanger |
| `Just_fake_stereo.vst3` | Wider (compatible internal name) |
| `Just_distortion.vst3` | Distortion |

The optional VST3 ZIP contains the same frozen bundles. With hosts closed, copy
only the chosen bundles from its `VST3` directory into the current-user VST3
folder above. Avoid keeping duplicate copies in multiple plugin search paths.

## Updates and removal

The installer replaces only selected bundles at their fixed paths, using bundle
identity and version checks. It does not relocate other copies, alter other
plugins, launch hosts, or run installation scripts. Same-version reinstall is
allowed; newer installed versions are not downgraded.

User presets are outside the package at
`~/Library/Application Support/JUST/Presets/v1/`. Keep that folder when upgrading
or removing plugins. If you manually stored personal files inside a plugin
bundle, move them out before replacement.

To uninstall, close hosts and move only the exact bundles listed above to Trash.
Do not delete the entire VST3 folder or use a broad name wildcard.

## Verification boundary

The expanded installer contains 152 files matching the frozen plugin set. Ten
independent choices and fourteen CLI selection scenarios passed. Native Installer
GUI checks passed for default-all, single and mixed selection, restoring all
choices, current-user scope and the pre-install confirmation page. The GUI check
stopped before installation. It is not a fresh live install/upgrade or a new DAW
acceptance test. See [release notes](RELEASE_NOTES_0.1.0.md) for platform limits.

## 中文简要说明

保存工程并关闭音频宿主，打开 PKG。在安装类型/自定义页选择需要的插件；默认十款
全选，取消勾选只会跳过，不会卸载已有副本。仅安装到当前用户的
`~/Library/Audio/Plug-Ins/VST3`。安装成功后重新打开原生 Apple silicon VST3 宿主并扫描。

本版仅提供 Apple silicon arm64 插件；安装包未 Developer ID 签名、未公证。若系统
阻止安装或加载，应获取签名并公证的版本并保持系统安全保护开启。用户预设目录不在
安装范围内。卸载时只移除上表明确列出的插件，保留用户预设。
