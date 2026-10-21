# 开发日志 (DEVLOG)

> **用途**：记录每个模块 / 功能的开发完成情况，供**另两名开发人员**与**其他 AI** 快速掌握项目进度。
> **规则**：每完成一个模块 / 功能（自测通过后），**必须**在本文件追加一条记录。
> **本文件是长期文档**：动手开发前，请先读它了解当前进度与已完成模块，避免重复劳动。

## 一、填写规则
- **何时写**：模块 / 功能开发完成、且按 `docs/PLAN.md` 的 DoD 自测通过后，**立即追加**。
- **谁来写**：完成该模块的开发人员（或其 Agent）。
- **怎么改**：**只追加、不篡改历史**。若状态变化，新增一条变更记录，保留旧条目痕迹。
- **对应关系**：模块需对应 `docs/PLAN.md` 的阶段条目，或 `docs/PROJECT_CHARTER.md` 的功能编号。

## 二、字段说明
| 字段 | 说明 |
|---|---|
| 日期 | 完成日期（YYYY-MM-DD） |
| 模块 / 功能 | 名称 + 功能编号（如：资料库 #1） |
| 阶段 | 阶段一 / 二 / 三 / 四 |
| 负责人 | 开发者（3 人之一） |
| 分支 / PR | 分支名 + PR 链接 |
| 状态 | 已完成 / 进行中 / 待验收 / 已验收 |
| 自测 | 异常边界测试情况（空数据 / 断网 / 超大文件…） |
| 遗留 / 待定 | 未完成事项、已知问题 |
| 备注 | 其他说明 |

## 三、条目模板（复制使用）
```md
### YYYY-MM-DD ｜ 模块名（功能 #N）｜ 状态：待验收
- **阶段**：
- **负责人**：
- **分支 / PR**：
- **交付内容**：
- **自测**：
- **遗留 / 待定**：
- **备注**：
```

---

## 四、日志（按时间倒序，最新在最上）

### 2026-10-21 ｜ v0.2.6 热修：通义 embedding 批量上限导致 HTTP 400 ｜ 状态：已修复
- **阶段**：阶段三
- **负责人**：总代理
- **现象**：资料库导入 docx 后状态显示「向量化失败：embedding HTTP 错误 400」，与文件大小无关（12 段、171 段的文件均失败）
- **根因**：通义 `text-embedding-v4` 单次请求最多 10 条文本（官方文档），而默认 `batchSize=16`；12/171 个片段会按 16 条一批发送，首批发 12 条即被服务端拒绝。已用实际 Key 实测：12 条返回 400，10 条返回 200
- **修复**：
  - `resources/providers.json`：embedding 增加 `maxBatchSize`（通义=10）
  - `core/ai/ProviderCatalog`：解析 `maxBatchSize`
  - `core/library/LibraryController::applyEmbeddingConfig`：发起请求前按厂商上限裁剪 batchSize（不改写用户配置）
  - `core/retriever/EmbeddingService`：HTTP 错误信息附带服务端 `error.message`，后续可直接看到「批量超限」等原因
- **验证**：Release / Debug 构建通过；`ctest` **23/23 通过**；实测 10 条请求返回 200
- **影响**：已安装用户需覆盖安装本安装包，并在资料库对失败文件点「重建」

### 2026-10-21 ｜ v0.2.6 交付补充：应用图标 + 设置内帮助 + 手册联系方式 + 重新打包 ｜ 状态：已交付
- **阶段**：阶段三
- **负责人**：总代理
- **交付内容**：
  - **图标**：用用户提供的猫咪照片裁出正方形头部区域（避开右下角水印），生成圆角图标；`resources/branding/app.png`（512 RGBA）与 `app.ico`（256/128/64/48/40/32/24/20/16 多尺寸）、`MeowAi.rc`
  - **窗口图标**：`main.cpp` 增加 `QGuiApplication::setWindowIcon(":/branding/app.png")`。无边框窗口不会自动继承 exe 内嵌图标，必须显式设置
  - **exe 图标**：`CMakeLists.txt` 在 WIN32 下把 `resources/branding/MeowAi.rc` 编入目标；资源管理器、任务栏、快捷方式均读取该图标
  - **安装包图标**：`scripts/MeowAi.iss` 增加 `SetupIconFile=..\resources\branding\app.ico`；`AppPublisher` 改为 Longling，新增 `AppContact=3086865618@qq.com`
  - **设置页帮助**：`ui/PersonalizationPage.qml` 底部新增「帮助」区块 + 「帮助」按钮；弹层展示开发者 **Longling**、联系方式 **3086865618@qq.com**、快速上手指引与《用户手册》位置。弹层宽高按窗口钳制并居中，不会溢出
  - **用户手册**：`docs/USER_GUIDE.md` 头部与新增 §11「帮助与联系」写入同一开发者与联系方式
  - **交付目录**：新建 `产品/`，内含 `MeowAi-Setup-0.2.6.exe`、`USER_GUIDE.md`、`DEVELOPER_GUIDE.md`
- **自测**：Release 构建通过；`--smoke-test` 退出码 0；启动日志无 QML 告警；安装包静默安装成功（1398 个文件），安装后的 exe `--smoke-test` 退出码 0；安装包与 exe 图标均已内嵌猫咪图标
- **遗留 / 待定**：与上一条相同（安装向导为英文、未签名、尚未在未装 Qt 的干净电脑实测）
- **备注**：`/产品/` 与 `/dist/` 均已加入 `.gitignore`

### 2026-10-21 ｜ v0.2.6 总代理验收 + UI 显示修复 + Windows 安装包 + 双手册 ｜ 状态：已验收（初步）
- **阶段**：阶段三
- **负责人**：总代理
- **分支 / PR**：工作区仍基于 `v0.2.5` 分支（v0.2.6 改动未单独提交）；PR 仍未创建
- **验收结论**：
  - Debug / Release 全量构建通过；`ctest` **23/23 通过**（含 `tst_referencemode` 13 项）
  - `windeployqt` 部署后的独立目录可正常启动，不依赖开发机 Qt PATH
  - 参考资料模式关键点核对：文件级聚合召回、全文/章节/片段三级注入、预算钳制、`contextWindow` 探测与回退、仅参考语义提示词、注入可观测 chip 均与交付说明一致
- **本次修复（UI 显示不全）**：
  - `ui/MChip.qml`：新增 `maxWidth` + 文字 `elide`，长文件名不再撑破布局
  - `ui/ConversationPage.qml`：联网来源 / 资料库概览 / 来源 / 注入 四行由 `Row` 改为 `Flow` 自动换行；输入框宽度加下限；接入卡厂商/模型/API 链接/模型名由固定宽改为按窗口自适应；"当前"胶囊与附图提示文字加省略
  - `ui/ModelMenu.qml`、`ui/WebSearchPanel.qml`：弹层宽高按宿主窗口钳制，内容区改为可滚动，下方放不下时自动翻到上方（修复小窗口/高 DPI 下弹层底部被屏幕裁掉）
  - `ui/ConversationPage.qml`：小库挂载列表限高 240 并改为可滚动列表
  - `ui/LibraryPage.qml`：参数说明文字加 `width` + `wrapMode`，长参数名不再被截断
  - `ui/main.qml`：窗口初始尺寸按可用屏幕自动收缩、位置回收到屏幕内；小屏（可用宽度 <1200 或高度 <820）自动最大化；切换显示器/远程桌面时重新收回窗口（修复小屏、高 DPI、远程桌面下窗口右侧与底部超出屏幕）
  - 修复 3 类运行期 QML 告警：Flow 内违规锚点（来源/注入标签）、Connections 目标信号不存在、小库列表文字锚定到非兄弟项（长库名会压住计数/删除按钮）
  - 修复后启动日志无 `qrc:` 运行期告警
- **安装包**：新增 `scripts/MeowAi.iss`（Inno Setup 6，按用户安装、无需管理员）；`dist/MeowAi-Setup-0.2.6.exe`（约 32.7 MB，Qt 运行库 + PDFium 已内置）。静默安装验证：56 个文件落地、可执行文件存在
- **文档**：重写 `USER_GUIDE.md`（面向零基础用户，含安装、首次配置、资料库、参考资料模式、隐私、FAQ）与 `DEVELOPER_GUIDE.md`（面向运维/销售/新开发，去代码化讲原理、部署、排查、演示话术）
- **遗留 / 待定**：
  - v0.2.6 改动尚未提交到独立分支，PR 仍未创建（DoD 第 3 条）
  - 安装向导暂为英文（Inno Setup 未自带简体中文语言包）
  - 安装包未做代码签名，Windows SmartScreen 会提示"未知发布者"
  - 尚未在一台未安装 Qt 的干净电脑上实测安装包（建议发版前补做）
- **备注**：安装包输出目录 `dist/` 已加入 `.gitignore`，不进入仓库

### 2026-10-21 ｜ v0.2.6 资料库：参考资料模式（文件级召回 + 全文/章节注入）｜ 状态：待验收
- **阶段**：阶段三（资料库能力增强）
- **负责人**：开发 Agent
- **分支 / PR**：`v0.2.6`（PR 待创建）
- **交付内容**：
  - **schema v3 增量迁移**（`storage/Database.cpp`）：新增 `documents`（token 估算 / 章节来源 / 章节数）与 `sections`（章节序号 / 标题 / chunk 区间 / token）两表，全部 `IF NOT EXISTS`，与 v1/v2 完全解耦；**不复制全文**（全文仍由 `chunks` 按 `ordinal` 拼接），旧文件无元数据时自动回退片段模式，**无需重建索引**
  - **新增存储层 `storage/DocumentStore`**：documents / sections 纯 CRUD，upsert 走事务，级联删除
  - **新增 `core/retriever/BudgetCalculator`**：上下文窗口钳制纯函数（`剩余 = contextWindow - 历史 - 回答预留`，逐级取 min），窗口未知时回退 16384 并给出中文降级说明
  - **新增 `FullTextAssembler`**：按 ordinal 拼接 + 相邻片段重叠去重（`前块尾 == 后块头` 精确匹配），`assembleRange()` 供章节级复用
  - **新增 `SectionPicker`**：以「章节内最高片段相似度」排序选章，超预算章节跳过并记录
  - **新增 `InjectionPlanner`**：单文件三级决策 **全文 → 章节 → 片段**，每级降级都产出 `trace`；片段回退用 `QHash<ordinal, chunk>` 预建索引，避免逐条线性查找
  - **新增 `FileLevelRetriever`**：文件级聚合打分 `max(片段分) + 命中数 × 权重`（默认 0.05）→ 阈值过滤 → 全局预算截断；**职责严格限定为编排，不做检索**（检索仍由 `RagRetriever` 的向量 + FTS5 异步路径负责）
  - **`LibraryIndexer` / `DocumentMeta`**：索引完成后自动填充 `documents` / `sections`。章节识别按优先级：**标题**（docx `<w:pStyle>` 的 `Heading1`–`6` 或 `<w:outlineLvl>`；markdown `#`–`######`，代码围栏内的 `#` 不算）→ 工作表（xlsx）→ 幻灯片（pptx）→ 页近似（PDF / json）→ 无结构。标题识别通过 `ExtractedSegment::headingLevel` 下传，`ChunkSplitter::split()` 收集 `HeadingSpan` 后按「标题起点 → 下一标题前一片段」划分章节；标题信息**不落库、不进入注入正文**
  - **上下文窗口自动探测**：拉取模型清单时解析厂商 `/models` 响应中的 `context_length` / `context_window` / `max_context_tokens` / `max_input_tokens`（OpenRouter / Together / Groq / DeepInfra 等会返回），命中即用；主流国内厂商（DeepSeek / 通义 / 智谱 / Kimi）只返回 id，回退 `providers.json` 内置值
  - **新增配置 `fallbackContextWindow`**（默认 32768）：模型与内置清单都拿不到窗口时使用，用户可按 1M 上下文模型自行改大，无需改 JSON
  - **`ChatController`**：system 提示词改为**仅供参考语义**（明确「不必仅依据文档」「可结合自身知识或联网结果」「文档未提及须说明」），无相关文件时**不注入任何 system 消息**；新增 `libraryInjections` 可观测属性（文件名 / 注入级别 / token / 截断 / 近似标记）
  - **配置项**（`LibraryConfigStore` + `LibraryController` Q_PROPERTY + `LibraryPage.qml`）：`perFileBudget`（默认 20000）、`globalBudget`（100000）、`contextReserve`（2000）、`fileAggregateWeight`（0.05）、`fallbackContextWindow`（32768），输入即时持久化
  - **`ConversationPage.qml`**：新增「注入」chip 行，按文件展示注入级别与截断/近似状态
  - **`providers.json`**：为可核实型号补 `contextWindow`（gpt-4o 128K、qwen-plus 131K、glm-4.6 200K、glm-4.5-air 131K、kimi-k2.6 256K、claude-haiku-4.5 200K）；未公开型号不填，按 `fallbackContextWindow` 估算并在状态栏提示
- **自测**：本地 Debug 全量构建通过；`ctest` **23/23 通过**。新增 `tests/tst_referencemode.cpp`（13 项）：窗口充足 / 窗口不足 / 窗口被历史占满 / 未知窗口回退（含 1M 自定义）、重叠去重、小文件全文注入、大文件片段回退、章节元数据启用章节注入、同文件多片段只产出一条结果、markdown 标题分章、markdown 解析器识别标题层级
- **遗留 / 待定**：
  - docx 的标题识别依赖 `w:pStyle` 样式名（`Heading1`–`6` / 纯数字 `1`–`6`）与 `w:outlineLvl`；若文档使用自定义样式名且未设大纲级别，则识别不到，退回页近似或无结构
  - 预算口径统一为**入库时记录的 `tokenEst`**（全文/章节/片段一致），与 `ChunkSplitter::estimateTokens` 的实时估算可能存在轻微偏差
  - 上下文窗口探测依赖厂商 `/models` 是否返回窗口字段；DeepSeek / 通义 / 智谱 / Kimi 官方接口目前不返回，只能用内置值或 `fallbackContextWindow`
  - PR 仍未创建（DoD 第 3 条未满足）
- **备注**：预算超限时**只截断不报错**，不会因上下文过长触发 400；无 embedding Key / 空库 / 无结构 PDF 均返回明确状态且不崩溃

### 2026-10-20 ｜ 总代理验收 + 双手册交付 ｜ 状态：已验收（初步）
- **阶段**：阶段二（收尾）
- **负责人**：总代理（审查与验收，不写代码）
- **分支 / PR**：`v0.2.5`（PR 待创建）
- **交付内容**：
  - **粗略验收**：资料库 / RAG（M1–M8）、联网搜索（v0.2.3）、v0.2.5 修复已完成；`ctest` **22/22 通过**；分层与红线未见越界；文献已随本次同步（`PLAN.md` 勾选、待定项修正）。
  - **新增 [USER_GUIDE.md](./USER_GUIDE.md)**：面向最终用户的配置、对话、资料库、个性化、隐私与 FAQ。
  - **新增 [DEVELOPER_GUIDE.md](./DEVELOPER_GUIDE.md)**：架构分层、模块地图、数据库结构、各功能原理与注意事项、扩展指南、已知技术债、进度快照。
  - **更新 [README.md](./README.md)**：手册入口与读者说明（手册不替代规范，冲突以 Charter 为准）。
- **遗留 / 待定**：
  - `防崩溃自检`（PLAN 阶段二）仍为未勾选，需作为资料库完成后的补齐项。
  - **PR 仍未创建**，DoD 第 3 条（PR 可见 diff）未满足。
  - `.tmpdb/` 未纳入 `.gitignore`；`scripts/fetch-pdfium.ps1` 未提交。
  - `Skill` 调用、`UpdateManager` 仍为骨架；视频仅占位。
- **备注**：本条目为初步验收，补 PR 后可转正式验收。

### 2026-10-20 ｜ v0.2.5 修订：图片导入无反馈 + 拖拽文件入库 ｜ 状态：待验收
- **阶段**：阶段二（资料库 / RAG · M7 UI 修订）
- **负责人**：开发 Agent（待补充）
- **分支 / PR**：`v0.2.5`
- **交付内容**：
  - **修复「选择图片后没有反应」**：`FileDialog.selectedFiles` 是 `list<url>` 序列，直接透传给 `Q_INVOKABLE importFiles(const QVariantList&)` 时转换失败不会抛到界面，表现为点击确定后毫无动静。改为在 QML 侧统一走 `page.toPathList()` 转成字符串路径数组后再调用，文件与图片走同一条可靠路径。
  - **导入反馈前置且更明确**：`Library.message` 从面板底部小字移到工具栏下方，允许换行（最多 3 行）；`importFiles` 的结果文案带上目标小库名与成功数量，部分失败时同时给出「已导入 N 个 + 失败原因」，不再只显示失败串。
  - **新增拖拽入库**：中栏（文件区）整体作为 `DropArea`，接受 `text/uri-list`；拖入时显示琥珀色高亮遮罩与「松开即可导入到「当前小库」」提示，松开后复用同一导入链路（复制入库副本，原文件不动）。未选中小库时自动落到「默认资料库」。
  - **导入/关联按钮不再要求先选小库**：只要资料库可用即可点击，由 `ensureCollection()` 兜底创建默认小库，避免按钮禁用造成「点了没反应」的错觉。
  - **修复图片描述服务的悬空指针**：`rebuildCaptionService()` 原先先 `reset()` 服务再 `setCaptionService(nullptr)`，索引器会比较到已释放的指针；改为先摘指针再销毁。
  - **补齐「自动索引为什么没动静」的可操作提示**：导入后索引本来就会自动入队；缺前置条件时（图片缺视觉模型 / 文本缺 embedding），文件列表上方直接给出数量提示与「去配置」按钮，点击滚动到右侧对应设置段，配置后点「重建索引」即可补齐。
- **自测**：Debug 构建通过；离屏启动 8s 无 QML 运行期警告；检查用户数据目录确认此前图片导入确实未落库（`library_files` 为空、`library/` 为空），与「无反馈」现象一致。
- **遗留 / 待定**：
  - Qt 官方包未带 WebP 图片插件，`.webp` 可入库但图片描述会失败并停留在「待索引」；需要时再补插件或收窄过滤项。
  - 拖拽目前只接受文件，拖入文件夹不会自动转为「关联文件夹」。
