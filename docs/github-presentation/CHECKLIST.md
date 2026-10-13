# 展示优化执行与验收 / Presentation checklist

本轮基线：`de16de3f7e3b5582a7bbe374a27d7ca4105d04ef`；工作分支：`docs/github-presentation`。策划书从 Library 完整分段读取并在本机确认 50,697 UTF-8 字节、773 行（含附录 A）。本机下载尝试未完成，后续以完整 Library read 为依据。

## §3 任务逐项状态

| ID | 交付 | 状态 |
| --- | --- | --- |
| T1 | [英文](../../README.md)、[中文](../../README.zh-CN.md)，语言切换、十插件表格、首屏下载、折叠校验、平台和 DSP 限制 | 内容已完成；[头图候选](HEADER-PREVIEW.md) 待 §9.2 确认，项目起因仅留注释 |
| T2 | [About 两版、20 Topics、Website/侧栏/社交预览步骤](OWNER-REVIEW.md#t2abouttopics-与网页操作) | 可审阅方案已完成；线上设置未改，待所有者 |
| T3 | [原样 Release 模板](../releases/TEMPLATE.md)、[未来 tag 与安装包命名提案](../releases/NAMING.md) | 文件已完成；提案尚未采用，脚本与历史名称不变 |
| T4 | [六份原文归档及拟上线标题/正文](../releases/PRESENTATION-REVIEW.md) | 文件已完成且逐字符核对；线上正文与状态未改，交所有者操作 |
| T5 | 发布说明顶部双语当前入口、十个子 README 过时平台句修正；[before/after](document-edits.md) | 非争议项完成；BUILDING 开头原文保留，[候选](OWNER-REVIEW.md#t5building-源码说明候选待第-6-项确认) 待 §9.6 |
| T6 | 十个插件子 README 图标、简介、功能、限制、实现、构建与返回首页 | 完成；原技术文字和构建命令逐字符保留 |
| T7 | [CHANGELOG 草稿](drafts/CHANGELOG.md)，六份 Release 原文来源 | 草稿完成；根目录未启用，待 §9.9 |
| T8 | [bug_report.yml 草稿](drafts/issue-template/bug_report.yml)、[config.yml 草稿](drafts/issue-template/config.yml) | 草稿完成；YAML 校验通过，8 个必填字段、十插件、允许空白 Issue；尚未启用 |
| T9 | [根提示词处理选项](OWNER-REVIEW.md) | 建议完成；根文件未移动或删除，待 §9.7 |

## §10 总验收

- [x] §0.3 七条硬性约束满足：只改展示文档；无新增美术；兼容性标识不变；保留法律文件、所有 Release、tag、附件；仅提交文档分支和 Draft PR。
- [x] 首页前 25 行包含定位、十插件入口和两平台直链；1280 × 720 的 GitHub 页面首屏目视确认。
- [x] 两语言内容对等、互相能切换。两份各 17 个可见普通段落、16 个标题；三个表格各有 10 / 2 / 4 个数据行；安装步骤各十项，列表各九项。构建命令相同并逐字符来自 BUILDING。
- [x] §1.3 平台状态与 §1.6 四条限制完整保留；签名、验收范围和系统安全保护立场保留。
- [x] 两下载文件名、直链、字节数和 SHA-256 与事实卡及 GitHub API 附件元数据逐字符相符。
- [x] 已检查相对链接、图片路径和首页锚点，无缺失。模板代码围栏内的占位与历史原文代码示例按用途保留。
- [ ] 所有外链均返回 200：25 个唯一 URL 中 24 个为 200；一个原有论文 DOI 的 HEAD / GET 均返回 202。未把 202 宣称为 200，保留原技术引用。详见 [实际请求记录](LINK-CHECKS.json)。
- [x] 两语言首页正文无 §4.4 禁用工程术语。
- [x] TEMPLATE 原样逐字核对；NAMING 包含 §6.1、§6.5、§6.6 并明确待确认。
- [x] 六个 Release 原标题与原正文完整归档；原正文共 18,076 UTF-8 字节，CRLF 保留。
- [x] 六个 Release 标题和正文均有附录 A 拟稿，交所有者应用；三个旧 preview 原正文为拟正文精确字节后缀。
- [x] 拟上线正文的全部核对标记已清除。附件名、摘要、字节没有擅自修改。
- [x] 提交后重读 GitHub：六 Release 标题、正文、tag、draft/prerelease 和附件 identity/name/size/digest/URL 与初始记录一致；Latest 仍为 `v0.1.0`；main 仍为基线。
- [x] [所有者步骤与完整 §9 十二项列表](OWNER-REVIEW.md) 已准备，并列入 PR。
- [x] 没有改代码、构建脚本、安装器、法律文件、运行时图片或已发布附件。归档目录的局部 .gitattributes 仅用于保留历史正文换行。

## 视觉与事实核查

GitHub 实际渲染检查了英文和中文首屏、两方向语言切换、校验值展开/收起、十款图标和表格。十个图标原始大小均为 272 × 272；表格显示宽度约 40，保持等比。头图 A 为现有 1536 × 1024 总览，宽 720 时高 480；B 为十个 40 px 图标同一行。两候选均已渲染，最终选择仍待所有者。

本轮为展示文档修改，未重新运行插件构建、DSP、安装器或宿主测试，不把原发布验证记录扩展成新的兼容性结论。COMCTL32 后续修复尚无确认依据，旧 preview 提示按附录 A.4 兜底要求不声称已修复。
