# Windows 0.1.0 对照验收记录

先处理 `KNOWN-DIFFERENCES-zh.md` 的已知无障碍功能缺口，并实测新补的单位提示与低性能勾选框；源码整合不等于已验收。

所有 Windows 栏初始为 **未验证**。源码实现、自动化离屏截图和 REAPER 原生鼠标/键盘/音频证据分别填，不能互相代替。用冻结 `b99abd3` 的 Mac 源码/资产辅助比较；公开候选不附旧私有参考截图，缺少实机像素对照时应明确记录；本包不是十款视觉等同的已验收结论。

记录设备/Windows/屏幕DPI/缩放、REAPER版本及位数、实际加载路径、二进制SHA256、CMake/MSVC/WindowsSDK版本、采样率/块大小/路由、测试工程新路径。每个PASS附截图/日志或测量文件；FAIL写复现步骤、预期、实际、修复commit。

| 产品 | 内部slug | 源码补齐审查 | 原生构建/validator | HWND离屏测试 | REAPER视觉/输入 | 音频/预设/自动化 |
|---|---|---|---|---|---|---|
| EQ | eq | 待核对 | 未验证 | 未验证 | 未验证 | 未验证 |
| Reverb | reverb | 待核对 | 未验证 | 未验证 | 未验证 | 未验证 |
| Delay | delay | 待核对 | 未验证 | 未验证 | 未验证 | 未验证 |
| Tremolo | tremolo | 待核对 | 未验证 | 未验证 | 未验证 | 未验证 |
| Compressor | compressor | 待核对 | 未验证 | 未验证 | 未验证 | 未验证 |
| Limiter | limiter | 待核对 | 未验证 | 未验证 | 未验证 | 未验证 |
| Gate | gate | 待核对 | 未验证 | 未验证 | 未验证 | 未验证 |
| Flanger | flanger | 待核对 | 未验证 | 未验证 | 未验证 | 未验证 |
| Wider | fake_stereo | 待核对 | 未验证 | 未验证 | 未验证 | 未验证 |
| Distortion | distortion | 待核对 | 未验证 | 未验证 | 未验证 | 未验证 |

每款共同验收：

- VST3-only、AMD64 PE32+、factory和文件资源均0.1.0，Yee Huang；20个原UID、完整原参数ID/标志/默认不变。展示名Wider不更改旧内部身份。
- 首开英文；中英切换即时更新标签/菜单/设置/反馈；Simple/Advanced切换不发声音参数写入，隐藏参数保持原值；保存工程后重开语言/大小/显示模式保持。
- 原品牌图标、浅色背景、圆角、页头84/页脚40逻辑单位、比例与Mac代码合同一致；默认/最窄/75/100/125/150%缩放及100/150/200%系统DPI无错位、文字截断、重复缩放或无法点选。
- 所有旋钮/滑杆/分段选择均按各产品原Mac形式排列；上下拖、Shift细调、受支持滚轮/箭头、右键恢复默认、直接文字输入/Enter提交/Esc取消、无效输入拒绝、焦点丢失结束手势；一次手势begin/perform/end配对，宿主automation可写回。
- 旁路按钮红色，其他图像黑白；图表停在最后实际测量，参数仍可修改；解除旁路丢弃旧显示历史，再绘新音频。不得用截图遮挡导致交互失效，不得改变算法旁路/尾音/PDC规则。
- 工厂预设完整；用户预设新建/重名拒绝/整状态载入/重命名/二次确认删除/撤销；pending及拒绝显示正确，不自动改变旁路；preset只保存声音数据，不污染UI偏好；跨实例/重开一致，损坏/异插件文件安全拒绝。
- Settings/About显示0.1.0、Yee Huang和已有联系信息；UI缩放按宿主接受结果更新，拒绝时回退；恢复UI默认不改声音参数；设置状态为真实反馈，无Foundation占位文字。
- 宿主正常音频、暂停、静音、尾音、无sidechain/静音sidechain、mono、变tempo时状态与真实数据相符；关闭GUI、多实例、重开GUI与参考实例的音频一致，无泄漏、卡死或临时绘图改变声音。

产品专属：

| 产品 | 必测行为 |
|---|---|
| EQ | 默认120dB右轴；显式保存60/90/120保留、旧UI回退120；无主图说明tooltip；真实输入/输出频谱和Analyzer设置；12band/声场/shape/slope/dynamic/外部SC；band浮层选中/hover/focus/文本/Solo/拖动/超时渐隐/隐藏后不可命中；图节点/曲线和Left/Right/Mid/Side语义一致。 |
| Reverb | 七Simple宏及完整Advanced；dry/wet测量、predelay sync互斥、freeze与尾音、空间/衰减等宏保留hidden值策略。 |
| Delay | Simple四项、Advanced组标题/布局；L/R时间与Sync divisions、真实反馈包络、tempo unavailable、反馈尾音/安全限制。 |
| Tremolo | 同Mac AmountDisplay、Rate Free/Sync原位转换；实测modulation min/max与音频时钟；StereoPhase/Waveform/PhaseReset、不同transport状态。 |
| Compressor | 真输入/输出包络、阈值线拖动、GR/SC读数；Simple和20参数Advanced布局、未支持控件按原合同说明，禁止旧“GR unavailable”占位。 |
| Limiter | 同Mac Simple输入/输出增益及Advanced；真实峰值/GR和Max复位；PDC/quality pending、算法旧状态/新版边界/输出增益与旁路状态。 |
| Gate | Threshold/Range/Attack/Release Simple与Advanced；真gain/GR/SC包络、阈值交叉/侧链丢失/短暂信号及恢复。 |
| Flanger | 真反馈/输入输出/调制状态、stop/no audio区别；Simple/Advanced和全部模式中文、速率Sync及声道关系。 |
| Wider | 同Mac声场图比例、M/S散点与L/R、M/S电平/相关度/clip读数；四项Simple、fader/分段选项/advanced、mono held/locked和声场变换，不能改fake_stereo身份。 |
| Distortion | Drive/Tone/Mix/Output与Advanced模式/滤波/品质；真波形或实测反馈、quality pending、停止/静音状态、输出增益。 |

音频矩阵至少每款在44.1/48/96kHz与64/256/1024块下进行支持声道/float格式、automation、保存重载、旁路与尾音测试。先跑已有算法/参数/状态fixture，再记录REAPER离线render；Mac跨平台样本对照缺失时必须列明。增加极端参数/随机automation用现有安全约束，不修改DSP以“使测量通过”。

备份验收：旧JUST bundle逐文件SHA256及复制一致；REAPER资源目录及授权仅本地备份；原工程未覆盖；测试工程新路径；测试结束说明保留候选或恢复旧版，恢复后再核验旧插件哈希。重复扫描路径必须解决以避免测错旧版本。