- **备注**：删除小库 / 解除关联仍然只删索引，不触碰用户原文件。

### 2026-10-20 ｜ v0.2.5 资料库 / RAG · M8：隐私与成本（CostEstimator / 同意记录 / 日志脱敏）｜ 状态：待验收
- **阶段**：阶段二（资料库 / RAG · 模块 M8 / M1–M8 的第 8 步）
- **负责人**：开发 Agent（待补充）
- **分支 / PR**：`v0.2.5`
- **交付内容**：
  - **新增 `core/library/CostEstimator.{h,cpp}` + `resources/pricing.json`（内嵌 `:/pricing/pricing.json`）**：纯本地查表，**不发请求、不读文件内容**；金额 = token ÷ 1,000,000 × 每百万 token 单价，vision 先按 `tokensPerImage` 折算 token。价格表按 `providerId + modelId` 精确匹配，**未收录的模型返回 `known=false`、金额 0**（UI 显示「—」并提示只估算 token），绝不编造价格。JSON 缺失 / 损坏 / 条目缺 id 一律降级为「未加载」，不崩。
  - **成本预估接入 `LibraryController`**：按当前小库（或全部）粗略估算「首次建向量 token / 图片张数 / 每次对话注入上限 token」，分别给出 embedding、vision、chat 三行金额（货币符号随价格表），并附「仅作量级参考」说明；图内价格逐条带 `source` 字段指向官方价格页，便于核对。
  - **同意记录**：`LibraryConfigData.consentGiven / consentAt / enabled` 持久化在 `config.json` 的 library 段；首次启用、首次挂载、导入后首次索引、图片外发**都要求同意**。`revokeConsent()` 立即关闭挂载与启用，并让 indexer / caption 服务同步为「未同意」——**撤回后一个请求都不再发**，已建本地索引保留。
  - **日志脱敏**：新增链路（Indexer / Controller / CostEstimator）**不打印正文、不含 Key、不打印请求体**；错误只回结构化原因 + 面向用户的中文说明。图片外发沿用 M3 的「显式同意」闸门（`ImageCaptionService::setConsentGiven`），索引器在未同意时直接把图片标记为 `pending`，连本地转码都不做。
  - **新增 `tests/tst_cost.cpp`（4 项，离线）**：默认价格表可读、未收录模型 `known=false`、已知单价百万 token 换算正确（$0.02 / 1M）、0 token 金额为 0。
- **自测**：Debug 构建通过；`ctest -C Debug -R "tst_cost|tst_chunk|tst_providercatalog"` **3/3** 通过（0.35s）；程序离屏启动 10s 无 QML 运行期警告。
- **遗留 / 待定**：
  - 价格为**内置参考值**，厂商调价需更新 `resources/pricing.json`（版本内不联网拉取价格）。
  - 成本为**估算**：`vision` 的每图 token 为经验值，实际以厂商账单为准；chat 输出 token 未纳入「每轮上限」（当前按输入预算展示）。
- **出处（价格表）**：
  - OpenAI embeddings 价格：https://openai.com/index/new-embedding-models-and-api-updates/
  - OpenAI API 价格：https://openai.com/api/pricing/
  - 通义千问 embedding 兼容接口：https://help.aliyun.com/zh/model-studio/embedding-interfaces-compatible-with-openai
  - 智谱开放平台价格：https://open.bigmodel.cn/pricing
  - OpenAI 视觉图片 token：https://platform.openai.com/docs/guides/images-vision

### 2026-10-20 ｜ v0.2.5 资料库 / RAG · M7：索引器 + QML 资料库页（LibraryIndexer / LibraryController / LibraryPage）｜ 状态：待验收
- **阶段**：阶段二（资料库 / RAG · 模块 M7 / M1–M8 的第 7 步）
- **负责人**：开发 Agent（待补充）
- **分支 / PR**：`v0.2.5`
- **交付内容**：
  - **新增 `core/library/LibraryIndexer.{h,cpp}`（FIFO 逐文件索引，本地解析 + 可选外发）**：
    - 文本链路：`ExtractorRegistry` 本地解析 → `ChunkSplitter` 切分 → `LibraryStore::replaceChunks` → `EmbeddingService` → `SqliteVectorIndex`；状态机 `pending / indexed / needs_embedding / no_text_layer / failed / stale / unsupported`。
    - 图片链路：**未同意 / 未配置视觉模型 → `pending`，一个请求都不发**；已同意则由 `ImageCaptionService` 生成描述后与文本同路径入库；限流 / 超时 / 断网一律「待索引」并保留原因，**不误判图片无效**。
    - 降级不崩：未配置 embedding、未同意外发、向量化服务忙 → 片段照常入库并标记 `needs_embedding`；扫描件无文本层 → `no_text_layer`；视频 → `unsupported`；解析失败 → `failed`；**任何路径都不删除用户文件**。
    - 队列同一时刻只处理一个文件，避免打满厂商限流；`cancel()` 清空队列并中断在途请求，已入库片段保留。
  - **新增 `core/library/LibraryController.{h,cpp}`（QML 唯一门面）**：
    - 小库 CRUD / 选择、导入文件（复制入库）、关联文件夹、手动重建快照、解除关联、移除记录、单文件 / 整库 / 全部重建索引；
    - 检索参数（Top-K / 每文件上限 / token 预算 / 阈值 / 邻居半径 / FTS 兜底 / 切分尺寸与重叠）读写即持久化，并**同步刷新 `RagRetriever` 配置**；
    - embedding / caption 的厂商、模型、独立 Key 配置（Key 仅写 SecureStore 的 `library.embedding.<id>.api_key` / `library.caption.<id>.api_key`，缺省回退到对话厂商 Key）；自定义厂商沿用「AI 接入」里的 API 地址；
    - 同意 / 启用 / 撤回、挂载集合持久化（`mountChanged` 由 AppController 同步给 `ChatController::setMountedCollections`）；
    - 库内关键词搜索（纯本地，返回文件名 + 定位 + 摘要）；成本预估。
  - **扩展 `core/ai/ProviderCatalog` + `resources/providers.json`**：新增 `embedding`（可用性 / 模型 / 维度 / 默认）与 `vision.captionModel` 元数据，UI **完全由目录数据驱动**，不硬编码厂商或模型；`ProviderCatalog.cpp` 对缺 id 条目只跳过并记警告。
  - **新增 `ui/LibraryPage.qml`（三栏：小库 / 文件与快照 / 检索·服务·隐私·成本）**：导入与关联走系统 `FileDialog / FolderDialog`；删除小库、移除文件均二次确认，并明确提示「不删用户原文件」；状态 chip 区分「已索引 / 待建向量 / 无文本层 / 索引失败 / 内容已变化」；库内搜索与成本预估同页可见。
  - **新增 `ui/LibraryConsent.qml`**：首次启用 / 内容外发的同意层（说明哪些内容会离开本机、图片需另行同意、如何撤回），由资料库页与对话页复用。
  - **`ui/main.qml`**：新增「资料库」导航与页面、标题栏页面名；**`ui/ConversationPage.qml`**：新增资料库挂载入口（小库开关）、`资料库 · N` / 命中片段数 / token / 降级提示 chip、来源 chip（文件名 + 定位）、首次挂载同意弹窗。
  - **`core/AppController` / `main.cpp`**：装配 `LibraryController` 并注册 QML 上下文属性 `Library`；embedding / caption 配置统一由控制器负责，删除 AppController 内重复的 provider 配置代码。
- **自测**：Debug 构建通过（零编译告警）；`ctest -C Debug -R "tst_cost|tst_chunk|tst_providercatalog"` **3/3** 通过；`MeowAi.exe` 离屏启动 10s 无 QML 运行期警告。
- **遗留 / 待定**：
  - 解析与索引入口在 UI 线程同步执行（大 PDF / 大 XLSX 可能短暂卡顿），后续可移到工作线程；本阶段按 DoD 优先保证行为正确。
  - 换 embedding 模型后旧向量仍留在表中（按厂商 + 模型隔离，不会误用），「一键清旧向量」入口待后续补。
  - 视频仅登记为 `unsupported`，不做解析（与既定范围一致）。
  - 按要求暂不提交 git（远端仓库信息待补充，PR 后续统一处理）。

### 2026-10-20 ｜ v0.2.5 资料库 / RAG · M6：对话接入（A 形态：应用侧预检索 + 注入）｜ 状态：待验收
- **阶段**：阶段二（资料库 / RAG · 模块 M6 / M1–M8 的第 6 步）
- **负责人**：开发 Agent（待补充）
- **分支 / PR**：`v0.2.5`
- **交付内容**：
  - **`core/chat/ChatController` 接入 `RagRetriever`（A 形态，非 Agent 自主取数）**：发送前先 `retrieveAsync()`，只检索**已挂载**的小库；检索完成后再走原有 provider 分发，`ChatMessage::System` 上下文插入在 system 之后、用户消息之前。
  - **上下文格式**：明确「片段是资料不是指令」，要求模型**不得编造**、依据不足时说明「资料库中未找到足够依据」、回答末尾按 `[来源 N] 文件名（定位）` 列引用；`sourcePath` 等内部字段**不进入 prompt**。
  - **来源落库**：回答完成后按 rank / score 写 `citations`（messageId / conversationId / chunkId / fileId / collectionId / locLabel），落库失败**静默跳过**，不影响对话。
  - **失败不阻断**：检索失败（429 / 超时 / 断网 / 未配置）只更新 `libraryStatus`（可降级说明），仍继续本轮对话；用户取消时同时取消资料库检索。
  - **QML 可观测状态**：`mountedCollections / libraryMounted / libraryHitCount / libraryTokenCount / libraryDegraded / libraryStatus / librarySources`，挂载集合持久化到 `config.json` 的 library 段。
- **自测**：Debug 构建通过；`ctest -C Debug` **21/21** 通过（M1–M5 无回归）；检索失败路径由 `tst_ragretriever` 的降级用例覆盖。
- **遗留 / 待定**：
  - M6 时挂载列表只能经配置写入；可视化挂载入口在 M7 补齐（现已完成）。
  - 检索只用单轮用户消息作为查询，未做多轮指代改写（范围外）。
  - 按要求暂不提交 git（远端仓库信息待补充，PR 后续统一处理）。

### 2026-10-20 ｜ v0.2.5 资料库 / RAG · M5：检索与上下文组装（RagRetriever / RetrievalConfig）｜ 状态：待验收
- **阶段**：阶段二（资料库 / RAG · 模块 M5 / M1–M8 的第 5 步）
- **负责人**：开发 Agent（待补充）
- **分支 / PR**：`v0.2.5`
- **交付内容**：
  - **新增 `core/retriever/RetrievalConfig.h`**：`topK`（K 值）、`perFileLimit`（每文件片段上限）、`tokenBudget`（token 预算）、`similarityThreshold`（相似度阈值）、`neighborRadius`（邻居扩展半径）、`ftsFallback`（关键词兜底开关）、`maxCandidates`（粗排上限）。三项用户可配置项**改动后行为随之改变**（有单测证明）。
  - **新增 `core/retriever/RagRetriever.{h,cpp}`（向量 + FTS5 兜底 + 上限 + 邻居 + 阈值 + 预算）**：
    - **检索顺序**：① 候选召回（向量过阈值 / 关键词按词覆盖度）→ ② **每文件上限截断** → ③ **邻居扩展** → ④ **token 预算截断**。每文件上限在扩展**之前**生效，否则单个大文件仍会占满预算；邻居扩展按 `ordinal` 取前后各 `neighborRadius` 段（`LibraryStore::chunksInOrdinalRange`），保持「前邻居 → 命中 → 后邻居」的阅读顺序，邻居分数为命中分 ×0.85（仅用于排序展示，不改变定位信息）。
    - **向量路径（异步 `retrieveAsync()`）**：复用 M4 的 `EmbeddingService` 向量化查询（经 `NetworkTransport`），与 `SqliteVectorIndex` 中的向量逐条算**余弦相似度**；维度不同 / 零范数 / 低于阈值一律跳过；结果按分数降序并截断到 `maxCandidates`。
    - **FTS5 关键词兜底（同步 `retrieve()` 实现 `DocumentRetriever`）**：查询按空白与中英文标点拆词，逐词调用 `LibraryStore::searchChunkIds` 并按**词覆盖度**累计打分（`0.5 + 0.5 × 覆盖率`，余弦阈值不误杀关键词分）；**同步接口绝不联网**（单测断言零请求）。
    - **合并**：向量命中不足 K 时，用关键词结果补齐（去重 + 同样受每文件上限约束，`mode = hybrid`），符合「先粗排后按分数截断」。
    - **降级不崩**：embedding 未配置 / 未建模型 → 直接关键词（`degraded = true` + 中文 `note`，**零请求**）；embedding 失败（429 / 超时 / 断网 / 401）→ 按 `ftsFallback` 降级并保留原因；`ftsFallback = false` 时结构化 `failed`。
    - **挂载范围**：`setMountedCollections()` 为空时**不检索**（避免把未挂载库内容注入对话）；向量与关键词都只覆盖挂载库的文件，`RetrievedChunk::collectionId` 标注来源库。
  - **修改 `core/retriever/DocumentRetriever.h`**（按已锁定决策 2 的破坏性扩展）：`RetrievedChunk` 增加 `chunkId / fileId / collectionId / fileName / sourcePath / ordinal / locKind / locLabel / locPage / locSheet / locRowFrom / locRowTo / tokenEst`；`sourcePath` 仅供内部核对，**不注入 prompt**。`NullRetriever` 无需改动即可编译（新增字段均有默认值）。
  - **新增 `tests/tst_ragretriever.cpp`（11 项，真实 SQLite + `FakeNetworkTransport`，全程离线）**：关键词兜底保留文件/定位元数据、每文件上限（2 → 4 条随配置变化）、邻居扩展（ordinal 0/1/2）、token 预算（250 → ≤2 条，5000 → 4 条）、挂载库过滤、未挂载零结果、向量阈值过滤（cos 0 被丢弃）、hybrid 补齐、429 降级关键词、关闭兜底后结构化失败、同步接口零网络请求。
- **自测**：Debug 构建通过（零编译告警）；`ctest -C Debug` **21/21** 通过（新增 `tst_ragretriever`），M1–M4 用例无回归。
- **遗留 / 待定**：
  - `RagRetriever` 的异步结果尚未接入对话（M6）；UI 展示（来源 chip / 命中数 / token）在 M7。
  - 关键词兜底依赖 `searchChunkIds` 的 FTS5 短语匹配，拆词后按覆盖度排序；**未做 rerank**（范围外）。
  - 向量检索为内存全量余弦（当前规模足够）；后端可替换接口已在 M4 留好。
  - 按要求暂不提交 git（远端仓库信息待补充，PR 后续统一处理）。

### 2026-10-20 ｜ v0.2.5 资料库 / RAG · M4：切分与向量（ChunkSplitter / EmbeddingService / SqliteVectorIndex）｜ 状态：待验收
- **阶段**：阶段二（资料库 / RAG · 模块 M4 / M1–M8 的第 4 步）
- **负责人**：开发 Agent（待补充）
- **分支 / PR**：`v0.2.5`
- **交付内容**：
  - **新增 `core/retriever/ChunkSplitter.{h,cpp}`（纯本地切分，无网络、无持久化）**：
    - 输入是解析层产出的 `ExtractedSegment`，输出可直接交给 `LibraryStore::replaceChunks` 的 `ChunkRecord`：**定位元数据逐条保留**（`loc_kind / loc_label / loc_page / loc_sheet / loc_row_from / loc_row_to`），`ordinal` 在文件内**连续递增**（邻居扩展依赖连续性）。
    - **文本类**：按字符窗口切分，切点优先落在**段落 / 句末**（`\n\n`、`\n`、`。！？；` 等），下限取目标尺寸的 3/5，避免切点过早导致片段过碎；相邻块重叠 `overlap` 字符。
    - **表格类（`loc_kind = row | sheet`）按行切分并**每块重复表头 + 分隔行**：脱离表头的 `| 1200万 |` 无法理解行列含义，重复表头后每块仍可独立读懂；行区间按数据行偏移**重算**（`loc_row_from / loc_row_to` 与定位串同步更新，XLSX 保留工作表名，形如「Sheet1 第 12-18 行」）。
    - **两项参数都可配置且行为随之改变**（DoD 第 5 条）：`chunkSize`（下限 40 字符）与 `overlap`（不超过目标尺寸的一半，防止原地打转）；`estimateTokens()` 提供保守 token 估算（CJK 1 字 ≈ 1 token，其余 4 字符 ≈ 1 token），用于 token 预算与成本预估。
    - 空 / 纯空白片段直接跳过，不产生零字块。
  - **新增 `core/retriever/VectorIndex.h` + `storage/SqliteVectorIndex.{h,cpp}`（向量后端，接口可替换）**：
    - 存储格式为 **float32 小端**连续字节，Android 与桌面读写同一份字节；按「厂商 + 模型」隔离检索。
    - 维度校验**整批拒绝**：空向量或批内维度不一致时一条都不写（`false + error`），杜绝半批脏数据导致检索期维度错位；写入走事务。
    - `removeByFile()` 按文件清理（换 embedding 模型必须重建，理由见下），`vectors()` / `count()` 供 M5 检索与「是否已建向量」状态展示。
    - **schema 事实**：`chunk_vectors` 以 `chunk_id` 为主键 → **一个片段只有一条向量**；换模型写入同一片段是**覆盖**而非并存，故换模型时必须先 `removeByFile` 再重建，DEVLOG 在此明确记录以免后续误判。
  - **新增 `core/retriever/EmbeddingService.{h,cpp}`（异步批量向量化）**：
    - 协议口径：**OpenAI 兼容的 `POST /embeddings`**（`{"model","input","dimensions"}`，`data[].embedding` + `usage.total_tokens`）；国内主流厂商（通义千问兼容模式 / 智谱 / 硅基流动等）与 OpenAI 同一套格式，因此**复用同一实现**，靠 `ProviderConfig` 切换 `baseUrl` / 鉴权 / 额外头，**不为每家厂商单独写实现**。
    - **网络一律经 `NetworkTransport` 发出**（与 `core/web/HttpWebSearchProvider` 同一口径），URL 与请求头复用 providers 层的 `ProviderUtils::endpointUrl / requestHeaders`；端点路径、鉴权与返回结构**逐项查官方文档核实**（出处见文末），不凭记忆猜测。
    - 按 `LibraryEmbeddingConfig::batchSize` **串行切批**，结果按响应 `index` **归位到输入顺序**（服务端乱序返回也不串位）；`usage` 跨批**累加**；`dimensions > 0` 时在请求里显式下发并**回验维度**。
    - **延迟索引（降级不崩）**：未配置 Key / 模型 / 地址时 `isReady()` 为 false，`embed()` 直接回 `ChatError::NotConfigured` 且**一个请求都不发**；上层据此把文件标记为 `needs_embedding`，配好服务后重建即可。
    - Anthropic 明确返回 `NotConfigured` + 中文提示（其无 embedding 接口，需单独配置一个支持 `/embeddings` 的服务）。
    - 失败**结构化映射**：401/403 → `Unauthorized`、429 → `RateLimited`、超时 → `Timeout`、断网 → `NetworkUnavailable`、响应非法 / 维度不符 / 数量不符 → `InvalidResponse`；**绝不把限流或断网误判为「密钥无效」**。
    - **安全**：Key 只来自调用方传入的 `ProviderConfig`，本类不读 SecureStore、不落盘、不打印日志；错误信息**不含 Key / 请求体 / 文件内容**（URL 里也不出现 Key）；`cancel()` 中断在途请求并回 `Cancelled`。
  - **新增 `tests/tst_chunk.cpp`（14 项）+ `tests/tst_embedding.cpp`（12 项）**，`tests/CMakeLists.txt` 登记到 `tst_chunk` / `tst_embedding`；`CMakeLists.txt` 新增 `ChunkSplitter.cpp` / `EmbeddingService.cpp` / `SqliteVectorIndex.cpp`。
    - 切分用例：token 估算、尺寸与重叠生效、**参数改动行为随之改变**（`chunkSize` 变小片段变多、`overlap = 0` 时相邻片段不再重叠）、定位元数据与 ordinal 保留、表格按行切**每块重复表头**且行区间连续、XLSX 定位串保留工作表名、空片段跳过、图片定位串保留。
    - 向量用例（真实 SQLite，临时目录）：无数据库安全失败、小端 float32 往返（1.0f 字节为 `00 00 80 3F`，长度非 4 倍数视为损坏不崩）、**维度不符整批拒绝**、换模型覆盖语义、按文件清理。
    - embedding 用例（**全程离线，网络经 `FakeNetworkTransport` 注入**）：未配置 Key **零请求**、Anthropic 无端点零请求、分批串行 + 乱序 index 归位 + usage 累加、请求体与鉴权头逐项核对（含「密钥不进 URL」）、401 → `Unauthorized` 且**首个批次失败即停**、429 → `RateLimited`、超时 / 断网映射、维度不符与数量不符 → `InvalidResponse`、空输入不发请求、`cancel()` 中断并回 `Cancelled`；断言中额外校验**错误信息不含 Key**。
