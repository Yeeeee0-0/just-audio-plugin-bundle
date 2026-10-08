# Windows 交接时已知差异与证据边界

本清单保留首次源码交接时的对照及未验证边界。后续 CI 原生构建、离屏截图、validator 与安装器结果以所下载预发布的 `preview-manifest.json`、Evidence.zip 和实际工作流为准；不要将下文首次交接的“尚未执行”当作新预发布的执行记录，也不要将 CI 结果当作用户 REAPER 视觉/输入验收。十款首轮代码已整合，但不能据此保证界面/功能完全一致。下列功能缺口仍属于本任务待完成内容。

## 源码可确认的剩余差异

| 项目 | 冻结 Mac | 本包 Windows | 后续处理 |
|---|---|---|---|
| 字体、字宽、抗锯齿 | Cocoa 系统字体及绘制 | Segoe UI / GDI+，中文依赖系统字体回退 | 属于平台绘制差异；核验中英文截断、基线、字号和间距，不能宣称像素一致。 |
| 菜单、文本输入、滚动条、焦点框 | NSPopUpButton/NSTextField/NSScrollView | Win32 COMBOBOX/EDIT/滚动条和焦点绘制 | 原生控件外观及输入法/键盘细节有差异；需实际高DPI、Tab、Esc、IME及滚动测试，保留功能和层级，不得改成通用参数页。 |
| 低性能模式开关 | 带 ON/OFF 文本的 checkbox | 已改为自绘勾选框 + ON/OFF，保留原位置与 Win32 checkbox 状态语义 | 源码补齐；勾选形状、缩放、键盘与旁路灰度仍须 Windows 实测。 |
| 自绘旋钮无障碍 | ControlsMac.mm 设置 slider role 与参数 accessibilityLabel，数值域单独标注 | 已实现 UI Automation Slider、物理范围 RangeValue、带单位 Value、双语名称、独立 EDIT 名称及键盘操作；复用原参数自动化手势 | 原生 `windows-controls-accessibility` 测试必须通过后才能打包；实际读屏器、焦点导航及 REAPER 仍须用户机检查，不能把接口测试当作完整无障碍验收。 |
| 旋钮数值单位提示 | 数值域 toolTip=spec.unit | 已为数值 EDIT 附加 spec.unit 原生 tooltip，并处理字体与销毁生命周期 | 源码补齐，尚未 Windows 实测；仅数值域有提示，EQ 主画布仍无说明 tooltip。 |
| 用户预设目录 | macOS Application Support 下的 JUST 路径 | Windows KnownFolder 对应的 `%APPDATA%\JUST\Presets\v1` | 必要平台路径映射；文件状态格式不变，需本机验证中文用户名、并发及权限错误。 |

相关源文件：`common/ui/NativeEditorMac.mm`、`NativeEditorWindows.cpp`、`ControlsMac.mm`、`ControlsWindows.cpp`、`VisualAssetsWindows.hpp`。这些差异并不证明所有其他视觉细节都已相同。

后续补齐：旋钮 provider 通过原 HWND 将读写转交 UI 线程，提供禁用/值/名称/焦点事件；窗口销毁后接口失效。新增隐藏窗口中的真实 Windows UIA 客户端测试覆盖角色、范围、读写、拒绝写入、中文名称、原参数 ID/手势和销毁。该实现不改变旋钮绘制正文、DSP 或 Mac 代码；各预发布是否通过该原生测试以其证据为准。

## 已收敛的源码差异（待原生运行确认）

整合中 Windows 曾额外加入预设导入/导出按钮，冻结 Mac 没有这两个入口。最终交接已撤回扩展，恢复 Save new / Load / Rename / Delete 四个按钮及原删除确认/提示的逻辑位置。运行测试对照当前冻结合同，不要求不存在的 Mac 功能；共享预设格式未变。另已补齐旋钮数值单位提示，低性能按钮改为自绘勾选框与 ON/OFF 文本。这些源码修复不等于 Windows 测试已通过。

## 需要 Windows 运行才能判断的部分

- 十款的实际文本排版、圆角边缘、浮层遮挡、缩放/高DPI、深浅宿主背景、native 子控件旁路灰度及焦点效果。源码坐标对齐不等于可见结果相同。
- EQ 的浮层时序/命中/消失、Solo 失焦释放、Analyzer、120dB 轴及各声道曲线；其他九款的真实测量、旁路冻结/恢复和专有控件。自动离屏图不能代替 REAPER 中鼠键操作。
- 多实例/关闭重开/卸载与窗口类和 GDI+ 生命周期、宿主拒绝缩放、自动化手势、预设 pending/拒绝和跨实例更新。
- Windows/MSVC/SDK 编译、PowerShell 安装与还原、实际 VST3 扫描/UID/文件版本、音频输出与工程保存重载。本包没有这些项目的运行结果。
- 公开源码不分发旧 Mac QA 对照截图，没有十款全状态最终实机截图。缺失参考图时保留“像素对照未验证”，以冻结 Mac 源码和公共模型辅助判断，不能自行制造 Mac 验收证据。

## 实际代码、测试代码与证据

`common/ui/*Windows*`、十款 `EditorWindows.cpp` 是候选产品实现；`tests/windows`、EQ 和 Dynamics 的 Windows fixture 是随包正式测试源码，但尚未在 Windows 执行。Mac 上为接口探测建立的临时 WinAPI 声明/探测文件不随包交付、不作为通过依据。旧 Mac CLI 回归及哈希日志不随公开源码提供；新增 Windows 日志必须写到 `build/windows-evidence/` 等独立本地证据目录，分享前检查隐私。

不能将上述功能缺口仅改为“平台差异可接受”以跳过修复，也不能修改 DSP、参数、ID、保存格式或 Mac 源码来凑测试通过。修复 Windows 范围问题后，更新本清单为真实结果并保留差异补丁。
