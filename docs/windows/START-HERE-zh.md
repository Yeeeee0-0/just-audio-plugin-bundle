# Windows x64 预览：下载与本机验收

这是 JUST 0.1.0 的独立 **Windows 预览分支，真实 REAPER 验收尚未完成**。CI 只有在原生构建、自动测试和隔离安装器测试全部通过后才发布 prerelease。以所下载版本的 `preview-manifest.json`、工作流结果和 Evidence.zip 为准；不存在对应预发布资产时，源码不能当作可安装插件。稳定 macOS 0.1.0 仍以主分支和 Mac 发布资产为准。

在仓库 [Releases](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases) 中选择标为 **Windows x64 preview / 待实机验证** 的预发布，下载同一版本的以下文件到一个新目录：

- `JUST-0.1.0-Windows-x64-preview.N-Setup.exe`：十款可选安装器，默认全选，未签名。
- `JUST-0.1.0-Windows-x64-preview.N-Portable.zip`：相同的十款 x64 VST3 完整包。
- `JUST-0.1.0-Windows-x64-preview.N-Source.zip` 与 `Evidence.zip`：对应公开源码、资产及 CI 证据。
- `preview-manifest.json`、`SHA256SUMS.txt`、`verify-preview-download.py`、`WINDOWS-CODEX-PROMPT-zh.txt`、验收清单与已知差异。

`N` 是实际预发布编号。不要混用不同预发布的文件，也不要假设同账号会自动同步 Mac 源码。已有 Python 时运行 `python verify-preview-download.py .` 可核对下载摘要、十款 PE AMD64 架构、版本和供应商；此步骤不执行 Windows 插件，不证明 REAPER 可用。把随包中文提示词粘贴到 Windows Codex，让它从真实目录和证据开始完成本机验收。

保存工作并正常关闭 REAPER 后，运行已核验的安装器。组件页可独立选择十款，默认目标是 64 位 Common Files 下的 VST3。安装器只备份及替换选中的 JUST 插件，不修改预设、REAPER 设置、授权或工程。备份与恢复方式见 `installers/windows/README.md`。安装器为未签名预览，宿主视觉和音频通过前不要作为正式版接受。

在 Windows AMD64 电脑上克隆公开的 `windows-port` 候选分支，并用进程级 Git 配置保留 LF：

```powershell
git -c core.autocrlf=false clone --branch windows-port https://github.com/Yeeeee0-0/just-audio-plugin-bundle.git
cd just-audio-plugin-bundle
```

稳定 Mac 0.1.0 位于 `main` 和 `v0.1.0`；不要把旧私有来源提交当作公开仓库可直接检出的提交。

`.github/workflows/windows-preview.yml` 使用公开仓库的标准 GitHub 托管 Windows runner，进行原生 MSVC 构建及测试；发布任务只创建 Windows prerelease，不标记为 latest。没有创建 Codespace 或 Codex 云工作区，也没有连接用户的 Windows 电脑。工作流只使用任务期临时 GitHub token 发布资产，不创建用户凭据。

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

`KNOWN-DIFFERENCES-zh.md` 记录平台绘制、无障碍实测和数值对照边界；`ACCEPTANCE-zh.md` 的用户机验收项初始为未验证。本公开候选未附旧 Mac 截图或运行日志，Mac 源码和共享模型只作实现合同，不能冒充像素对照证据。

安装与恢复脚本属于显式人工选择的本地候选测试流程。只有在当前用户确认安装目标、正常关闭 REAPER、完成并核验备份后使用。备份可能含 REAPER 授权与用户设置，必须留在本地；不可提交 Git 或上传为 Release 资产。

`docs/windows/ci/windows-x64.yml` 是早期手动示例；当前实际流程在 `.github/workflows/windows-preview.yml`。预览只使用公开仓库的标准 runner，不使用付费大机型、付费签名服务或积分；不要为本任务购买工具或额度。