- **自测**：Debug 构建通过（零编译告警）；`ctest -C Debug` **20/20** 通过（新增 `tst_chunk` / `tst_embedding`）；原有 18 个用例无回归。
- **遗留 / 待定**：
  - **入库与检索未接**：本步只交付「切分 + 向量读写 + 向量化服务」，把文件真正标记为已索引 / 待索引（`LibraryIndexer`）与 **M5 `RagRetriever`**（向量 + FTS5 兜底 + 每文件上限 + 邻居扩展 + 阈值 + token 预算）属后续模块。
  - **`RetrievalConfig.h`（K / 每文件上限 / token 预算三项可配置）属 M5**。
  - **换模型重建**：目前靠调用方显式 `removeByFile` + 重建实现，UI 侧一键重建入口在 M7。
  - **无 embedding 时的降级展示**（「待索引」状态与关键词兜底提示）在 M5 / M7。
  - 按要求暂不提交 git（远端仓库信息待补充，PR 后续统一处理）。
- **出处（embedding 端点 / 鉴权 / 参数 / 返回结构，均为官方文档）**：
  - OpenAI：`POST /v1/embeddings`，`Bearer`，`input` / `dimensions`，返回 `data[].embedding` 与 `usage.total_tokens`：https://platform.openai.com/docs/api-reference/embeddings
  - 阿里云百炼（OpenAI 兼容）：`POST /compatible-mode/v1/embeddings`，`text-embedding-v3/v4`（v4 支持 2048→64 维，单行 8192 token、单次最多 10 行）：https://help.aliyun.com/zh/model-studio/embedding-interfaces-compatible-with-openai
  - 智谱：`POST https://open.bigmodel.cn/api/paas/v4/embeddings`，`embedding-3`（默认 2048 维，单次最多 64 条）：https://docs.bigmodel.cn/cn/guide/models/embedding/embedding-3
  - 硅基流动：`POST https://api.siliconflow.cn/v1/embeddings`（bge-m3 等，单次条数与维度以模型页为准）：https://docs.siliconflow.cn/cn/api-reference/embeddings/create-embeddings
  - 无 embedding 接口（需另配服务）：Kimi https://platform.kimi.com/docs/api/overview ；DeepSeek https://api-docs.deepseek.com/quick_start/pricing ；Anthropic 建议使用 Voyage AI：https://docs.anthropic.com/en/docs/build-with-claude/embeddings
  - Qt `QDataStream`（`setByteOrder` / `setFloatingPointPrecision` 保证 float32 小端跨平台一致）：https://doc.qt.io/qt-6/qdatastream.html

### 2026-10-20 ｜ v0.2.5 资料库 / RAG · M3-d：图片类（本地转码 + 视觉 caption 服务；视频按用户指示不实现）｜ 状态：待验收
- **阶段**：阶段二（资料库 / RAG · 模块 M3-d / M3 拆 4 小步的最后一步）
- **负责人**：开发 Agent（待补充）
- **分支 / PR**：`v0.2.5`
- **交付内容**：
  - **`Extractor.h` 契约扩展（图片类口径）**：新增 `ImageCaptionTask`（`bytes` / `mimeType` / `width` / `height` / `sourcePath`）与 `ExtractionResult::captionTasks`。
    - 为什么不能把 caption 塞进同步 `extract()`：解析层契约是「本地同步、绝不联网」，若让 `extract()` 去调视觉模型，同步接口就被迫偷偷联网、破坏分层。因此图片类**本地只产出「已转码、可外发」的任务**，`segments` 为空（**预期而非缺陷**：入库文本必须由视觉模型给出，本地不产文本、不臆造）。
    - 「图片序号」**不由解析层赋予**（解析层只负责单张图片本身，不参与排序），由上层在 caption 阶段给出，避免同一条记录两处编号互相打架。
  - **新增 `core/library/extract/ImageTranscoder.{h,cpp}`（纯本地）**：校验（存在 / 普通文件 / 未超 `kMaxExtractBytes`）→ 解码（`setAutoTransform(true)` 按 **EXIF 方向校正**，避免「躺倒」的图影响视觉判断）→ 长边压缩（默认 **1536**，**只缩不放**）→ 统一编码（**优先 PNG**，图文/截图无损不糊；失败回落 JPEG），BMP / GIF / WEBP 一律归一到外发通用格式，绝不原样外发 BMP。
    - 防「解压炸弹」：源图像素超过 **6400 万**直接拒绝（约 256 MB 位图），不让一张超大图把内存吃满。
    - 失败一律 `false + error`：文件不存在 / 编码不可读 / 解码失败 / 转码失败各自区分；**不可读时的错误里列出运行库当前可读格式**，便于现场自查 webp 等格式是否缺 `imageformats` 插件（GIF 取首帧，不产多片段）。
    - 本类**不联网、不写盘、不改动原图**。
  - **新增 `core/library/extract/ImageCaptionExtractor.{h,cpp}`（`id = "image"`）**：经**同一注册点**接入（与文本类、视频预留点一致），`maxEdge` 由上层按 `LibraryCaptionConfig::maxEdge` 注入，保证「转码口径」只有一处；静态 `segmentFromCaption(序号, 描述)` 负责把描述回填为入库片段（`loc_kind = image`、定位串「图片 #N」），定位元数据只在解析层定义一次。
  - **新增 `core/library/ImageCaptionService.{h,cpp}`（异步 caption 服务）**：
    - **复用既有 `AiProvider::sendChat`**，不新建视觉 Provider、不新增厂商协议实现；provider 由调用方（M4 索引器 / M7 控制器）用 `ProviderFactory` 建好、按 **caption 的 providerId / modelId** 配置后注入，本服务**不读 SecureStore、不碰持久化、不落盘**。
    - **一次调用同时拿「描述」与「强制文字提取」**（锁定决策 4，不额外再调 OCR/单独接口）：`promptProfile` 提供 `default`（先整体描述，再**逐字完整抄录**图中文字并保留分行与表格行列、不许翻译概括省略，最后给 3-8 个检索关键词）与 `text-heavy`（扫描件/表格照片/截图：**抄录优先**，描述从简）；两档都强制要求「无文字时写『无可见文字』」，杜绝模型静默跳过。
    - `stream = false` 一次拿完整文本；`temperature = 0.2`（描述/抄录属事实性任务，压低随机性）；`maxTokens` / `maxEdge` / `promptProfile` 全部来自 `LibraryCaptionConfig`。
    - **图片外发必须用户显式同意（硬性约束）**：`consentGiven` 默认 **false**，未置位时**一个请求都不发**，直接回 `ChatError::ImagesNotConfirmed`「该图片未上传，已标记为待索引」；置位后才在请求上带 `imagesConfirmed = true`。
    - **降级不崩、且不误判**：provider 未配置或未指定 caption 模型 → `NotConfigured`（上层配好视觉模型后重试）；模型返回空文本 → `EmptyResponse`；provider 的 429 / 超时 / 断网 / 401 **原样结构化回传**，**绝不把限流或断网当成「图片无效」**；本地转码失败用 `Unknown` 但 message 明确写「本地转码失败」以免被当作网络故障。
    - FIFO 队列逐张处理（provider 单实例同一时刻只承载一个在途请求），**单张失败不阻断后续**；`cancelAll()` 清空队列并中断在途；`queueDrained()` 供批量收尾。
    - 队列推进用 `QMetaObject::invokeMethod` **排队调用而非直接递归**：即使 provider 同轮回信号，栈深也不随队列长度增长。
    - 全程不打印图片内容、API Key 或完整请求体。
  - **`ExtractorRegistry` 登记 `image`**（顺序：plain → csv → json → pdf → docx → xlsx → pptx → **image** → video）；`forPath("照片.jpg")` 由「返回 `nullptr`（暂不支持）」变为命中 `id = "image"`；`CMakeLists.txt` 新增 `ImageCaptionService.cpp`、`ImageCaptionExtractor.cpp`、`ImageTranscoder.cpp`。
  - **视频按本次指示不实现**：`VideoExtractor` 仅保留既有注册点（返回 false +「视频解析本次未实现」），不新增任何视频解析代码。
  - **新增 `tests/tst_imagecapture.cpp`（11 用例 / 13 项）+ `tests/CMakeLists.txt` 登记**：图片夹具用 `QImage` **现场生成**（PNG / JPG / BMP），caption 用**假 Provider**（零网络、零 Key、按脚本同步回信号），因此不提交任何二进制夹具、不依赖真实厂商。用例覆盖：长边压缩且等比（3000×1000 → 1536×512，产物可解码复验）、**只缩不放**（800×600 保持）、BMP 归一到 PNG/JPEG、文件不存在/内容非图片分别报错、注册点路由到 `image`、`maxEdge` 注入生效、`图片 #N` 定位、**未同意时请求数为 0**、单次请求形态逐项核对（model / stream=false / imagesConfirmed=true / maxTokens / 单条消息单张图片 / **外发字节解码为 512×384 而原图是 1200×900** / 提示词含「逐字完整抄录」与「关键词」）、`text-heavy` 档提示词、未指定模型与 provider 未配置均为 `NotConfigured`、**429 原样回传且后续图片仍能成功**（单张失败不阻断队列）；每个用例末尾用 `isIdle()` 断言队列确实收敛，避免「悄悄卡在处理中」被漏测。
  - **`tests/tst_extractors.cpp` 同步更新**：`ids()` 含 `image`；图片断言由「`nullptr`」改为「命中 `id = image`」。
- **自测**：Debug 构建通过（零编译告警）；`tst_imagecapture` **13 项全 PASS（0 skipped）**；`tst_extractors` **19 项仍全 PASS**（PDFium 路径无回归）；`ctest -C Debug` **18/18** 通过；`MeowAi.exe --smoke-test` 退出码 **0**。
- **遗留 / 待定**：
  - **入库与 UI 未接**：本步只交付「本地转码 + caption 服务」，图片真正落库（写 `chunks` / 建向量）与首次同意弹窗、来源 chip 属 M4 / M7。
  - **未接 OCR 专用模型**：扫描件靠同一提示词「强制逐字抄录」覆盖；OCR 不在 `docs/PROMPT.md` 本次范围内。
  - **`resources/pricing.json` 视觉单价**（图转 token 的成本口径）属 M8。
  - **GIF 只取首帧**、不产多片段；多帧逐帧描述未做。
  - **视频不实现**（用户指示），注册点保留。
  - 按要求暂不提交 git（远端仓库信息待补充，PR 后续统一处理）。
- **出处**：
  - Qt `QImageReader`（`setAutoTransform` EXIF 校正 / `canRead` / `supportedImageFormats` / `size` / `errorString`）：https://doc.qt.io/qt-6/qimagereader.html
  - Qt `QImage`（`scaled` 等比缩放 / `save` 编码为 PNG、JPEG）：https://doc.qt.io/qt-6/qimage.html
  - Anthropic 视觉输入（`source.type = base64`，图片与文本同一条消息）：https://docs.anthropic.com/en/docs/build-with-claude/vision
  - 通义千问（阿里云百炼）视觉理解模型：https://help.aliyun.com/zh/model-studio/vision
  - Kimi（月之暗面）视觉模型：https://platform.moonshot.cn/docs/guide/use-kimi-vision-model
  - 智谱 GLM 视觉模型（GLM-4V-Flash，图像理解/字幕）：https://docs.bigmodel.cn/cn/guide/models/free/glm-4v-flash ；模型总览（视觉理解 vs 视觉推理）：https://docs.bigmodel.cn/cn/guide/start/model-overview
  - OpenAI 兼容多模态请求格式（`image_url` + base64 data URL）沿用本仓库 **v0.2.3 条目已核对**的官方出处；`supportsVision` 能力标记同源。

### 2026-10-20 ｜ v0.2.5 资料库 / RAG · M3-c：PDF 文本层解析（PDFium，本地放置 + 仓库忽略）｜ 状态：待验收
- **阶段**：阶段二（资料库 / RAG · 模块 M3-c / M3 拆 4 小步的第 3 步）
- **负责人**：开发 Agent（待补充）
- **分支 / PR**：`v0.2.5`
- **交付内容**：
  - **新增 `scripts/fetch-pdfium.ps1`（抓取脚本，入库）**：按**固定 tag** `chromium/8111` 拉取官方预编译产物 `pdfium-win-x64.tgz`，解压到 `third_party/pdfium/`，并校验 `include/fpdfview.h`、`include/fpdf_text.h`、`lib/pdfium.dll.lib`、`bin/pdfium.dll` 四项齐全。
    - 先按 tag 解析资产再下载，且走 **API 资产端点**（而非 `github.com` 浏览器直链）：本机实测 `github.com:443` 不可达、`api.github.com` / `release-assets.githubusercontent.com` 可达，走 API 端点可绕开 302 跳转到被墙域名的失败。**不使用编辑器/脚本内的相对路径假设**，默认目标目录由脚本自身位置推导仓库根。
    - **不下载到仓库内部任何会被提交的位置**；产物为解压即用，不改 CMake 缓存。
  - **`.gitignore` 新增 `third_party/pdfium/`（本地放置 + 仓库忽略）**：PDFium 二进制（含 `pdfium.dll` 约 7.6 MB）**不进仓库、不进 PR diff**；PR 中只出现脚本 + CMake 改动。
  - **`CMakeLists.txt` 条件启用**：目录存在（三个必要文件齐全）→ `add_library(pdfium SHARED IMPORTED GLOBAL)` 并链接 `MeowAiLib`、定义 `MEOWAI_HAVE_PDFIUM`（**PUBLIC**，便于测试按有/无两种口径切换断言）；**缺失则不定义宏、不报错**，编译降级实现。
    - 同时为 `MeowAi` 与 `tst_extractors` 加 `POST_BUILD` 的 `copy_if_different`：Windows 运行期不查 PATH 之外的库路径，`pdfium.dll` 必须与可执行文件同目录。
    - 配置期打印明确状态：「PDFium 已启用（PDF 文本层解析）」或「未检测到：PDF 解析降级为『未启用』（抓取：pwsh scripts/fetch-pdfium.ps1）」。
  - **新增 `core/library/extract/PdfTextExtractor.{h,cpp}`（`id = "pdf"`）**：
    - 抽取范围仅限**文本层**：`FPDF_LoadMemDocument64` → 逐页 `FPDF_LoadPage` + `FPDFText_LoadPage` → `FPDFText_CountChars` / `FPDFText_GetText` → 每页一个片段，`loc_kind = page` + `loc_page`，定位串「第 N 页」。
    - **走内存加载而非按路径加载**：避免含中文/非 ASCII 的路径在不同平台被当作本地编码解析而失败（本次范围内用户文件多为中文名）。
    - 文本层返回 UTF-16：`QString::fromUtf16` 解码后把 `\r\n`、孤立 `\r` 归一为 `\n`，再逐行 `trimmed` 并丢弃空行（文本层常带整行空白），压缩后再产出片段，避免脏片段进入后续切分。
    - **失败原因逐一区分、绝不静默返回空结果**：`FPDF_GetLastError()` 映射为中文原因（文件损坏 / 非 PDF 格式 / **已加密需口令** / 不支持的加密方案 / 页面无法解析）；整册无文本层 → 报「该 PDF 无文本层：N 页均未解析出文字（可能为扫描件或纯图片 PDF，需 OCR）」；全页解析失败 → 报「文件可能已损坏」；页数为 0 → 报「不包含任何页面」。
    - 无 PDFium 构建时：`supports()` **仍认领 pdf**（保证注册表路由一致），`extract()` 返回 `false` + 「PDF 支持未启用：…运行 pwsh scripts/fetch-pdfium.ps1 后重新配置构建即可启用文本层解析」→ 由上层落成 `no_text_layer`，**其它格式解析完全不受影响**。
    - 沿用 `kMaxExtractBytes`（64 MB）上限；只读，不做网络请求，**不做 OCR**（扫描件不在本次范围）。
    - PDFium 全局库用**函数内静态对象只初始化一次**（`FPDF_InitLibrary`），静态析构时 `FPDF_DestroyLibrary`，避免进程退出时泄漏。
  - **`ExtractorRegistry` 登记 `pdf`**（顺序：plain → csv → json → **pdf** → docx → xlsx → pptx → video），上层仍只用 `forPath()`；`CMakeLists.txt` 的 `core/library` 清单新增 `PdfTextExtractor.cpp`。
  - **测试 `tests/tst_extractors.cpp` 扩充到 17 用例（含 init/cleanup 共 19 项）**：
    - **新增测试专用 PDF 写入器 `pdfBytes()`**：手写**最小合法 PDF**（Catalog / Pages / Font / 每页「内容流 + 页对象」），**交叉引用表偏移在生成时精确计算**；正文用 Base14 `Helvetica` + `Tj`，无需嵌入字体。传空字符串的页只放图形指令（`re f`）→ **模拟扫描件（无文本层）**。因此**无需预置任何二进制夹具**即可覆盖真实 PDFium 解析路径。
    - `pdfExtractsPageTextWithLocators`：两页 → 2 片段、`loc_kind = page`、`loc_page` 1/2、定位串「第 1 页 / 第 2 页」、第 1 页文本精确等于 `Boiler Room Notes`、第 2 页含 `Pump 2 checks weekly`；**无 PDFium 构建时**该用例自动改判为「降级口径」：返回 false、错误含「未启用」、片段为空。
    - `pdfReportsMissingTextLayerSeparately`：图形页 PDF → 报「无文本层」；非 PDF 内容 → 报「打开失败」且**断言不出现「无文本层」字样**（证明两类失败被区分，不会互相误判）；无 PDFium 构建时 `QSKIP`。
    - `registryRoutesByExtension` 更新：`ids()` 含 `pdf`，`报告.pdf` 命中 `id = "pdf"`；`image`（jpg）仍**明确返回 `nullptr`**（M3-d 待补，上层须给「暂不支持」提示）。
