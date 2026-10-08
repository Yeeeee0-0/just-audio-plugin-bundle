# 本次 Windows 源码移植与证据边界

目标保持 JUST 0.1.0 已冻结 Mac 视觉/交互合同。起点是 `b99abd3be4ad89b21e3a0c3caf1f12904503f168`，独立本地分支 `codex/windows-010`。本公开候选的 Windows 源码来源为 `d63e3dbe34c878cf307e642862279d9c9364f827`；这是旧来源的溯源标记，不是公开新历史的可检出提交。这份报告不宣称 Windows 已编译或已达到像素等同。

| 范围 | 旧Windows实现的实际缺口 | 此次源码实现 |
|---|---|---|
| 公共外壳 | 占位式标题栏，缺品牌外壳、设置/关于、用户预设管理与缩放；旁路为普通checkbox | 原图标、84/40逻辑页头页脚、圆角GDI+绘制、原生可编辑控件、中英设置/关于0.1.0、宿主批准缩放、低性能模式、工厂/用户完整预设事务及管理、红旁路与灰度/暂停。 |
| 共享旋钮与图表 | 固定84px旋钮，缺不同字体/暗色/横竖fader | 复用ControlGeometry/RotaryDrag/TextEditSession及DisplayPolicy，支持原Mac旋钮和Wider推子，精确文本、keyboard/Shift、逻辑缩放、真analysis及resume清历史。 |
| EQ | 旧静态图/表单，无真实频谱/Analyzer/Solo/最终浮层合同 | 同EditorModel/PanelPlacement/Presentation模型：120dB默认真Pre/Post频谱、Analyzer选项、按实际采样率静态目标响应、12band和声场、完整/紧凑浮层、超时/渐隐/命中、精确文本与拖拽、hold-Solo租约。无主图说明tooltip；不新增未验收的动态增益估计曲线。 |
| Compressor | 临时SCROLLBAR、GR unavailable | 同Mac包络模型、可拖阈值、真实GR/SC卡片、四项Simple及完整20项旋钮；不改变既有Lookahead支持范围。 |
| Tremolo | 临时滑条和理论波形 | 原Mac AmountDisplay、Free/Sync、完整Advanced、真实modulationMin/Max音频时钟包络。 |
| Delay / Reverb | 分组/中文/暂停不完整 | 恢复各自Mac分组和旋钮语义，真dry/wet/echo反馈、中英图注、冻结测量/tempo及resume清旧数据。 |
| Gate / Limiter | 中文/暂停及Max复位不足 | 对齐各自布局和原生输入，真GR/peak/SC状态，Limiter peak/max-GR复位、旁路灰度暂停。 |
| Flanger / Wider / Distortion | 高级菜单/专用控件/真实状态/暂停不足 | 各自专用界面；Flanger真实时钟/延迟状态；Wider原FieldGeometry/M-S散点、电平/相关度、fader和Mono交互；Distortion模型/Crush/品质pending和真实测量。 |
| Windows构建 | 有VS2022基础但原生测试少，产品文件资源版本不足 | Windows系统绘图库链接、UTF-8/MSVC设置、版本资源0.1.0/Yee Huang、静态CRT配方、离线固定SDK、完整构建/测试/候选打包脚本和可逆备份安装流程。 |

跨模块审查修复：原生窗口类引用计数及注销、创建失败路径、GDI+正常UI生命周期、混合GDI/GDI+双缓冲、旁路原生子控件残留颜色、Analyzer窄化类型、PowerShell5.1 stderr处理、重复VST3路径误检、安装中断后的回滚与备份哈希验证、CI的Git自动换行哈希差异。

以下为旧 Windows 移植交接记录的 Mac CLI 结果摘要；本公开源码不附其原始本机日志，发布审计未重新执行这些测试，不将它们作为 Windows 通过证据：

- 31项产品/模型/算法回归，全部通过；6项公共合同回归，全部通过；预设存储52项检查通过。
- 旧交接记录中的419个受保护文件与冻结基线逐字节一致；公开版去除私有证据和改写文档后保留251个源码/资产/夹具哈希；十款品牌图标、20个UID/参数来源合同和固定SDK1005文件哈希通过。
- 最终Mac配置解析、Git差异检查、Python脚本语法/本地测试音频生成可检查。本次未操作GUI或REAPER，也未改装Mac产品。

待 Windows 实际执行：完整 Win32/MSVC 编译、14项新增原生窗口/模块测试（十款真实DLL + EQ与三款Dynamics fixture）、官方validator、PE/资源版本、PowerShell5.1/7脚本、真实REAPER DPI/视觉/鼠键/音频/预设/automation。临时WinAPI声明的语法探测或纯SDK模板接口检查不等于Windows SDK编译；报告中不把它们作为通过证据。

剩余已知差异及未实现部分详见 `KNOWN-DIFFERENCES-zh.md`，包括尚未对等实现的自绘旋钮无障碍角色与平台原生控件；单位提示和低性能勾选框已作最小源码补齐，尚待Windows运行确认。此次末轮撤回了冻结Mac没有的预设导入/导出扩展，并对齐其四个操作按钮与删除确认的逻辑位置；没有改Mac预设合同。

不同平台系统字体和原生菜单可有像素差异；是否符合用户要求必须看Windows真实截图和输入验收，不能依据源码实现就盖章。若任何源代码或脚本在Windows暴露问题，在当前用户明确的任务范围内修复并重测，保持DSP/身份/格式/Mac文件保护。无可用Windows工具链和离线Windows电脑是本次未能生成候选二进制的真实限制。

REAPER可复现输入由 `scripts/windows/make-test-audio.py <新目录> --sample-rate 48000` 生成；44.1/96kHz各用另一个新目录。脚本生成明确标记的未处理PCM素材和哈希，不打开宿主、不创建虚假DSP结果。请在新测试工程导入，按验收清单加载实际候选并保存渲染证据。
