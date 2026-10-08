# Windows x64 移植候选：从公开源码开始

这是 JUST 0.1.0 的 **Windows 源码候选，尚未通过 Windows 原生编译或 REAPER 验收**。稳定 macOS 0.1.0 仍以主分支和 Mac 发布资产为准；本移植应在独立候选分支使用，验收前不合并到稳定主线。

在 Windows AMD64 电脑上克隆公开的 `windows-port` 候选分支，并用进程级 Git 配置保留 LF：

```powershell
git -c core.autocrlf=false clone --branch windows-port https://github.com/Yeeeee0-0/just-audio-plugin-bundle.git
cd just-audio-plugin-bundle
```

稳定 Mac 0.1.0 位于 `main` 和 `v0.1.0`；不要把旧私有来源提交当作公开仓库可直接检出的提交。

公开仓库可克隆，只表示源码可获取，**不表示已经创建 Codex 云工作区、Codespace、云构建环境或 Windows 实机**。本次发布准备没有创建这些资源，也没有启用 GitHub Actions。

仓库包含项目源码、合成测试夹具和已授权运行图标；不需要旧私有三 ZIP 交接包、Library 文件、测试截图或原始策划文档。先阅读根目录 `README.md`、`BUILDING.md`、`LICENSE`、`ARTWORK.md` 和 `THIRD_PARTY_NOTICES.md`。

## 工具和构建

需要已有的 Visual Studio 2022 C++ x64 工具、Windows SDK、CMake >= 3.25、Python >= 3.9 和 64 位 PowerShell。先从官方固定上游取得锁定的 VST3 SDK，再构建：

```powershell
python scripts/bootstrap-sdk.py
python scripts/verify-source-copy.py
python scripts/check-contracts.py
python scripts/windows/check-source.py
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\windows\Build-And-Test.ps1 -Jobs 2
```

SDK bootstrap 访问官方 Steinberg Git 仓库，锁定 3.8.1 及其子模块；保留各自许可证。不从本仓库分发 Microsoft/Apple SDK。`Build-And-Test.ps1` 本身不下载工具、不操作 REAPER、不安装插件。这里的 ExecutionPolicy 只作用于该进程。

保护清单 `immutable-baseline.json` 固定公开版保留的 251 个原始源码/模型/资产/夹具文件；其摘要仍来自 Mac 基线 `b99abd3be4ad89b21e3a0c3caf1f12904503f168`。不得修改 DSP、UID、参数、状态格式或 Mac 实现以让 Windows 测试通过。

脚本在原生构建、CTest、PE AMD64 检查、SDK validator 和原生离屏测试通过后才生成 `build/candidates/` 下的未签名候选 ZIP。该 ZIP 不包含本机原始诊断日志；日志留在 `build/windows-evidence/`，对外分享前另做隐私检查。候选仍需真实 Windows REAPER 视觉、输入、音频、预设和自动化验收。

`KNOWN-DIFFERENCES-zh.md` 保留已知自绘旋钮无障碍缺口；`ACCEPTANCE-zh.md` 所有 Windows 验收项初始为未验证。本公开候选未附旧 Mac 截图或运行日志，Mac 源码和共享模型只作实现合同，不能冒充像素对照证据。

安装与恢复脚本属于显式人工选择的本地候选测试流程。只有在当前用户确认安装目标、正常关闭 REAPER、完成并核验备份后使用。备份可能含 REAPER 授权与用户设置，必须留在本地；不可提交 Git 或上传为 Release 资产。

`docs/windows/ci/windows-x64.yml` 是未启用的手动 CI 示例，不在 `.github/workflows`。其启用、额度和环境创建是独立操作；本候选没有替用户运行或付费。