- **自测**：Debug 构建通过（零编译告警，配置期确认「PDFium 已启用」）；`tst_extractors` **19 项全 PASS（0 skipped）**；`ctest -C Debug` **17/17** 通过（M1 / M2 / M3-a / M3-b 无回归）；`MeowAi.exe --smoke-test` 退出码 **0**；`build/Debug/` 下 `pdfium.dll` 与 `MeowAi.exe` / `tst_extractors.exe` 同目录（POST_BUILD 拷贝生效）。
  - 上游产物实测：`third_party/pdfium/VERSION` 为 `MAJOR=158 / MINOR=0 / BUILD=8111 / PATCH=0`，即锁定的 `chromium/8111`。
- **遗留 / 待定**：
  - **M3-d**：`ImageCaptionExtractor`（图片走 providers 层视觉模型，需 `providers.json` 视觉配置 + AiProvider 多模态支持）+ `ImageTranscoder`（长边压缩 / BMP 转码）+ 视频真实现。
  - 扫描件（无文本层）**不做 OCR**，只给出明确原因；OCR 不在 `docs/PROMPT.md` 本次范围内。
  - 未抽取 PDF 的书签/目录、表单字段、注释与批注；PDFium 未启用的构建仍可编译但 pdf 解析降级（属**预期行为**，非缺陷）。
  - 按要求暂不提交 git（远端仓库信息待补充，PR 后续统一处理）。
- **出处**：
  - PDFium 官方头文件与 API（`FPDF_InitLibrary` / `FPDF_LoadMemDocument64` / `FPDF_GetPageCount` / `FPDF_LoadPage` / `FPDF_ClosePage` / `FPDF_GetLastError` 及错误码 `FPDF_ERR_*`）：https://pdfium.googlesource.com/pdfium/+/refs/heads/main/public/fpdfview.h
  - PDFium 文本层 API（`FPDFText_LoadPage` / `FPDFText_CountChars` / `FPDFText_GetText`（缓冲区需 `count + 1`、返回值含终止 NUL）/ `FPDFText_ClosePage`）：https://pdfium.googlesource.com/pdfium/+/refs/heads/main/public/fpdf_text.h
  - PDFium 许可证（**BSD-3-Clause**，允许随二进制再分发）：https://pdfium.googlesource.com/pdfium/+/refs/heads/main/LICENSE
  - 预编译产物来源与版本（`bblanchon/pdfium-binaries`，Windows x64 包内为 `bin/pdfium.dll` + `lib/pdfium.dll.lib` + `include/`）：https://github.com/bblanchon/pdfium-binaries

### 2026-10-20 ｜ v0.2.5 资料库 / RAG · M3-b：Office 三件套解析（docx / xlsx / pptx，零新增第三方依赖）｜ 状态：待验收
- **阶段**：阶段二（资料库 / RAG · 模块 M3-b / M3 拆 4 小步的第 2 步）
- **负责人**：开发 Agent（待补充）
- **分支 / PR**：`v0.2.5`
- **交付内容**：
  - **新增 `core/library/extract/OfficeXmlUtils.{h,cpp}`**：Office 解包工具，**自研最小 ZIP 读取器**。
    - `OfficeZipArchive`：整包读入 → **从尾部回溯 EOCD**（签名 `0x06054b50`，并校验「注释长度 + 22 必然到文件尾」以避免误命中数据区同名字节串）→ 遍历**中央目录**（`0x02014b50`）建立条目表 → 取条目时按**本地头**（`0x04034b50`）重算数据起点（本地头与中央目录的额外字段长度可能不同）。
    - 仅支持本次范围的 **stored(0) / deflate(8)**，遇到其它压缩方式给出明确错误；**不处理 ZIP64**（本次范围外）。
    - 压缩流走 **Qt 自带的 zlib**：`#include <QtZlib/zlib.h>`，Qt 内部已把符号前缀化为 `z_*`（`z_inflateInit2` / `z_inflate` / `z_inflateEnd`），由 `Qt6Core` 导出 —— 因此**不引入任何新的第三方依赖**，也不用 Qt 私有 API。
    - 大小上限沿用 `kMaxExtractBytes`；**只读不写**，绝不修改用户原始 Office 文件。
    - 附带 `officeElementName()` / `officeAttribute()`：按**本地名**匹配（`w:p` → `p`、`r:id` → `id`），因此**无论是否开启命名空间处理都能稳定命中 OOXML 元素**；`collapseXmlWhitespace()` 统一折叠空白。
  - **新增 `DocxExtractor`**（`docx`）：解 `word/document.xml`，按**段落**产出片段（`loc_kind = para`，定位串「第 N 段」）；表格按**行**产出片段、单元格以 ` | ` 连接（保留行列含义）。
    - **只收集 `<w:t>` 内字符**：域代码（`w:instrText`）与修订删除文本（`w:delText`）不会混入正文。
    - 空段落（`<w:p/>`）不产出片段；单张表格行内的 `w:p` 不会误当作正文段落。
  - **新增 `XlsxExtractor`**（`xlsx`）：解 `xl/workbook.xml` + `xl/_rels/workbook.xml.rels` + `xl/sharedStrings.xml` + `xl/worksheets/sheetN.xml`。
    - 按 `workbook.xml` 中 `<sheet>` 的**出现顺序**逐表处理（即用户看到的表序）；关系路径支持包内绝对（`/xl/...`）与相对（`worksheets/...`，含 `..` 归一）两种写法。
    - 单元格取值覆盖 **共享字符串（`t="s"`）/ 内联字符串（`t="inlineStr"`）/ 公式结果（`t="str"`）/ 数值（无 `t`）**；**按列字母定位**，因此中间空列不会被挤掉（列对齐保留）。
    - 转 **Markdown 表格**（与 CSV 口径一致：首个非空行为表头、每片段最多 `kTableRowsPerSegment` 行、每片段重复表头）；定位 `loc_kind = sheet` + `loc_sheet` + `loc_row_from/to`，定位串形如「预算表 第 2-21 行」，行号取**工作表真实行号**（`row/@r`）。
    - 单表读取失败**不中断整册**（记入待报告列表）；若全部工作表都读不出则明确报错。
  - **新增 `PptxExtractor`**（`pptx`）：按 `ppt/slides/slideN.xml` 逐页抽取，每页一个片段；段落以换行连接，`loc_kind = slide` + `loc_page`，定位串「第 N 张幻灯片」。
    - **按文件名中的数字排序**（`slide2.xml` 排在 `slide10.xml` 之前），避免字符串排序导致顺序错乱；无文本的幻灯片与单页损坏不产片段、不中断整册。
  - **`ExtractorRegistry` 登记新解析器**（docx / xlsx / pptx），上层调用方式不变（仍只用 `forPath()`）。
  - **小重构**：把「表格每片段行数」提为公共常量 `kTableRowsPerSegment`（`Extractor.h`），CSV 与 XLSX 共用；`markdownCell/markdownRow/markdownSeparator` 提为 `Extractor.h` 内联工具，两个表格解析器共用一份实现，避免口径漂移。
  - **测试 `tests/tst_extractors.cpp` 扩充到 15 用例（含 init/cleanup 共 17 项）**：新增一个**测试专用 ZIP 写入器**（用 Qt 自带 zlib 现场生成**合法 ZIP**，支持 stored 与 deflate 两种方式并写真实 CRC32），因此**无需预置任何二进制夹具**即可覆盖解压路径。
- **自测**：Debug 构建通过（零编译告警）；`ctest -C Debug` **17/17** 通过（M1/M2/M3-a 无回归）；`MeowAi.exe --smoke-test` 退出码 **0**。
  - **docx**：`docxExtractsParagraphsAndTableRows` —— **同一份内容分别以 stored 与 deflate 两种方式打包**，两种都解出 5 个片段，「第 1 段 = 工程概况」「第 3 段 = 项目 | 金额」「第 4 段 = 土建 | 1200万」（表格行语义保留）、空段落不产片段。
  - **xlsx**：`xlsxExtractsSheetRowsWithLocators` —— 共享字符串表头（名称/预算）、内联字符串与数值单元格、25 条数据行 → 两片段「预算表 第 2-21 行」「预算表 第 22-26 行」；**稀疏行 `A24` 留空**仍保留列对齐（断言 `|  | 240 |`）。
  - **pptx**：`pptxExtractsSlidesInNumericOrder` —— 夹具故意含 `slide10.xml`，断言第 3 片为「第十页」（**数字排序而非字符串排序**），`loc_page` 递增。
  - **健壮性**：`officeBrokenContainerReportsError` —— 非 ZIP 内容报「不是有效的 Office 文件」；合法 ZIP 缺 `word/document.xml` 报「缺少 word/document.xml」；无幻灯片报「未找到幻灯片」。三种情况**都不崩溃、都有中文明确错误**。
  - **注册表**：`registryRoutesByExtension` 更新为 docx/xlsx/pptx 命中；`pdf` 与 `image` 仍**明确返回 `nullptr`**（M3-c / M3-d 待补，上层须给「暂不支持」提示）。
- **遗留 / 待定**：
  - **M3-c**：`pdf`（PDFium，按用户决策**本地放置 + 仓库忽略**：放 `third_party/pdfium/`、加 `.gitignore`、附固定 tag 的抓取脚本；CMake 检测到才启用，缺失时降级并明确提示）。
  - **M3-d**：`ImageCaptionExtractor`（图片走视觉模型，属 providers 层）+ `ImageTranscoder`（长边压缩 / BMP 转码）+ 视频真实现。
  - docx 的页眉页脚、批注、脚注暂不抽取；xlsx 的公式原始表达式、图表、批注暂不抽取；pptx 未按 `presentation.xml` 的 `sldIdLst` 排序（按文件名数字）；以上均在本次范围外，已在代码注释标注。
  - 按要求暂不提交 git（远端仓库信息待补充，PR 后续统一处理）。
- **出处**：
  - ZIP 格式规范（本地头 `0x04034b50`、中央目录 `0x02014b50`、结束记录 `0x06054b50`，字段偏移与「本地头额外字段长度可能与中央目录不同」）：https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT
  - zlib 手册（`inflateInit2` 的 `windowBits = -15` 表示**原始 deflate 流**；`zlib.h` 中 `z_inflateInit2` 为 Qt 前缀化后的等价入口）：https://zlib.net/manual.html#Basic
  - Qt 自带 zlib（`QtZlib/zlib.h`，符号前缀 `z_` 由 `Qt6Core` 导出，无需额外依赖）：https://doc.qt.io/qt-6/third-party-licenses.html
  - Qt XML 流式解析（`QXmlStreamReader` 的 `qualifiedName()/name()/attributes()` 与命名空间处理）：https://doc.qt.io/qt-6/qxmlstreamreader.html
  - OOXML 文本结构（Word 正文 `word/document.xml` 的 `w:p`/`w:t`/`w:tbl`、`w:instrText` 域代码、`w:delText` 修订文本）：https://learn.microsoft.com/en-us/office/open-xml/word/paragraphs
  - OOXML 共享字符串与工作表（`xl/sharedStrings.xml` 的 `si`/`t`、`xl/workbook.xml` 的 `sheets/sheet`、关系表 `xl/_rels/workbook.xml.rels` 的 `Relationship`、`xl/worksheets/sheetN.xml` 的 `row/@r` 与 `c/@r`/`c/@t`）：https://learn.microsoft.com/en-us/office/open-xml/spreadsheet/how-to-parse-a-spreadsheet
  - OOXML 幻灯片（`ppt/slides/slideN.xml` 的 `a:p`/`a:t`）：https://learn.microsoft.com/en-us/office/open-xml/presentation/structure-of-a-presentationml-document
  - 深研结论：**不引入第三方 ZIP/Office 库**（既有 Qt 自带 zlib 足够），保持「零新增依赖 + 不用 Qt 私有 API」；三个解析器统一遵守 `Extractor` 契约（本地同步、失败给中文明确错误、片段必带定位），与 M3-a 的分块口径（首行表头、20 行一段、每段重复表头）保持一致。

### 2026-10-20 ｜ v0.2.5 资料库 / RAG · M3-a：解析层骨架 + 纯文本类解析器（plain / csv / json）｜ 状态：待验收
- **阶段**：阶段二（资料库 / RAG · 模块 M3-a / M3 共拆 4 小步：a 注册表与纯文本类、b Office 三件套、c PDF(PDFium)、d 图片 caption 与视频预留）
- **负责人**：开发 Agent（待补充）
- **分支 / PR**：`v0.2.5`
- **交付内容**：
  - **新增 `core/library/extract/Extractor.h`**：解析层公共契约。
    - `LocatorKind`（`Para/Row/Sheet/Page/Slide/Json/Image/None`）+ `locatorKindName()` 内联映射 —— **落库字符串与 `chunks.loc_kind` 的取值一一对应**，避免各解析器各写一套字符串。
    - `ExtractedSegment`：**强制携带定位信息**（`locLabel` 人可读定位串 + `locPage/locSheet/locRowFrom/locRowTo`），否则引用来源无法向用户核对（DoD #2）。
    - `ExtractionResult{ok, segments}`；`Extractor` 接口 = `id()` / `supports(kind, ext)` / `extract(path, result, error)`，**只做本地同步解析、绝不发起网络请求**，失败一律 `false + error`、不抛异常。
    - `kMaxExtractBytes = 64MB` 单次解析上限 + 共用读取函数 `readLibraryTextFile()`：校验存在/普通文件/大小 → **去 UTF-8 BOM** → UTF-8 优先、出现替换字符时回落本地编码（中文 Windows 即 GBK）→ `\r\n`、孤立 `\r` **统一归一为 `\n`**。
  - **新增 `core/library/extract/ExtractorRegistry.{h,cpp}`**：注册式路由——`add(std::unique_ptr<Extractor>)`（**注册表独占所有权**，同 id 后注册者覆盖）、`forPath()`（按 `LibraryFileKinds` 类别 + 扩展名匹配）、`ids()`、`extract()` 便捷入口（**无匹配格式时给出「暂不支持该格式」明确错误**，不留空指针崩溃）。
  - **新增 `PlainTextExtractor`**（`txt / md / markdown / log`）：按**空行分段**（Markdown 的自然块边界），每段一个片段、定位串「第 N 段」；**长度切分与重叠留给 M4 的 `ChunkSplitter`**，本层只负责「语义块 + 定位」。
  - **新增 `CsvExtractor`**（`csv / tsv`）：**RFC 4180 风格解析**（引号包裹、引号内的分隔符与换行、`""` 转义、字段起始处才认引号），转 **Markdown 表格**（首行即表头、每片段重复表头以保证单独可读、列数按块内最大值补齐、单元格内 `|` 转义为 `\|`、换行转 `<br>`）；每片段最多 20 条数据行，定位串按**文件真实行号**标注「第 12-18 行」（表头为第 1 行）。
  - **新增 `JsonExtractor`**（`json`）：嵌套结构**展平为「路径: 值」语义行**（保留键路径含义），再按**顶层分支**（根对象每个键 / 根数组每个元素）分组为片段，定位串即该分支的 JSON 路径（`$.server.port`、`$[3].name`）；空对象/空数组不产生片段；超过 `kMaxLinesPerSegment = 20000` 时截断并在片段尾部**明确标注**，不静默丢数据。
  - **新增 `VideoExtractor`（预留注册点）**：`supports(kind == Video)` 命中，`extract()` 返回 `false` + 「视频解析本次未实现（仅预留注册点）」。入库侧 `LibraryManager` 仍把视频登记为 `unsupported`、不进入索引流程；本类保证注册表类型覆盖完整，后续实现视频解析只需替换其内部。
  - **新增测试 `tests/tst_extractors.cpp`（11 用例 / 13 含 init+cleanup）**；`CMakeLists.txt` 与 `tests/CMakeLists.txt` 接入新源码与测试目标。
- **自测**：Debug 构建通过（零编译告警）；`ctest -C Debug` **17/17** 通过（M1/M2 的 16 项无回归）；`MeowAi.exe --smoke-test` 退出码 **0**。
  - **定位元数据用例**：`plainTextSplitsParagraphsWithLocators`（段落序号 + `loc_kind=para` 落库串）、`csvConvertsToMarkdownTableWithRowRanges`（`第 2-21 行` / `第 22-26 行`，Markdown 表头与分隔行、单元格 `|` 已转义）、`jsonFlattensBranchesWithPathLabels`（`$.server` / `$.tags` 两段，行内容为完整路径 + 值）。
  - **健壮性用例**：`plainTextHandlesBomCrlfAndEmptyFile`（BOM 去掉、CRLF 归一、空文件合法且零片段）、`plainTextDoesNotDropNonUtf8Content`（非 UTF-8 内容回落本地编码、**不留替换字符**）、`csvKeepsQuotedDelimiterAndNewlineInOneField`（引号内的逗号与换行不拆列拆行）、`jsonInvalidContentReportsParseError`（坏 JSON → 明确错误 + 不崩）。
  - **注册表用例**：`registryRoutesByExtension`（md→plain、csv/tsv→csv、json→json、mp4→video；**pdf/xlsx 本批明确返回 `nullptr`**，即 M3-b/M3-c 的待补清单）、`registryIsOverridableBySameId`（同 id 覆盖且只保留一份）、`videoIsReservedAndReportsNotImplemented`、`missingFileAndUnknownFormatFailWithMessage`。
- **遗留 / 待定（M3 后续小步）**：
  - **M3-b**：`docx / xlsx / pptx` —— 需先落 `OfficeXmlUtils`（自研最小 ZIP 读取器，**走 Qt 自带的 `QtZlib`（符号已前缀化为 `z_inflate` / `z_inflateInit2_`，由 `Qt6Core` 导出）**，零新增第三方依赖）+ `QXmlStreamReader` 抽文本；定位分别为段落 / 工作表名 + 行号 / 幻灯片序号。
  - **M3-c**：`pdf` —— 按已锁方案接 **PDFium**（BSD-3-Clause，`FPDFText_LoadPage / CountChars / GetText` 抽文本层，页码天然可得；扫描件渲染页 → 视觉模型为可选且可一键关闭，关闭时标 `no_text_layer` 并明确提示）。`bblanchon/pdfium-binaries` 最新 tag `chromium/8111` 已确认可获取，**待用户确认落地方式**（vendored 进仓库 / 可选构建降级）。
  - **M3-d**：`ImageCaptionExtractor`（图片走视觉模型生成 caption，属 providers 层，需 `ImageTranscoder` 做长边压缩与 BMP 转码）+ 视频真实现。
  - 因此本批 `forPath()` 对 `pdf/docx/xlsx/pptx/image` 尚无解析器：上层（M4 索引器）需把「无匹配解析器」落到 `status = needs_* / unsupported` 并给出明确提示，不得当作崩溃。
  - 按要求暂不提交 git（远端仓库信息待补充，PR 后续统一处理）。
- **出处**：
  - Qt 文件读取与信息（`QFile::readAll`、`QFileInfo::size/isFile`）：https://doc.qt.io/qt-6/qfile.html 、https://doc.qt.io/qt-6/qfileinfo.html
  - Qt 字符串编码与替换字符（`QString::fromUtf8` 对非法序列替换为 `QChar::ReplacementCharacter`、`QString::fromLocal8Bit` 回落本地编码）：https://doc.qt.io/qt-6/qstring.html#fromUtf8 、https://doc.qt.io/qt-6/qchar.html#ReplacementCharacter
  - Qt 正则分段（`QRegularExpression` 用于空行切分）：https://doc.qt.io/qt-6/qregularexpression.html
  - Qt JSON 解析与错误（`QJsonDocument::fromJson(QJsonParseError*)` 带偏移量、`QJsonValue::type()` 区分类型）：https://doc.qt.io/qt-6/qjsondocument.html#fromJson 、https://doc.qt.io/qt-6/qjsonparseerror.html
  - RFC 4180（CSV 的引号包裹、引号内分隔符与换行、`""` 转义规则）：https://www.rfc-editor.org/rfc/rfc4180
  - 深研结论：**解析器契约与文件清单严格按 `docs/PROMPT.md` §3 的已锁定方案落地**；本层只做「本地语义块 + 定位」，长度切分/重叠归 M4、网络（caption/embedding）归 providers，保持分层边界不杂糅。

### 2026-10-20 ｜ v0.2.5 资料库 / RAG · M2：core 层（LibraryManager 文件来源 + CollectionManager）｜ 状态：待验收
- **阶段**：阶段二（资料库 / RAG · 模块 M2 / 共 8 个模块）
- **负责人**：开发 Agent（待补充）
- **分支 / PR**：`v0.2.5`
- **交付内容**：
  - **新增 `core/library/LibraryFileKinds.{h,cpp}`**：文件类别判定与命名（`LibraryKind`：Text / Image / Video / Unknown）。文本集 = `txt/md/markdown/csv/tsv/json/log/pdf/docx/xlsx/pptx`；图片集 = `jpg/jpeg/png/webp/gif/bmp`；视频集 = `mp4/mov/mkv/avi/webm/m4v`。用途：上传导入与文件夹扫描共用同一套「可索引 / 仅登记 / 直接跳过」判定，**UI 与核心层不各自硬编码扩展名**。
  - **重写 `core/library/LibraryManager.{h,cpp}`（覆盖阶段一空骨架）**：资料库**文件来源**的两种语义严格分离。
    - **上传导入**：把文件**复制**进 `<AppData>/library/`（`QFile::copy` + **大小校验**，校验失败即清理未完成副本），**绝不移动、绝不覆盖原文件**；同名自动改名；原路径只记在 `origin_path` 里**仅作溯源、永不删除**。登记失败则回滚已复制副本，不留孤儿文件。
    - **关联文件夹（快照）**：只对用户目录建**索引引用**，**不复制、不监视**；扫描（`QDirIterator` 递归、`NoSymLinks`）后落库，上限 `kMaxSnapshotFiles = 5000`（超出**直接中止、不做部分写入**，避免误关联整个磁盘）。**先扫描后落库**：文件夹不可读 / 文件过多时不留半成品关联记录。
      - 快照统计（`FolderSnapshotResult`）：`added` / `unchanged` / `markedStale`（内容变更）/ `missing`（磁盘已消失，**只标记不删除**）/ `unsupported`（视频：**登记但不索引**）/ `skipped`（扩展名不支持：完全不入库）。
      - **手动重建**：`rebuildFolder` 由用户触发，识别新增 / 变更（标记 `stale`）/ 缺失（标记 `stale`），**全程只更新记录与状态，不删任何磁盘文件**；重建失败会把关联状态写为 `failed` 并留下错误文本，便于 UI 提示。
    - **删除安全语义（红线）**：`isManagedCopy()` 路径前缀校验是删除的**唯一放行条件**——只有 `sourceKind == "upload"` 且确属我们复制进库内目录的副本才允许 `QFile::remove`；关联文件夹里的用户文件与上传时的**原文件永不触碰**。库内副本清理失败只 `qWarning`（索引已移除、磁盘留残），不阻断操作。
    - 内容指纹 `contentHash`：≤ 8MB 取完整 `sha256:`；大文件退化为 `sz-mtime:size:mtime` 代理值，避免每次快照对上百 MB 文件做全量哈希。
  - **新增 `core/library/CollectionManager.{h,cpp}`**：小库（命名库）管理——列表 / 新建 / 改名 / 删除，以及文件与库的多对多归库关系（`addFileToCollection` / `removeFileFromCollection` / `collectionsOfFile`）。
    - `removeCollection()` 三阶段：① 先解除该库下**全部关联文件夹**；② 该库下**不再归属任何小库**的文件记录才移除索引（upload 类顺带清理库内副本，folder 类只删记录）；③ 删除库记录（归库关系与快照行由数据库级联清理）。
  - **`storage/LibraryStore` 增补**：`filesByFolderLink(folderLinkId)`（快照重建与解除关联需要按快照取文件记录），M1 未交付该方法。
  - **新增测试 `tests/tst_librarymanager.cpp`（10 项 / 12 含 init/cleanup）**；`CMakeLists.txt` 与 `tests/CMakeLists.txt` 接入新源码与测试目标。
- **自测**：Debug 构建通过（零编译告警）；`ctest -C Debug` **16/16** 通过（M1 的 15 项无回归）；`MeowAi.exe --smoke-test` 退出码 **0**。
  - **安全红线用例**：`removeFileRecordRemovesUploadCopyOnly`（移除上传记录后**库内副本被清理、用户原文件内容一致**；folder 类记录移除后**磁盘文件原样保留**）、`removeCollectionKeepsUserFilesOnDisk`（删除小库后**用户磁盘文件全部保留**、共用文件的其它库归属保留）、`unlinkFolderKeepsUserFilesAndDetachesSharedRecords`（解除关联后用户文件仍在、共用记录脱离快照但保留）。
  - **来源语义用例**：`importCopiesIntoLibraryAndKeepsOriginal`（复制入库、原文件不动、同名改名）、`importRejectsMissingUnknownAndManagedCopy`（不存在 / 未知扩展名 / 已在库内均给出中文明确错误）、`importFilesPartialFailureReportsAndKeepsSuccess`（部分失败时成功项仍可用）、`folderSnapshotRegistersKindsAndSkipsUnknown`（视频只登记不索引、未知扩展名不入库）、`folderRebuildDetectsAddedChangedMissing`（重建识别新增/变更/缺失且不删盘上文件）、`linkFolderRejectsDuplicateAndMissingRoot`（跨库重复关联被拒、失败不留半成品）。
- **语义决策（需用户确认 / 可否决）**：
  - ① **解除文件夹关联**：快照文件会**从该小库撤出**（记录同时脱离快照）；若该文件仍归属其它小库，则**保留记录、仅脱离本快照**。即「解除关联」= 该文件夹的文件不再属于本库。
  - ② **同一物理文件夹不允许跨库重复关联**（给中文明确错误并提示改用「加入小库」）。理由：一个文件记录只挂一个快照，否则「快照重建 / 解除关联」对共用文件会产生归属歧义；需要跨库的重复内容请用「加入小库」。
- **遗留 / 待定**：
  - `CollectionManager` 的「删除小库」不删除**库内副本以外的任何东西**；`removeCollection` 中「先解除文件夹」的顺序保证不产生孤儿快照。
  - 视频本次只在库中**可见**、状态标记 `unsupported`，不解析不索引（M3 的 `VideoExtractor` 仅预留）。
  - 按要求暂不提交 git（远端仓库信息待补充，PR 后续统一处理）。
- **出处**：
  - Qt 文件与目录遍历（`QDirIterator` 递归 + `NoSymLinks`、`QDir::cleanPath`、`QFileInfo`）：https://doc.qt.io/qt-6/qdiriterator.html
  - Qt `QFile::copy` 语义（**不覆盖已存在目标**，同名需自行改名）：https://doc.qt.io/qt-6/qfile.html#copy
  - Qt 加密哈希（`QCryptographicHash::Sha256` + `addData(QIODevice*)` 流式哈希，用于内容指纹）：https://doc.qt.io/qt-6/qcryptographichash.html
  - 深研结论：**两种来源的语义差异（复制 vs 引用）与「绝不删用户原文件」的红线严格按 `docs/PROMPT.md` §0/§2 的已锁定口径落地**；`isManagedCopy()` 作为删除前的唯一放行条件，与 M1 的 `removeFile` / `removeFolderLink` 职责边界清晰（存储层不做删除磁盘文件的决策）。

### 2026-10-20 ｜ v0.2.5 资料库 / RAG · M1：storage 层（schema v2 + LibraryStore + LibraryConfigStore）｜ 状态：待验收
- **阶段**：阶段二（资料库 / RAG · 模块 M1 / 共 8 个模块）
- **负责人**：开发 Agent（待补充）
- **分支 / PR**：`v0.2.5`
- **交付内容**：
  - **`storage/Database.{h,cpp}` · schema v2 前向幂等迁移**：新增 8 张资料库表的 DDL（`collections` / `library_files` / `file_collections` / `folder_links` / `chunks` / `chunk_vectors` / `citations` / 虚拟表 `chunk_fts`）+ 唯一索引与查询索引。**v1 的会话历史表一行未动**，迁移逐版本增量执行、全部 `IF NOT EXISTS`，重复调用（每次启动）幂等。
    - **FTS5 能力探测与降级**：先 `SELECT sqlite_compileoption_used('ENABLE_FTS5')`，失败再扫 `PRAGMA compile_options`；探测结果经 `Database::fts5Available()` 暴露给上层。虚拟表按 `trigram` → 默认分词器 → 关闭 FTS **三级降级**创建，任一失败都不会让整个 schema 迁移失败（关闭时关键词检索改走 LIKE）。
    - FTS 索引表与版本号**解耦**：已是 v2 的库若缺 `chunk_fts`（迁移时 FTS 尚不可用）会自动补建，避免上层的 FTS 写入打在不存在的表上。
    - `m_fts5Available` 在版本提前返回**之前**完成探测，保证「库已是最新版本」的常见路径下能力标志依然正确。
  - **新增 `storage/LibraryStore.{h,cpp}`**：schema v2 各表的**纯 CRUD**（无业务判断、无 UI 语义），所有失败路径返回 `false + error` 不抛异常。
    - 小库：列表 / 按名查询 / 新建（重名给出中文明确错误）/ 改名 / 删除。
    - 文件：列表 / 库内列表 / 按 id、按 `source_path` 查询 / 新增（来源路径重复给出中文明确错误）/ 整行更新 / 删除记录。
    - 归库关系：多对多关联（`INSERT OR IGNORE`，**重复关联幂等**）/ 解除关联 / 查所属小库。
    - 关联文件夹快照：新增 / 写快照（时间 + 文件数 + 状态）/ 列表 / 删除关联。
    - 片段：`replaceChunks` 在**单个事务**内「清旧片段 + 清对应 FTS 行 → 插新片段 + 同步 FTS → 回填 `library_files.chunk_count`」，失败整体回滚（重新切块不残留、不重复）；`chunksOfFile` / `chunksByIds` / `chunkById` / `chunksInOrdinalRange`（邻居扩展用）。
    - 关键词检索：`searchChunkIds(query, collectionIds, limit)` —— FTS5 走 `MATCH` + `bm25()` 排序（查询串按 FTS5 语法整体加引号转义，trigram 至少 3 字符）；不足 3 字符或 FTS 不可用则降级 `LIKE`（`%` `_` `\` 全部转义 + `ESCAPE`）；`MATCH` 因任意标点失败时再降级 LIKE，不让检索整体报错；支持按小库过滤。
    - 引用来源：写入 / 按消息查询（按 `rank` 升序）/ 按消息、按会话清理（citations 无外键，删除消息与会话时必须显式清理，否则留脏数据）。
  - **新增 `storage/LibraryConfigStore.{h,cpp}`**：只读写 `config.json` 的 `library` 段（模式对标 `AiConfigStore`：读现有 JSON → 只替换本段 → `QSaveFile` 原子写）。**其他段（ai / theme / personalization / web）一律原样保留**；**绝不保存密钥**（embedding / caption 的 Key 只进 `storage/SecureStore`）。默认值即 PROMPT 锁定口径：`topK=6` / `perFileLimit=2` / `tokenBudget=2000` / `similarityThreshold=0.30` / `neighborRadius=1` / `ftsFallback=true` / `chunkSize=700` / `chunkOverlap=120`，外加 `enabled`、`consentGiven`+`consentAt`、`embedding{providerId,modelId,dimensions,batchSize}`、`caption{providerId,modelId,maxEdge,maxTokens,promptProfile}`、`mountedCollections`。
  - **新增测试**：`tests/tst_librarystore.cpp`（13 项）、`tests/tst_libraryconfig.cpp`（6 项）、`tests/tst_storage.cpp` 增 2 项（v2 建表 + 迁移幂等且不删 v1 表）。`CMakeLists.txt` / `tests/CMakeLists.txt` 接入新源码与测试目标。
- **自测**：Debug 构建通过（无编译告警）；`ctest -C Debug` **15/15** 通过（原 13 项无回归 + 新增 2 个测试目标）。
  - **安全红线专项用例**：`removingCollectionKeepsFilesAndOtherMembership`（删除小库后文件记录与其他库归属都保留）、`removingFileRecordNeverTouchesDiskFile`（磁盘真实文件内容逐字节比对，删除索引后**原文件依然存在且内容一致**）。
  - **降级专项用例**：`unavailableWithoutDatabase`（无数据库时全部接口返回失败 + 中文错误，不崩）、短关键词（2 字符）走 LIKE 召回、`%` 通配符被转义、空关键词给明确错误。
  - **切片一致性用例**：`replaceChunksIsAtomicAndIdempotent`（含「重新切块不残留」「清空片段合法」「非法 fileId 被拒」）、`neighborRangeAndChunkById`（含边界不越界）。
  - 实测本机 Qt 自带 SQLite **FTS5 可用**（`fts5Available() == true`，走 trigram + bm25 路径；LIKE 降级路径亦被用例覆盖）。
- **遗留 / 待定**：
  - 本次**只交付 storage 层**，尚无 core 层调用方；`chunk_vectors` 的读写归 M4（`storage/SqliteVectorIndex`），故 `LibraryStore` 本批不含向量 CRUD（表已建好）。
  - `folder_links` 之下文件索引记录的连带清理**由 core 层（M2）决定**，存储层只提供 `removeFolderLink` 与 `removeFile` 两个原子操作。
  - 应用可执行文件 `MeowAi.exe` 本次**未重新链接**（用户正在运行的实例占用该文件，LNK1168），库与全部测试目标均已重建；待用户关闭实例后补一次整体构建。
  - 按要求暂不提交 git（远端仓库信息待补充，PR 后续统一处理）。
- **出处**：
  - SQLite FTS5 扩展（`fts5` 虚拟表、`MATCH`、`bm25()`、`trigram` 分词器自 3.34 起支持中文子串检索、`tokenize='trigram'`）：https://www.sqlite.org/fts5.html
  - SQLite 编译期选项探测（`sqlite_compileoption_used()` / `PRAGMA compile_options`，用于 FTS5 能力探测与降级）：https://www.sqlite.org/pragma.html#pragma_compile_options
  - SQLite 外键级联与事务（`ON DELETE CASCADE` 用于归库关系与片段自动清理；`PRAGMA foreign_keys` 为**连接级**开关，需每次连接显式开启）：https://www.sqlite.org/foreignkeys.html
  - SQLite `LIKE` 转义语义（`ESCAPE` 子句 + `%`/`_` 通配符需转义）：https://www.sqlite.org/lang_expr.html
  - Qt SQL 驱动（`QSqlDatabase::transaction/commit/rollback`、`QSqlQuery::lastInsertId`、驱动绑定空值写 NULL）：https://doc.qt.io/qt-6/qsqldatabase.html
  - Qt 原子写文件（`QSaveFile`：写临时文件后 `commit()` 原子替换，避免配置写坏）：https://doc.qt.io/qt-6/qsavefile.html
  - 深研结论：**8 张表 DDL 与配置项默认值严格按 `docs/PROMPT.md` §4/§5 的已锁定方案落地**，未自行增删字段；`chunk_fts` 用**独立 FTS5 表**（非 `content=` 外部内容表），故由 `LibraryStore` 在片段增删时**显式同步**索引行（虚拟表不支持外键级联）。
- **补充（M1 收尾，状态变更，原条目保留）**：经用户确认后结束占用构建产物的运行实例，**整体重建完成**（`MeowAiLib` / `MeowAi.exe` / 全部 16 个测试目标，零编译告警），`MeowAi.exe --smoke-test` 退出码 **0**，`ctest -C Debug` 复核 **15/15** 通过。上条「`MeowAi.exe` 未重新链接」的待定项**已解除**。

### 2026-10-20 ｜ v0.2.4 联网检索改造：B 形态（模型自主联网）+ DeepSeek 零配置 + UI 弱化 ｜ 状态：待验收
- **阶段**：阶段二（AI 接入增强 · 联网检索改造）
- **负责人**：开发 Agent（待补充）
- **分支 / PR**：`v0.2.4`
- **交付内容**（把 v0.2.3 的「A 形态·应用侧预检索」替换为「B 形态·模型自主按需联网」，并新增厂商内置源零配置）：
  - **providers 层 · 工具调用（function calling）**：
    - `providers/ChatTypes.h`：新增 `ChatToolCall{id,name,arguments}`；`ChatMessage::Role` 增 `Tool`，并新增 `toolCallId` / `toolCalls`；`ChatRequest` 增 `tools` / `toolChoice`；`ChatResponse` 增 `FinishReason` 枚举与 `toolCalls`。注册 `Q_DECLARE_METATYPE` 供信号跨线程 / `QSignalSpy` 使用。
    - `providers/OpenAiProvider.{h,cpp}`：`buildBody` 支持 `role=tool`（`tool_call_id` + 文本）与 assistant 携带 `tool_calls` 透传；请求体按需注入 `tools` 与 `tool_choice`；`processStreamEvent` 按 `delta.tool_calls[].index` **拼接**（id/name 覆盖、arguments **追加**，兼容分片与缺省 index），并记录 `finish_reason`；`handleFinished` 流式 / 非流式均下发 `toolCalls`，`finishReason` 有工具调用即判 `ToolCalls`。
  - **目录与配置（数据驱动，零硬编码）**：`resources/providers.json` 的 `deepseek` 条目新增 `nativeSearch{protocol:"deepseek", searchEndpoint:"…/v1/tools/search", fetchEndpoint:"…/v1/tools/fetch", reuseProviderKey:true}`；`core/ai/ProviderCatalog` 解析 `nativeSearch`（`protocol` 归一化小写；缺 `searchEndpoint` 或非 `http` 前缀 → **只记 warning 并忽略**，不使整条目录失效），`providerOptions()` 增 `nativeSearch` 布尔供 UI 判断；UI 依旧**不硬编码厂商**。
  - **core/web · 双协议检索后端**：`WebSearchProvider.h` 的 `WebSearchConfig` 增 `enum class Protocol{Generic,DeepSeek}` 与 `fetchEndpoint`（**追加在原有成员之后**，保持既有聚合初始化兼容）。`HttpWebSearchProvider` 增 `isDeepSeek()` 与 `jsonRequest()`：DeepSeek 协议下搜索 `POST <searchEndpoint>` body `{"query","count"}`、抓取 `POST <fetchEndpoint>` body `{"url"}`，均带 `Content-Type: application/json` 与 `Authorization: Bearer <providerKey>`（**密钥只走请求头，不进 URL、不进日志 / 错误信息**）；`parseResults` 的摘要优先级补入 `summary`；`parseFetch` 增 DeepSeek 分支解析 `{title,content(Markdown)}` 并按 `maxFetchChars` 裁剪。`WebSearchService` 增 `configure(runtime)` / `clearRuntimeConfig()`：运行时方案就绪时**跳过自填端点和搜索密钥**，支撑「厂商内置源 · 复用厂商 Key」的零配置路径。
  - **core/web · 工具封装**：新增 `WebSearchTool.{h,cpp}`，把一次「检索 + 抓取 + 拼接」包装成模型可调用的工具：`descriptor()` 给出 OpenAI function schema（`name=web_search`，`required=[query]`），`execute(query)` 异步执行并以 `finished(resultText, sources, error)` 回传；service 为空 / query 为空时**立即以 error 回传**不崩。
  - **core/chat · B 形态工具循环**：`ChatController` 新增 `webSearchNative` / `webSearchSearching` 属性与 `effectiveSearchConfig()`（① 厂商 `nativeSearch` 有效 → `supported=true`、按 `deepseek` 映射协议、按 `reuseProviderKey` 决定取 `providerApiKeyId` 还是 `webSearchApiKeyId`；② 无内置但有自填端点 → Generic 自定义源；③ 否则给出中文 notice「当前厂商暂不支持智能联网…」）。开启联网且工具就绪时：下发 `web_search` 工具并进入**工具调用循环**（`tool_choice=auto`，模型需要时才上网），循环上限 **3 轮**（`kMaxToolRounds`），未命中 / 未知工具 / 缺参 / 检索失败均**容错继续让模型收敛**，多轮来源**累加**；不支持 / 未就绪时**降级为普通对话 + 明确提示**，不崩不清空；`cancel()` 在检索中或生成中均清空循环标志。
  - **UI 重构（按用户要求「弱化网络端子」）**：`ui/ConversationPage.qml` 删除接入卡内联网端点 / 密钥配置行，在输入条新增「智能联网」pill（点击即 `Chat.webSearchEnabled` 取反），「当前选择条」新增联网状态 chip（正在联网搜索… / 智能联网 / 联网未就绪）；联网第二次配置入口下移到 `ui/ModelMenu.qml` 的「联网搜索」入口行 → 新增第三级弹层 `ui/WebSearchPanel.qml`（总开关 + 内置源说明 / 自定义源端点与密钥）。**QML 只调 `Chat`，不碰网络与密钥明文**。
  - **tests**：新增 `tests/tst_toolcall.cpp`（8 项：请求体含 `tools`、流式 `tool_calls` 拼接、非流式解析、`WebSearchTool` schema + 执行、正常回答不检索、检索后作答、**3 轮上限停止**、厂商不支持时降级）；`tests/tst_websearch.cpp` 增 4 项（DeepSeek 搜索 `POST` JSON、DeepSeek 抓取 `POST` JSON + Markdown 裁剪、缺 `fetchEndpoint` 拒绝、**运行时方案零配置端到端**）；`tests/tst_providercatalog.cpp` 增 `nativeSearch` 解析（合法声明解析 + 非法声明被忽略且只记 warning）与内置目录自检断言。
- **自测**：本地 Debug 构建通过（含新增 `tst_toolcall` 目标）；`ctest -C Debug` **13/13** 通过；`MeowAi.exe --smoke-test` 退出码 **0**；运行期 QML 警告 **0**。
- **遗留 / 待定**：**Anthropic 本轮不支持智能联网**（提示「暂不支持」并降级为普通对话），如需支持另行评审；真实 DeepSeek 内置检索为联网手测项，待用户自备 Key 验证；**按要求暂不提交 git**，待用户提供远端信息后补 PR。
- **出处**：
  - DeepSeek 内置搜索 / 抓取 API（`POST /v1/tools/search` body `{"query","count"}` → `{"results":[{title,url,snippet,summary,content,…}]}`；`POST /v1/tools/fetch` body `{"url"}` → `{"title","content"(Markdown)}`；鉴权复用同一 API Key）：https://api-docs.deepseek.com/zh-cn/guides/search
  - OpenAI 兼容工具调用协议（`tools` / `tool_choice` / `tool_calls` / `role=tool` / `finish_reason=tool_calls`，流式按 `index` 分片拼接）：https://platform.openai.com/docs/guides/function-calling
  - 深研结论：**工具调用循环位于 core 层**（`providers` 保持「单次请求 → 信号」不反向依赖），符合「UI → core → {providers,storage} → platform」分层硬约束。

### 2026-10-20 ｜ v0.2.3 新模块：联网检索（search / fetch）｜ 状态：待验收
- **阶段**：阶段二（AI 接入增强 · 独立新模块）
- **负责人**：开发 Agent（待补充）
- **分支 / PR**：`v0.2.3`
- **交付内容**（**严格边界隔离**：只新增 `core/web/` + `storage/WebConfigStore`，不改动既有 Provider 与资料库接口）：
  - **`core/web/WebSearchProvider.h`**：联网检索纯接口 + 公共类型 `WebSearchResult{title,url,snippet}` / `WebFetchResult{url,title,text}` / `WebSearchConfig{endpoint,apiKey,timeoutMs,maxFetchChars}`。**与 `core/retriever/DocumentRetriever` 刻意分开**（前者面向公网、后者面向本地资料库），二者不共用类型与实现，避免职责杂糅。
  - **`core/web/HttpWebSearchProvider.{h,cpp}`**：自填端点 + URL 直连抓取，**不依赖任何第三方搜索 SDK**。搜索走 `GET <endpoint>`（支持 `{query}` 占位符，无占位符则自动追加 `?q=`），容错解析常见 JSON 结构（`results` / `data` / `items` / `hits` / 嵌套 `web.results`，字段名兼容 `title|name|heading`、`url|link|href|uri|permalink`、`snippet|description|content|summary|text|body`）；抓取 `GET <url>`，剥离 `<script>`/`<style>`、去标签、解码常见实体、折叠空白并按 `maxFetchChars` 裁剪正文。响应体累积上限 512KB 防超大页面。网络**一律经 `providers/NetworkTransport` 抽象**发出（core → providers 方向合法；生产自建 `QNetworkTransport`，单测注入假实现）。
  - **`core/web/WebSearchService.{h,cpp}`**：一次 `run()` 编排「检索 → 抓取前 2 条正文 → 拼成可注入上下文（标题 + 链接 + 摘要 + 正文）」，**单页抓取失败只跳过该页、整体不中断**。
  - **`storage/WebConfigStore.{h,cpp}`**：只读写 `config.json` 的 `web` 段（`enabled` / `endpoint`），**模式对标 `AiConfigStore`**（读现有 JSON → 只替换本段 → `QSaveFile` 原子写；损坏文件以空对象重建），其他段（ai / theme / personalization）一律保留不动；**绝不保存密钥**。
  - **`storage/SecureStore`**：新增固定键名 `web.search.api_key`（`webSearchApiKeyId()`），搜索密钥**只走保险库、绝不明文落盘**；`Authorization: Bearer <key>` 只随请求头下发、**不进 URL**，日志与错误信息不含密钥。
  - **A 形态接入（应用侧预检索）**：`core/chat/ChatController` 新增 `webSearchEnabled` 开关与端点 / 密钥的 QML 门面；开关打开时在 `dispatchUserMessage()` 中**先检索**，把资料作为一条 system 消息插在「当前时间」注入之后（**同样不入库**）再发送；**未配置（端点或密钥缺失）/ 401 / 403 / 429 / 超时 / 断网 / 网页不可抓**一律**降级为普通对话 + 中文明确提示**，不崩、不清空；检索阶段点「中断」整轮作废并丢弃迟到结果。
  - **`core/AppController`**：装配 `WebConfigStore` + `HttpWebSearchProvider` + `WebSearchService` 并注入 `ChatController`。
  - **`ui/ConversationPage.qml`**：接入卡新增「联网」开关、搜索端点输入、搜索密钥保存 / 清除、就绪状态 chip 与配置提示；底部新增「已联网 · N 条来源」与来源标题 chip。**QML 只调 `Chat`，不碰网络与密钥明文**。
  - **`tests/tst_websearch.cpp`**（18 项，全离线）：配置往返 + 其他段保留 + 损坏回退；`{query}` 占位符与自动追加 `?q=`；密钥走 Bearer 头且**绝不进 URL**；空端点拒发请求；JSON 两种结构解析 + 无 url 条目丢弃 + topK 生效；非 JSON 响应明确报错；**401 / 403 / 429 / 500 / 超时 / 断网**分类提示且不崩；抓取标题 / 正文抽取 + 脚本样式剥离 + 上限裁剪；非 http 地址拒绝；**取消静默**；服务缺端点 / 缺密钥的状态说明；**端到端编排**（检索 → 抓 2 条 → 第 2 条超时仍成上下文）。
- **自测**：Debug 构建通过（无编译告警）；`ctest -C Debug` **12/12** 通过；`tst_websearch` 单跑 **18 passed / 0 failed / 0 skipped**。
- **接口偏差说明（如实记录）**：最初草案曾设想阻塞式 `search(query, topK, error)`。因架构硬约束「网络只经 `providers/NetworkTransport` 抽象」而该抽象本身即异步（无阻塞 API），阻塞实现只能靠嵌套事件循环，会**卡住 UI 线程且易重入**；故最终接口改为**异步 + 信号**（`searchFinished` / `fetchFinished`），语义与既有 `AiProvider` 一致。行为与 DoD 完全一致，仅调用形态变化。
- **出处**：
  - HTTP 语义（GET / `Authorization` 请求头字段）：RFC 9110 — https://www.rfc-editor.org/rfc/rfc9110
  - Qt 网络（`QNetworkAccessManager` / `QNetworkRequest` / 超时与错误分类）：https://doc.qt.io/qt-6/qnetworkaccessmanager.html
  - JSON 搜索结果结构参考（`{"results":[…]}` 是自建/自托管搜索端点的最常见形态）：SearXNG 搜索 API — https://docs.searxng.org/dev/search_api.html
- **遗留 / 待定**：**B 形态（模型自主工具调用 search/fetch）未实现**（按既定决策 A 先行）；真实端点手测需用户自备端点与密钥，待人工验证；厂商级「原生联网」能力未接入（本项目统一走应用侧预检索）。

### 2026-10-20 ｜ v0.2.3 补丁：DeepSeek 视觉标记修正 + 当前时间注入 ｜ 状态：待验收
- **阶段**：阶段二（AI 接入增强 · 补丁）
- **负责人**：开发 Agent（待补充）
- **分支 / PR**：`v0.2.3`
- **交付内容**：
  - **修正数据错误**：`resources/providers.json` 中 `deepseek-flash` 的 `supportsVision` 由 `false` 改为 `true`。依据 DeepSeek 官方文档 `https://api-docs.deepseek.com/quick_start/pricing` —— **deepseek-flash（DeepSeek-V4.1-Flash）Vision ✓ 支持**，而 `deepseek-v4-pro` 官方标注 **Not supported**（保持 `false`）。此前误标导致「DeepSeek Flash 的附图入口被禁用」。
  - **新增 · 当前时间注入**：`core/chat/ChatController.cpp` 在每轮请求组装 `messages` 时**置顶插入**一条 system 消息「当前时间：yyyy-MM-dd HH:mm dddd（UTC±HH:MM）」，**不进会话历史、不入库**，每轮现算；**Provider 层零改动**（OpenAI 走 `role=system` 消息、Anthropic 走顶层 `system` 字段，两者既有实现已支持）。
- **自测**：Debug 构建通过；`ctest -C Debug` **11/11** 通过；重新启动正常。附带修复的验证：选 DeepSeek Flash → 附图入口由禁用转为启用。
- **遗留 / 待定**：时间注入**只让模型「知道当前时间」，不等于联网**；联网搜索（search / fetch）经核查项目**当前完全未实现**（现有 `SkillRegistry` 仅扫描示例技能、`DocumentRetriever`/`NullRetriever` 为空占位、`fetchModels` 是「拉取厂商模型清单」与联网无关），作为**独立新模块**另行出方案评审。

### 2026-10-20 ｜ 阶段二收尾：AI 接入增强（多厂商 + 自定义 + 思考档位 + Key 校验）｜ 状态：待验收
- **阶段**：阶段二（AI 接入增强 · 功能 #3 / #4）
- **负责人**：开发 Agent（待补充）
- **分支 / PR**：`v0.2.3`（PR 待创建；仓库远端待配置）—— 口径统一为 **v0.2.3**（上一批为 v0.2.1）
- **交付内容**（四层，数据驱动，未重写既有实现）：

  **① 数据层 · 厂商/模型目录**
  - 新增 `resources/providers.json`（PREFIX `/providers`，运行期 `:/providers/providers.json`），**内置 7 家**：`openai / deepseek / qwen（通义千问）/ kimi（月之暗面）/ zhipu（智谱 GLM）/ anthropic / custom`。**明确不接入豆包 / 火山方舟（Ark）**。
  - 每条厂商形如 `{id, displayName, protocol, baseUrl, authStyle, extraHeaders, validation:{method}, models:[...]}`，模型含 `supportsVision` / `isDefault` / `thinkingSpec.{levels, native}`。
  - `protocol` 是多厂商关键：绝大多数厂商为 **OpenAI 兼容**，**复用既有 `OpenAiProvider`，仅切换 baseUrl / authStyle / extraHeaders**；**不为每个厂商写一套 Provider**。
  - 新增 `core/ai/ProviderCatalog`：`providerOptions()` / `modelOptions(pid)` / `modelInfo(pid,mid)`，**未知 providerId 退化为 `custom` 条目**；新增 `tests/tst_providercatalog`（10 项）。
  - 新增厂商 / 模型 = **改 JSON 即可，零 UI / 核心代码改动**。

  **② Provider 层 · 鉴权 / 参数 / 校验**
  - `providers/ChatTypes.h`：`ChatRequest` 增 `thinkingLevel` + `nativeParams`；新增 `enum class CredentialStatus{Unknown,NotConfigured,Valid,Invalid,Unverifiable}`；`ProviderConfig` 增 `providerId / authStyle / validationMethod / extraHeaders`。
  - `providers/ProviderUtils`：新增 `endpointUrl`（query-key 时追加 `?key=`）、`authHeaders`（bearer→`Authorization: Bearer`；x-api-key→`x-api-key`；query-key→无头）、`requestHeaders`（`extraHeaders` **只补充不覆盖**）、`mergeNativeParams`（合并时**跳过保留字段** `model/messages/stream/system`）、`credentialStatusForHttp`（2xx→Valid；401/403→Invalid；429/超时/断网/5xx→Unverifiable）。
  - `providers/NetworkTransport`：新增 `get()`（与 `post()` 共用私有 `send()`，按 method 选 GET/POST）。
  - `providers/AiProvider`：新增纯虚 `validateCredentials()` / `fetchModels()` 与信号 `credentialsChecked` / `modelsFetched`。
  - `OpenAiProvider`：`validateCredentials()`——`validation.method=="models"` 走 `GET /models`，否则发 **chat-minimal 最小请求**（`max_tokens:1`、`stream:false`、单条 "ping"）；`fetchModels()` 解析 `{"data":[{"id":...}]}`，**失败保留内置清单、不报错不清空**；`buildBody` 末尾注入 nativeParams。
  - `AnthropicProvider`：同上；鉴权头 `x-api-key` + `anthropic-version` 兜底；思考用 `adaptive` / `disabled`（**不使用 budget_tokens**）。
  - **三态语义严守**：**429 / 超时 / 断网 → `无法验证(Unverifiable)`，绝不误判为「Key 无效」**。

  **③ core / storage · 选择与持久化**
  - 新增 `storage/AiConfigStore`：只读写 `config.json` 的 `ai` 段（`providerId / modelId / customBaseUrl / thinkingLevels / nativeOverrides`），读改写回**保留其他模块的键**，`QSaveFile` 原子写，损坏 JSON 以空对象重建；**旧键平滑迁移**（`model.provider` / `model.name`）。
  - `core/chat/ChatController` 重写为**数据驱动 + 视图接口**：`providerOptions()/modelOptions()/modelInfo()/effectiveParams()/advancedParams()`、`setProvider/setModelId/setThinkingLevel/setCustomBaseUrl/setAdvancedParams/clearAdvancedParams`、`saveApiKey/eraseApiKey/validateApiKey/fetchModelList`、`send/sendWithImage/cancel`；`credentialStatus` 输出 `unknown/notConfigured/valid/invalid/unverifiable` 字符串供 QML 直接比较。
  - 默认思考档位取该模型 `levels` 最高档；`setThinkingLevel` 会清掉该选择键的高级覆盖（语义唯一）；`effectiveParams()` **高级覆盖优先于档位映射**。
  - **自定义厂商门槛**：`customizable() && (customBaseUrl 为空 || modelId 为空)` 时 `dispatchUserMessage()` / `validateApiKey()` **拒绝并给中文错误**，避免打到空地址。
  - `core/AppController`：装配 `ProviderCatalog` + `AiConfigStore` 并注入 `ChatController`；状态栏追加「厂商 N 家」。
  - 新增 `tests/tst_aiconfigstore`（8 项，含旧配置迁移 / 损坏 JSON / 原子写不覆盖他段）。

  **④ UI 层 · 接入卡 + 二级菜单（修复 bug）**
  - **修复核心 bug**：此前 `ui/ConversationPage.qml` **硬编码** `model: ["openai","anthropic"]`，故下拉只两家、且「Anthropic」宽度不足被截成「An」。现改为 **完全由 `Chat.providerOptions()` / `Chat.modelOptions()` 驱动**，7 家厂商全名正常显示（下拉宽度 176）。
  - 接入卡改造：**厂商下拉 → 模型下拉 → Key 输入 + 保存 / 清除 / 验证 + 三态 chip**；`custom` 时露出**手填 API 链接 / 模型名**两行。
  - 新增 `ui/ModelMenu.qml`（Popup 二级菜单，已登记 CMakeLists）：**悬停约 1s（Timer）/ 单击 / 长按** 三种入口（Android 触屏无 hover，必须有回退）；行内**档位胶囊**点击 = 选模型 + 设档位；底部「高级原生参数」区（JSON TextArea + 重置 / 应用 + 「当前生效」chip + 解析错误提示）。
  - 底部「当前」条：`modelPill`（HoverHandler + 1s Timer + TapHandler `onTapped`/`onLongPressed`）显示模型 / 档位 / 视觉 / 高级参数摘要。
  - **图片入口按 `Chat.supportsVision` 启停**，不支持时显示「该模型未声明视觉能力，附图入口已关闭」；`submit()` 仅在 `supportsVision` 时走 `sendWithImage`。
  - 接入卡高度自适应展开；**QML 只调 `Chat` 的 Q_INVOKABLE，不解析 JSON / 不碰网络 / 不碰密钥 / 不碰存储**。

- **自测**：本地 Debug 构建通过；`ctest -C Debug` **11/11 全部通过**（含新增 `tst_providercatalog` 10 项、`tst_aiconfigstore` 8 项、`tst_provider` 扩展至 31 项：自定义 baseUrl+Bearer、逐厂商鉴权头、Anthropic 兜底头、nativeParams 注入不覆盖核心字段、Valid/Invalid/429/超时/断网/NotConfigured/chat-minimal 三态、清单解析、不支持厂商拒绝）；`MeowAi.exe --smoke-test` 退出码 0；**运行期 QML 警告 / 错误数为 0**（用 `QT_LOGGING_RULES` 正控制证明 stderr 管道有效后，实测空输出）。
- **官方文档出处（硬性，逐一核对）**：
  - OpenAI：`https://platform.openai.com/docs/api-reference/introduction`（baseUrl `/v1` 与 Bearer）、`https://platform.openai.com/docs/api-reference/models`（`GET /models` 清单）、`https://openai.com/index/introducing-gpt-5-for-developers` + `https://openai.com/index/o1-and-new-tools-for-developers`（`reasoning_effort`）
  - DeepSeek：`https://api-docs.deepseek.com/`（baseUrl、Bearer）、`https://api-docs.deepseek.com/api/list-models`（`/models`）、`https://api-docs.deepseek.com/guides/thinking_mode`（思考开关）
  - Kimi（月之暗面）：`https://platform.kimi.com/docs/api/chat`、`https://platform.kimi.com/docs/api/list-models`（`/models`、baseUrl `api.moonshot.cn/v1`）、`https://platform.kimi.com/docs/models`
  - 通义千问（阿里云百炼）：`https://help.aliyun.com/zh/model-studio/base-url`、`https://help.aliyun.com/zh/model-studio/qwen-api-via-openai-chat-completions`（OpenAI 兼容 baseUrl、`enable_thinking`）、`https://help.aliyun.com/zh/model-studio/deep-thinking`、`https://help.aliyun.com/zh/model-studio/models`
  - 智谱 GLM：`https://docs.bigmodel.cn/cn/api/introduction`（baseUrl `open.bigmodel.cn/api/paas/v4`、Bearer）、`https://docs.bigmodel.cn/cn/guide/capabilities/thinking`（`thinking.type`）、`https://docs.bigmodel.cn/cn/guide/start/model-overview`
  - Anthropic：`https://platform.claude.com/docs/en/api/models/list`（`GET /v1/models`）、`https://platform.claude.com/docs/en/build-with-claude/extended-thinking`（`thinking.type` = `enabled|disabled|adaptive`）、`https://platform.claude.com/docs/en/api/versioning`（`anthropic-version: 2023-06-01`）
- **遗留 / 待定**：
  - 真实各家联网对话与「图片输入」需有效 Key 手测（DoD 第 3 条，待人工验证）。
  - **模型清单为内置精选**（版本号按当前口径拟写：`gpt-5/4o`、`deepseek-v4-pro/flash`、`qwen3.8-max/plus/flash/vl-plus`、`kimi-k3/k2.6`、`glm-5.3/4.6/4.5-air/4.6v`、`claude-opus-5-5/sonnet-5/haiku-4.5`）——厂商发新版本时**改 `providers.json` 即可**；已提供「从厂商接口动态拉取」按钮（仅 `validation.method=="models"` 的厂商启用）。
  - 仓库未配置 GitHub 远端，`v0.2.3` PR 待补。
- **备注**：Key 仍**只**存 `SecureStore` 的 `provider.<id>.api_key`；错误信息 / 日志**不得含 Key 或完整请求体**；厂商 / 模型等非敏感选择只落 `config.json` 的 `ai` 段。版本口径统一为 **v0.2.3**。

### 2026-10-20 ｜ 总代理验收：会话历史模型 / 主题系统 / 无边框标题栏 ｜ 状态：已验收（初步）
- **阶段**：阶段二（收尾）——注：此前条目把这三个模块标为"阶段三"，**按当前口径归回阶段二收尾**。
- **负责人**：总代理（审查与验收，不写代码）
- **分支 / PR**：`v0.2.1`（PR 待创建）
- **交付内容**：对模块 ①②③ 做初步验收（看实现与证据，未逐行深审），**结论：通过**。
  - ① **会话历史**：`core/chat/ChatHistoryModel`（`QAbstractListModel`）+ `storage/ChatStore` **复用既有 SQLite `Database`**（未新建存储机制）；schema v1 建 `conversations` / `messages` / `message_attachments` 并为资料库预留位；流式 delta 节流落库、终局 finalize；`reload()` 将中断的 streaming 消息降级为 partial。`tests/tst_chatstore` 覆盖 schema / CRUD / **重启后保留** / 中断降级 / DB 不可用降级 / 有界上下文 → 证据充分。
  - ② **主题系统**：`ui/Theme.qml` 令牌已由 `readonly` 改为**可变**，由 `ThemePresetManager` 以 `setProperty` 动态驱动；预设为数据文件 `resources/themes/theme1.json`（**主题1 = Nocturne 琥珀 = 当前配色 = 默认**）；选择持久化于 `config.json` 的 `theme.preset`；个性化页可列出并切换预设。
  - ③ **标题栏**：`supportsCustomTitleBar()` Desktop=true / Android=Web=false；`TitleBarHelper.cpp` 以 `#if defined(Q_OS_WIN)` 隔离，处理 `WM_NCHITTEST` / `WM_NCCALCSIZE` / `WM_GETMINMAXINFO`，非 Windows 返回 `nullptr`；`ui/main.qml` 按能力切 `FramelessWindowHint` 且带 `resizeFrame` 兜底。另修复 `WIN32_EXECUTABLE TRUE`，消除多余控制台窗口。
- **自测**：抽查证据如上；`--smoke-test` 支持存在。
- **遗留 / 待定**：
  - **组件硬编码颜色需抽查确认**（重点 `ui/MButton.qml` 等），否则换主题会漏色。
  - Android Keystore 未真机验证（留阶段四）。
  - **版本号口径不一致**：DEVLOG 记 `v0.2.1`、阶段三提示词曾建议 `v0.3.0`，建议**统一为 `v0.2.1`**（按"仍属阶段二"口径）。
- **备注**：**PR 尚未创建，DoD 第 3 条（PR 可见 diff）未满足**；补 PR 后本条可转"正式验收"。

### 2026-10-19 ｜ 无边框自绘标题栏（阶段三·模块③）｜ 状态：待验收
- **阶段**：阶段三（会话历史模型 + 主题系统 + 无边框标题栏）
- **负责人**：开发 Agent（待补充）
- **分支 / PR**：`v0.2.1`（PR 待创建）
- **交付内容**：
  - `platform/PlatformAdapter.h` 新增纯虚能力标志 `supportsCustomTitleBar()`：Desktop = `true`，Android / Web = `false`（**非桌面端明确回退系统原生标题栏，不做无边框**）
  - 新增 `platform/TitleBarHelper.h` + `platform/impl/TitleBarHelper.cpp`：Windows 分支为原生 `QAbstractNativeEventFilter`，处理 `WM_NCHITTEST`（外圈 6px 返回缩放边框码）/ `WM_NCCALCSIZE`（返回 0，去系统边框）/ `WM_GETMINMAXINFO`（最大化贴合工作区）；补 `WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU` 并以 `DWMNCRP_DISABLED` 关闭 DWM 非客户区绘制；**非 Windows 返回 nullptr**，交由 QML 侧兜底
  - 新增 `ui/TitleBar.qml`：自绘标题栏（`startSystemMove()` 拖拽、双击切换最大化还原、最小化 / 最大化还原 / 关闭三个按钮，关闭按钮危险色）
  - `ui/main.qml`：`flags` 按平台能力切换 `FramelessWindowHint`；插入自绘标题栏并抬高内容区；新增 `resizeFrame`（8 个 6px 边条 MouseArea 调用 `startSystemResize`）作为原生命中的**第二重保险**，杜绝「窗口无法拖动 / 无法关闭」
  - `main.cpp`：界面加载后按 `customTitleBar()` 安装 `MeowAiPlatform::installTitleBarHelper(window)`
  - **工程修复**：`CMakeLists.txt` 为可执行程序设 `WIN32_EXECUTABLE TRUE` —— 修复「打开 Demo 时后面弹出命令行窗口、关掉命令行 Demo 也跟着关」的问题，现在**只出现一个 Demo 窗口**
- **自测**：本地 Debug 构建通过；`ctest` **9/9** 通过；`MeowAi.exe --smoke-test` 退出码 0；**运行期 QML 警告数为 0**
- **遗留 / 待定**：标题栏拖拽 / 缩放 / 最大化还原 / 关闭的**人工点击验证**待图形界面手测（DoD 第 3 条）；Linux / macOS 无原生助手、仅 QML 兜底（项目当前不交付这两端）
- **备注**：标题栏在任何情况下**都不得出现无法拖动或无法关闭**的情形

### 2026-10-19 ｜ 主题系统（阶段三·模块②）｜ 状态：待验收
- **阶段**：阶段三（会话历史模型 + 主题系统 + 无边框标题栏）
- **负责人**：开发 Agent（待补充）
- **分支 / PR**：`v0.2.1`（PR 待创建）
- **交付内容**：
  - `ui/Theme.qml` 全部设计令牌由 `readonly property` 改为 `property` —— 令牌可**运行期改写**且 QML 属性自带变更通知，绑定即时刷肤；新增 17 个令牌（`overlayFaint/overlaySubtle/overlayHover/overlayStrong`、`shadow`、`accentShadow`、`dangerPress/dangerHair/dangerHairStrong`、`okGhost/okHair`、`navRail`、`stageTop/stageMid/stageBottom`、`splashScrimTop/splashScrimBottom`）；旧令牌 `background/surface/foreground/muted/spacing` 保留为兼容绑定
  - 新增主题预设资源 `resources/themes/theme1.json`（`Nocturne 琥珀`，深色，与原配色一致）与 `theme2.json`（`Daylight 浅蓝`，**浅色系**）；两文件令牌集**完全一致**（44 色 + 22 整数 + 2 缓动 = 68），色值统一 `#AARRGGBB`
  - 新增 `storage/ThemeStore`：只读写 `config.json` 的 `theme` 段（键 `preset`），**读改写回保留其他模块的键**，`QSaveFile` 原子写，损坏 JSON 以空对象重建
  - 新增 `core/theme/ThemePresetManager`：扫描 `:/themes/*.json`、`selectPreset(id)`、`attachThemeObject(QObject*)`（用 `QObject::setProperty` 逐令牌写入 QML 单例）、**令牌契约漂移检测**（预设缺令牌即报错）；刻意**不依赖 Qt Qml 模块**，避免 core 反向依赖 ui
  - `core/theme/ThemeManager` **保持纯个性化职责**（字体 / 背景 / 头像 / 开屏），**不改名、不合并**
  - 装配：`main.cpp` 用 `engine.singletonInstance` 解析 QML 单例 `Theme` 并注入 `attachThemeObject`；新增上下文属性 `ThemePreset`
  - `ui/PersonalizationPage.qml`：新增「主题」卡（预设按钮 + 当前选中态 + 错误提示）
  - **硬编码色收归**：`ui/` 下除 `Theme.qml` 的令牌定义外**无残留 hex 色**（`MButton` / `MSurface` / `MChip` / `SplashVideo` / `ConversationPage` 等全部改用令牌）
- **自测**：本地 Debug 构建通过；`ctest` **9/9** 通过；`--smoke-test` 退出码 0；QML 警告 0。新增 `tests/tst_themepreset.cpp`（6 项）：预设解析（多种取值形态）、损坏输入拒绝、令牌写入并上报漂移、加载并持久化选择、未知预设拒绝、**无存储依赖时保持默认不崩**
- **遗留 / 待定**：运行期换肤（深色 ↔ 浅蓝）的**目视效果**待人工手测
- **备注**：新增令牌**必须同步补进所有预设**，否则漂移检测会报错；主题持久化与个性化配置同存 `config.json` 但**键段互不覆盖**

### 2026-10-19 ｜ 会话历史模型（阶段三·模块①）｜ 状态：待验收
- **阶段**：阶段三（会话历史模型 + 主题系统 + 无边框标题栏）
- **负责人**：开发 Agent（待补充）
- **分支 / PR**：`v0.2.1`（PR 待创建）
- **交付内容**：
  - `storage/Database`：`ensureSchema` 重写为**幂等前向迁移** —— 连接级 `PRAGMA foreign_keys = ON` / `journal_mode = WAL` / `busy_timeout = 3000`，版本记录于 `meta.schema_version`；v1 迁移建 `conversations` / `messages` / `message_attachments` 与索引 `idx_messages_conversation`（仅前向、不回退、不删除既有表）
  - 新增 `storage/ChatStore`：会话历史记录读写（会话增 / 删 / 改名 / 触碰时间，消息增 / 查 / 改，中断遗留 `streaming` 降级 `partial`），**只做记录读写、不含业务判断**；时间戳统一**毫秒级**并保证被触碰会话严格排到最前；null 字符串统一规范为空串（避免 `QString()` 被绑定成 SQL `NULL` 触发 `NOT NULL` 约束失败）
  - 新增 `core/chat/ChatHistoryModel`（`QAbstractListModel`）：角色 `role/content/messageId/status/createdAt`；`Q_INVOKABLE` 新建 / 打开 / 重命名 / 删除会话；**流式 400ms 节流落库 + 终局落库**；首条用户消息自动生成会话标题；`requestHistory(maxMessages, maxChars)` 输出**有界上下文**（跳过空占位与 `error` 行，条数 / 字符双限）；依赖缺失时构造即给出明确错误、**安全降级不崩**
  - `core/chat/ChatController` 扩展：新增 `dispatchUserMessage()` —— 发送时写入用户消息 + 创建助手占位、请求携带**有界历史**；流式增量 / 终局 / 失败（取消→`partial`，其余→`error`，**保留已生成内容**）分别落到模型
  - `core/AppController` / `main.cpp`：装配 `ChatStore` + `ChatHistoryModel` 并 `reload()`，注入上下文属性 `History`
  - `ui/ConversationPage.qml`：新增**左列会话列表**（＋新对话 / 点击切换 / 悬停删除 / 当前高亮）与消息流改绑**真实模型**（流式呼吸竖线、`error` 危险色、空态、不可用提示）；**UI 只绑定模型，不直连数据库**
  - 上述 UI 全部落在 `ui/` 模块内，便于后续在 ui 模块内自定义各组件位置
- **自测**：本地 Debug 构建通过；`ctest` **9/9** 通过（新增 `tests/tst_chatstore.cpp` 7 项：表结构幂等迁移、会话 CRUD 与排序、消息生命周期**跨重开保留**、流式节流后落库、中断遗留降级 `partial`、上下文有界裁剪、**无数据库降级不崩**）；`--smoke-test` 退出码 0；QML 警告 0
- **遗留 / 待定**：多轮上下文与历史保留的**真实联网手测**需有效 Key（DoD 第 3 条）；图片仅补在末条、**不落库**（图片附件表已预留，待后续模块）
- **备注**：`messages` 通过外键 `ON DELETE CASCADE` 随会话一并清理；历史对话**持久保留**，可随时**新开对话**，上下文交互保持流畅（有界裁剪）

### 2026-10-19 ｜ 前端精细化重设计（UI 层）｜ 状态：待验收
- **阶段**：阶段二（核心基础模块）
- **负责人**：开发 Agent（待补充）
- **分支 / PR**：`v0.2.0`（PR 待创建；**当前仓库尚未配置远端**）
- **交付内容**：
  - `ui/Theme.qml` 重写为**完整设计令牌系统**（视觉方向：Nocturne / 琥珀猫眼）——墨蓝舞台色阶 `ink0–ink4`、8%/15% 两级发丝描边、唯一一道暖琥珀强调色 `accent`（+ 高光 / 深压 / 叠层 / 描边变体）、语义色 `ok/warn/danger`、双色背景光晕、圆角 `rSm–rPill`、间距 `s1–s7`、字号 `fLabel–fDisplay`、动效时长与缓动；**保留旧令牌** `background/surface/foreground/muted/spacing` 指向新语义，兼容既有引用
  - 新增**自绘组件库**（全部平铺于 `ui/`，依赖同目录隐式导入，规避子目录 qmldir 风险）：`MSurface`（卡片：填色 / 发丝描边 / 可选柔光投影 / 悬停增亮）、`MLabel`（大写宽字距微标签，版式节奏基底）、`MButton`（primary/ghost/quiet/danger 四变体 + small 尺寸，含按压 0.97 缩放与主行动琥珀外发光）、`MField`（凹槽墨底，聚焦点亮为琥珀描边）、`MCombo`（完全自绘下拉 + 墨色弹层，含未选中占位）、`MSwitch`（纯自绘轨道 + 圆钮）、`MChip`（胶囊状态标签，含状态点）、`MGlow`（高斯模糊柔光晕）、`EyeMark`（`QtQuick.Shapes` 绘制的猫眼品牌标志：杏仁轮廓 + 竖瞳）、`MAvatar`（`MultiEffect` 圆形遮罩头像，无图时退化为琥珀首字母徽记）
  - 新增 `ui/ConversationPage.qml`：**可折叠接入卡**（厂商 / 模型 / 密钥 / 图片上传显式确认开关，头部常驻显示 provider 与密钥状态 chip）+ **消息流**（用户右侧玻璃卡片、助手左侧带琥珀竖线的气泡，各带 `MEOWAI` / 你 微标签）+ **流式气泡**（生成期间贴尾，竖线呼吸脉动）+ **输入条**（附图路径 + 发送 / 中断）+ 错误横幅 + 空态
  - `ui/main.qml` 重写为**应用外壳**：墨色渐变底 + 暖冷双光晕 + 用户自定义背景（其上压暗层保证可读）+ **左侧导航栏**（品牌标志 / wordmark / 导航项带琥珀指示条 / 底部运行平台与状态元信息）+ **顶栏**（页面标题 + 平台 chip + 头像）+ `StackLayout` 两页切换 + 开屏覆盖层（区分「启动播放」与「手动预览」，避免改设置后反复自动播放）
  - `ui/PersonalizationPage.qml` 由**全屏覆盖层**改为**导航页**：字体（含未选中占位，不再误把首项当当前字体）、背景 / 头像（预览缩略图）、开屏动画（状态 chip + 预览按钮 + 两个开关）、重置；新增 `previewSplash()` 信号交由外壳播放
  - `ui/SplashVideo.qml` 精修：淡入、上下渐变压暗、底部品牌行 + 琥珀播放进度条、「跳过」胶囊（悬停增亮）、载入中提示
  - **工程修复 ①**：`main.cpp` 强制 `QQuickStyle::setStyle("Basic")` —— Windows 上 Qt 6 默认的 native（Windows11）样式**拒绝** QML 侧的 `background` / `contentItem` / `indicator` 定制，全自绘 UI 在其上会完全失效；CMake 新增 `QuickControls2` 组件，**仅链接可执行程序，不进 core 层**
  - **工程修复 ②**：`CMakeLists.txt` 用 `set_source_files_properties(ui/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)` 显式声明单例 —— 否则生成的 `qmldir` 会把它登记为普通类型，`Theme.<token>` 全部解析为 `undefined`（此前一直存在，只是旧 UI 简单、未暴露）
  - **工程修复 ③**：令牌 `onAccent` 改名 `accentInk` —— QML 会把 `on<大写开头>` 形式的赋值当作信号处理器解析，导致单例编译失败
  - `CMakeLists.txt` 的 `qt_add_qml_module(... QML_FILES ...)` 补齐全部新增 QML
- **自测**：本地 Debug 配置 + 构建通过；`ctest` **7/7** 通过；`MeowAi.exe --smoke-test` 退出码 0；**运行期 QML 警告数为 0**；另用 `PrintWindow` 抓取窗口位图逐页目视检查（对话页空态 / 接入卡展开 + 消息气泡 / 个性化页各卡片），确认布局、配色、间距与交互态（悬停、禁用、折叠）符合设计意图
- **遗留 / 待定**：
  - **core 层目前只暴露单个流式 `replyText`，没有会话历史模型**，因此对话页的消息列表是 **QML 侧临时 transcript**（进程内有效、重启即清空）——待 core 提供真实历史模型后替换
  - 窗口标题栏仍为系统原生样式（未做无边框自绘标题栏）
  - 界面不覆盖 `font.family`（用户自定义字体优先），若需品牌字体应在个性化模块内补默认字体
- **备注**：UI 层只做展示与绑定，业务仍全部经 `App` / `Chat` / `Personalize` 转发；本次改动仅涉及 `ui/`、`main.cpp` 与 `CMakeLists.txt` 的 QML 登记，`core/` / `providers/` / `storage/` / `platform/` **零改动**

### 2026-10-19 ｜ 个性化模块（功能 #11）｜ 状态：待验收
- **阶段**：阶段二（核心基础模块）
- **负责人**：开发 Agent（待补充）
- **分支 / PR**：`v0.2.0`（PR 待创建；**当前仓库尚未配置远端**）
- **交付内容**：
  - 新增 `storage/PersonalizationStore`：只读写 `config.json` 的 `personalization` 段，读改写回时**保留其他模块写入的键**；`QSaveFile` 原子写；资源路径白名单校验（拒绝绝对路径 / 盘符 / `~` / 路径穿越 `..`），非法路径读取时忽略、写入时直接拒绝
  - `core/theme/ThemeManager` 由骨架改为真实实现（**个性化配置中心，不与其他模块杂糅**）：
    - **字体**：`availableFontFamilies()` 列系统字体 + `setSystemFont()` 选用系统字体 + `importFont()` 导入 `.ttf/.otf/.ttc`（复制进 `assets/fonts` 并 `addApplicationFont` 注册）；字体经 `QGuiApplication::setFont` **全局生效**
    - **背景 / 头像**：`setBackground()` / `setAvatar()`，图片白名单（png/jpg/jpeg/webp/bmp/gif/svg），20 MB 上限，复制进 `assets/backgrounds|avatars`
    - **开屏动画**：`setSplash()` 选定**视频文件**（mp4/mov/m4v/webm/mkv/avi，200 MB 上限），复制进 `assets/splash`；`splashEnabled` / `splashSoundEnabled` 分别控制是否播放与**音效开关**
    - `clearX()` / `resetAll()` 恢复默认并清理已导入资源；**持久化失败即回滚内存状态**（设置只在真正落盘后才生效），依赖未就绪时明确拒绝写入
  - **UI**：新增 `ui/PersonalizationPage.qml`（纯展示 + `FileDialog` 选择，业务全部转发给 `Personalize`）与 `ui/SplashVideo.qml`（`MediaPlayer` + `VideoOutput` + `AudioOutput` 播放开屏视频及音轨，含「跳过」按钮，结束 / 无法播放自动卸载）；`ui/main.qml` 接入自定义背景、开屏覆盖层与设置入口；`ui/Theme.qml` 移除静态 `fontFamily`（字体改由 ThemeManager 全局驱动）
  - **装配**：`AppController` 新增 `personalizationStore()` 与 `PersonalizationStore` 成员，`initialize()` 中装配并 `load()`；`main.cpp` 注入上下文属性 **`Personalize`**（刻意避开 QML 单例 `Theme` 的命名冲突）
  - **依赖**：开屏视频 + 音效依赖 Qt Multimedia，CMake 新增 `Multimedia` 组件（**仅链接到可执行程序，不进 core 层**）；CI `.github/workflows/ci.yml` 的 Qt 模块列表补 `qtmultimedia`
- **自测**：本地 Debug 配置 + 构建通过；`ctest` **7/7** 通过（新增 `tests/tst_theme.cpp`，16 项）；`MeowAi.exe --smoke-test` 退出码 0；额外用 `qml.exe` 验证 QtMultimedia QML 插件可加载、`MediaPlayer` 可实例化（并做反向对照确认该验证有效）
  - 异常边界：config.json **缺失 / 损坏（非 JSON）**→ 回落默认且不崩；资源文件丢失 → 回落默认；源文件不存在；扩展名非法；**空文件**；**超大文件（21 MB）**；**路径穿越**（绝对路径 / 盘符 / `..`）；**依赖为空（null store）**；系统未安装字体；无界面环境下导入字体
  - 隔离性：写入个性化设置后 `config.json` 中其他模块的键（如 `model.provider`）**保持原样**
- **遗留 / 待定**：开屏视频的**实际画面与音效播放**、系统字体选择、背景 / 头像切换需在图形界面手测（DoD 第 3 条，待人工验证）；Android / iOS 端多媒体后端与视频编解码支持留阶段四验证
- **备注**：Key 仍**只**存 `SecureStore` 保险库，`config.json` 仅含 `personalization` 段；个性化资源属「用户文件类数据」，**不出本地、不上传**；`assets/` 与 `config.json` 已在 `.gitignore`；本模块与 AI 接入 / 安全 / 资料库模块**零耦合**

### 2026-10-19 ｜ 阶段二：AI 接入（功能 #3 / #4）｜ 状态：待验收
- **阶段**：阶段二（核心基础模块）
- **负责人**：开发 Agent（待补充）
- **分支 / PR**：`v0.2.0`（PR 待创建；**当前仓库尚未配置远端**）
- **交付内容**：
  - `providers/ChatTypes.h`：扩展内容块（文本 / 图片）、`ChatUsage`（token 用量）、结构化 `ChatError{code, httpStatus, message}`；新增 `ChatRequest::stream` 与 `imagesConfirmed`（图片上传的**显式确认开关**，默认 false）
  - `providers/AiProvider.h`：失败信号由裸字符串改为结构化 `chatFailed(const ChatError&)`；注册自定义元类型（`ChatError` / `ChatResponse` / `Protocol`）
  - 新增共享件：`SseStreamParser`（W3C SSE 增量解析，跨分片、CRLF、注释行、多行 data）、`NetworkTransport`（抽象 + `QNetworkTransport` 基于 `QNetworkAccessManager`，带超时、失败分类 `Timeout/NetworkUnavailable/Cancelled/Other`）、`ProviderUtils`（默认域名、图片读取 / MIME 推断 / 脱敏）
  - `OpenAiProvider`：真实 Chat Completions（`/chat/completions`、`Bearer` 鉴权、SSE `delta`、图片 `image_url` 的 `data:` base64 URI、非流式整包解析与 usage）
  - `AnthropicProvider`：真实 Messages API（`/messages`、`x-api-key` + `anthropic-version`、system 顶层字段、`max_tokens` 必填默认值、SSE `content_block_delta`、图片 `image/source.base64`）
  - 取消：`cancel()` 调用 `NetworkTransport::abort()` **真正中断在途请求**，并发 `Cancelled` 结构化错误
  - `core/chat/ChatController`：Key 只从 `SecureStore` 读取、组装请求、流式增量与错误回调；`ui/main.qml` 增加最小可手动验证入口（选 Provider / 存 Key / 图片确认开关 / 发送 / 中断 / 显示与错误）
  - `ProviderFactory::create()` 增加可选 `NetworkTransport*` 注入点，便于离线单测
- **自测**：本地 Debug 构建通过；`ctest` 6/6 通过；`MeowAi.exe --smoke-test` 退出码 0（QML 可加载）。新增 `tests/tst_sse.cpp`（8 项）与 `tests/tst_provider.cpp` 扩展（14 项，经 `FakeNetworkTransport` 离线复现）：缺失 Key、401、429、超时、断网、**流中断**、**空响应**、取消中断、图片未确认被拒、OpenAI/Anthropic 图片编码格式
- **遗留 / 待定**：真实厂商联网对话与"图片输入"手测需有效 Key，待人工验证（DoD 第 3 条）；`MeowAiLib` 拆分层 target 仍留阶段四；GitHub 远端待配置
- **备注**：Key 仅经 `SecureStore`、日志不打印 Key 与完整请求体；图片仅在用户勾选确认后才上传，杜绝静默上传

### 2026-10-19 ｜ 阶段二：安全机制（功能 #5）｜ 状态：待验收
- **阶段**：阶段二（核心基础模块）
- **负责人**：开发 Agent（待补充）
- **分支 / PR**：`v0.2.0`（PR 待创建；**当前仓库尚未配置远端**）
- **交付内容**：
  - `platform/`：新增系统级加密原语 `supportsSecretEncryption()` / `protectSecret()` / `unprotectSecret()`（替换原 `writeSecret/readSecret/removeSecret`），把"加解密能力"与"文件读写"分层
    - Windows：DPAPI 用户作用域 + 应用附加熵（CMake 链接 `crypt32`）
    - Android：AndroidKeyStore 中 AES-256-GCM（JNI，`#if defined(Q_OS_ANDROID)` 内编译；本机/CI 无 Android 构建，**真机验证留阶段四**）
    - Web / Linux / macOS：明确返回"不支持"，**不降级、不明文落盘**
  - `storage/SecureStore`：落地真实实现——保险库目录 `<AppData>/secure/`、密文文件 `<key>.vault`、Key 名白名单校验（仅 `[a-z0-9._-]`、首字符字母数字，杜绝路径穿越）、`has/save/load/erase`、`QSaveFile` 原子写入、属主读写权限
  - 语义修正：`isAvailable()` 现在仅表示"**本平台具备真实加密能力**"（不再误导上层认为"已加密安全"）；新增 `backendName()` 供 UI 明示后端；新增 `providerApiKeyId()` 统一多 Provider Key 命名（`provider.<id>.api_key`）
  - 前置清理：`ProviderFactory` 改为返回 `std::unique_ptr<AiProvider>`（去裸指针，方案 A）
  - `AppController`：装配 `SecureStore`，状态栏追加"密钥后端"信息
- **自测**：本地 Debug 构建通过；`ctest` 5/5 通过。新增 `tests/tst_securestore.cpp`（13 项）：加解密往返、多 Provider Key 独立、**密文不含明文**、缺失文件、密文损坏、非法 Key 名（含越权路径）、平台不支持时拒绝写入、空值拒绝、幂等删除、null 平台不崩溃
- **遗留 / 待定**：Android Keystore 未真机验证（阶段四）；仓库未配置 GitHub 远端，PR 待补
- **备注**：异常流（无 Key / 错 Key / Key 文件损坏）均返回明确错误且不崩溃，符合 DoD 第 2 条

### 2026-10-19 ｜ 阶段一：底层架构搭建 ｜ 状态：待验收
- **阶段**：阶段一（规划与底层架构）
- **负责人**：待补充
- **分支 / PR**：待补充
- **交付内容**：
  - 可编译的 Qt 6（QML + C++）工程 + CMake；目录分层 `ui/ core/ providers/ storage/ platform/ resources/skills/ tests/` 落地
  - 三个抽象接口：`AiProvider`、`Skill`（`Skill.h` + `SkillRegistry`）、`DocumentRetriever`（**仅预留接口**，`NullRetriever` 为空桩）
  - 存储骨架：SQLite 封装 `Database`、`AppPaths`、`SecureStore`（Key 加密**占位**）
  - 平台适配：`Desktop` / `Android` / `Web` 适配器 + 工厂
  - 单元测试 4 项（storage / skill / retriever / provider）；GitHub Actions CI（Windows + Qt 6.8.3 + ctest）
- **自测**：工程可构建；CI 可跑
- **遗留 / 待定**：`OpenAiProvider` / `AnthropicProvider` 为**桩**（阶段二接入真实协议）；资料库检索算法、蓝奏云升级、Agent 式文件操作**均未实现**（符合阶段一"明确不做"清单）
- **备注**：总代理**初验收通过**——骨架达标。提示：CI 仅在 `v*` 分支触发，注意按版本号命名分支。
