# MeowAi 开发人员手册

> 版本：**v0.2.5**（阶段二收尾）
> 读者：本项目 3 名开发者及其 Agent
> 规范优先级：`PROJECT_CHARTER.md` > 本手册 > 其他文档；规范冲突以 Charter 为准。

---

## 1. 快速开始

```powershell
# 配置（Windows，Visual Studio + Qt 6.5+）
cmake -S . -B build

# 可选：启用 PDF 文本层解析（PDFium 预编译产物，仓库不含二进制）
pwsh scripts/fetch-pdfium.ps1
cmake -S . -B build     # 重新配置以链接 PDFium

# 构建 / 测试
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

- 可执行产物：`build/Debug/MeowAi.exe`
- 冒烟自检：`MeowAi.exe --smoke-test`（退出码 0 为通过）
- 当前测试规模：**22 个测试目标，全部通过**（总代理实测 22/22）

依赖组件：`Core Gui Qml Quick Network Sql Test Multimedia QuickControls2`。
`Multimedia` 与 `QuickControls2` **只链接可执行程序，不进 core 层**。

---

## 2. 分层与依赖方向（硬约束）

```
ui/        (QML，纯展示与绑定)
  ↓ 只能通过 App / Chat / Library 等上下文属性
core/      (业务编排：chat / library / retriever / web / theme / skill / update)
  ↓
providers/ (AI 协议、SSE、网络传输、Provider 工厂)   ← 唯一出网层
storage/   (SQLite、配置、密钥、主题、资料库持久化)
platform/  (DPI/路径/加密原语/标题栏 等系统能力)
```

规则：
- **QML 不直连存储与网络**；业务一律经 `AppController` 注入的上下文属性转发。
- **所有 HTTP 必须经 `providers/NetworkTransport`**（生产 `QNetworkTransport`，单测注入 `tests/fakes/FakeNetworkTransport.h`）。
- 系统级操作（DPAPI、Keystore、Win32 消息）只允许在 `platform/`。
- 新增第三方依赖（如 PDFium）**默认只进可执行程序或特定层**，不得污染 core。

---

## 3. 模块地图

### 3.1 core

| 模块 | 关键类 | 职责 |
|---|---|---|
| 组合根 | `AppController` | 装配全部子系统，向 QML 注入 `App/Chat/Library/Personalize` 等 |
| 对话 | `ChatController`、`ChatHistoryModel` | 组装请求、流式增量、会话切换、检索/工具上下文注入 |
| AI 目录 | `ai/ProviderCatalog` | 读取 `resources/providers.json`，厂商/模型/能力/思考档位 |
| 资料库 | `LibraryController`、`CollectionManager`、`LibraryIndexer`、`LibraryManager` | 小库、导入、关联文件夹、索引编排、状态机 |
| 解析 | `library/extract/*`、`ExtractorRegistry` | 注册式 extractor：txt/md/csv/json/pdf/docx/xlsx/pptx/image/video(占位) |
| 检索 | `RagRetriever`、`ChunkSplitter`、`EmbeddingService`、`DocumentRetriever` | 向量 + 关键词混合检索、上下文组装 |
| 图片 | `ImageCaptionService`、`ImageCaptionExtractor`、`ImageTranscoder` | 多模态描述入库、格式转码 |
| 成本 | `CostEstimator` | 首次建索引 / 每轮注入的成本估算 |
| 联网 | `WebSearchService`、`HttpWebSearchProvider`、`WebSearchTool` | 应用侧检索抓取；部分厂商支持模型工具调用循环 |
| 主题 | `ThemeManager`、`ThemePresetManager` | 主题令牌驱动、预设加载 |
| Skill | `Skill`、`SkillRegistry` | **接口与扫描已就绪，调用尚未接入对话** |
| 升级 | `UpdateManager` | **骨架，未实现** |

### 3.2 providers

| 类 | 说明 |
|---|---|
| `AiProvider` | 纯抽象：`sendChat`（流式）、`cancel`、能力声明 |
| `OpenAiProvider` | OpenAI 协议；同时承载所有 **OpenAI 兼容厂商**（DeepSeek/Qwen/Kimi/GLM/自定义） |
| `AnthropicProvider` | Anthropic Messages 协议 |
| `ProviderFactory` | 按 `protocol` 路由，返回 `std::unique_ptr<AiProvider>` |
| `ProviderCatalog` | 厂商与模型清单（JSON 数据驱动） |
| `SseStreamParser` | SSE 增量解析（OpenAI `choices[].delta` / Anthropic `content_block_delta`） |
| `NetworkTransport` | 统一出网抽象 + 可注入 fake |
| `ProviderUtils` | 鉴权头、错误归一化（401/403 → 无效；429/超时/断网 → 无法验证） |

### 3.3 storage

| 类 | 说明 |
|---|---|
| `Database` | schema 建表 + 版本迁移；**唯一 SQLite 入口** |
| `ChatStore` | 会话与消息 CRUD、streaming 消息 finalize / 降级 partial |
| `LibraryStore` | 小库、文件、关联、文件夹快照、chunk、citation、检索日志 |
| `SqliteVectorIndex` | 向量 BLOB 读写 + 余弦计算（`VectorIndex` 接口可替换） |
| `SecureStore` | 加密保险库 `<AppData>/secure/<key>.vault`，Key 名白名单 + 原子写 |
| `AiConfigStore` / `LibraryConfigStore` / `PersonalizationStore` / `ThemeStore` / `WebConfigStore` | 各自配置段，写入时**保留其他段** |
| `AppPaths` | 所有本地路径的唯一来源 |

### 3.4 ui

`main.qml`（外壳/导航/标题栏/开屏）、`ConversationPage`、`LibraryPage`、`PersonalizationPage`、
`TitleBar`、`SplashVideo`、`LibraryConsent`、`WebSearchPanel`、`ModelMenu`，以及 `M*` 自绘组件库与 `Theme.qml`。

---

## 4. 数据库结构（schema v1）

| 表 | 用途 |
|---|---|
| `conversations` | 会话（标题、时间） |
| `messages` | 消息（role、内容、状态 streaming/complete/partial/error） |
| `message_attachments` | 消息附图 |
| `collections` | 小库 |
| `library_files` | 文件（来源 upload/linked、状态、mtime、hash、错误） |
| `file_collections` | 文件↔小库 多对多 |
| `folder_links` | 关联文件夹（快照时间） |
| `chunks` | 片段（文本、序号、元数据：页码/工作表/行号/图片序号） |
| `chunk_vectors` | 向量（BLOB、模型、维度） |
| `citations` | 检索引用（会话/消息 ↔ 片段） |
| `chunk_fts` (FTS5) | 关键词兜底索引 |

> 所有写入走 `Database`；**不要**在业务层直接开 `QSqlDatabase`。

---

## 5. 关键链路

### 5.1 对话
```
QML 发送 → ChatController
  ├─ 组装 system（当前时间，每轮现算，不入库）
  ├─ 若挂载小库 → RagRetriever 检索 → 注入上下文 + 记录 citations
  ├─ 若开启联网 → 支持工具调用的厂商进入工具循环
  ├─ ProviderFactory → OpenAiProvider / AnthropicProvider → NetworkTransport
  └─ SSE 流式 delta → ChatHistoryModel → ChatStore（节流落库）→ QML
```

### 5.2 资料库入库
```
导入 / 关联快照 → LibraryIndexer
  → ExtractorRegistry 选择 extractor（文本 / 图片 caption）
  → 结构化文本 + 元数据
  → ChunkSplitter（尺寸 + 重叠）
  → EmbeddingService（API）→ SqliteVectorIndex
  → FTS5 同步
状态机：待索引 → 已索引 / 待建向量 / 待索引(图片) / 无文本层 / 索引失败 / 内容已变化
```

### 5.3 检索
```
query → (embedding) → 余弦 Top-N 候选
      → 阈值过滤 → 每文件上限 → 邻居扩展 → token 预算截断 → 引用
      ↘ embedding 不可用 → FTS5 关键词（hybrid 可合并）
输出 RetrievalResult{mode, degraded, note, vectorHits, keywordHits, tokenEst}
```

---

## 6. 各功能原理与注意事项

### 6.1 AI 接入 / 多厂商
**原理**：`resources/providers.json` 描述厂商 `{id, displayName, baseUrl, authStyle, protocol, models[]}`。
`protocol=openai` 的全部复用 `OpenAiProvider`，**新增厂商 = 加一条 JSON**；`protocol=anthropic` 走 `AnthropicProvider`。

**注意**：
- 模型能力（vision / reasoning 档位）由 catalog 声明；**未声明的档位不得猜测**，UI 显示"未声明"。
- `reasoningLevel` 映射到厂商原生字段；高级参数可透传覆盖。
- Key 校验三态：**401/403 = 无效**；429/超时/断网 = **无法验证**——不得混为一谈。
- 模型清单可动态拉取，但**内置清单是兜底**，拉取失败不能报错。
- 旧配置里的自由 `model` 字符串必须能平滑迁移。

### 6.2 安全存储
Windows 用 DPAPI（用户作用域 + 应用熵）；Android 用 KeyStore AES-256-GCM（`#if Q_OS_ANDROID`）。
**平台无加密能力时明确拒绝写入，绝不降级明文**。`isAvailable()` 只表示"具备真实加密能力"。
日志**禁止**打印 Key、完整请求体、完整文件内容。

### 6.3 RAG 参数
`RetrievalConfig` 默认：`topK=6`、`perFileLimit=2`、`tokenBudget=2000`、
`similarityThreshold=0.30`、`neighborRadius=1`、`ftsFallback=true`、`maxCandidates=60`。
**这些参数改动后行为必须改变**（DoD 验收点，已有测试）。

**注意**：
- **每文件上限**用于防止大文件霸榜；**邻居扩展**用于补回被切断的语义。
- token 估算为保守近似，不是精确 tokenizer。
- 引用必须能定位到**文件名 + 页码/工作表/行号/图片序号**。
- 检索失败**不得中断对话**，只能降级并提示。

### 6.4 图片
图片**不做本地解析**：入库时调多模态模型生成描述（caption），描述参与切块与检索。
需**同时满足**：已配置视觉模型 + 已同意图片外发 + 模型声明 `vision`。
`.webp` 依赖 Qt imageformats 插件，缺失时会失败——需明确报错，不得静默。

### 6.5 联网搜索
两种形态并存：
- **A 形态**：应用侧 `WebSearchService` 先检索、抓取前 2 条正文，再拼进上下文。
- **B 形态**：厂商支持工具调用时，模型自主发起 `search/fetch`（有最大轮数上限）。
注意：`WebSearchProvider` 与 `DocumentRetriever` **刻意分离**（公网 vs 本地资料库），不要合并类型。

### 6.6 主题
`ui/Theme.qml` 令牌**可变**，由 `ThemePresetManager` 用 `setProperty` 驱动；预设是 `resources/themes/*.json`。
**令牌名是全局契约**：只能新增、不能改名；新增令牌必须同时补进所有预设。
QML 坑：`Theme.qml` 必须在 CMake 中声明为 **QML 单例**；令牌名不能写成 `onXxx`（会被当信号处理器）。
组件内**不得硬编码颜色**，否则换主题漏色。

### 6.7 会话历史
`ChatHistoryModel` 是 `QAbstractListModel`；流式 delta 节流写库，终局 finalize。
启动时把残留 `streaming` 降级为 `partial`。不要在 QML 侧维护 transcript。

### 6.8 个性化
`PersonalizationStore` 只读写 `config.json` 的 `personalization` 段，读改写回**保留其他段的键**。
资源路径必须白名单校验（拒绝绝对路径/盘符/`~`/`..`），图片 20MB、视频 200MB 上限。
落盘失败要**回滚内存状态**。

### 6.9 标题栏
`supportsCustomTitleBar()`：Desktop=true，Android/Web=false（自动回退）。
Windows 用 `QAbstractNativeEventFilter` 处理 `WM_NCHITTEST/NCCALCSIZE/GETMINMAXINFO`，QML 侧还有 `resizeFrame` 兜底。
修改这块后**必须实测**：拖动、双击最大化、边缘缩放、关闭。

---

## 7. 扩展指南

### 7.1 新增厂商（OpenAI 兼容）
1. 在 `resources/providers.json` 加一条：`id / displayName / baseUrl / authStyle / protocol:"openai" / models[]`。
2. 为每个模型声明 `id / displayName / supportsVision / reasoningLevels`。
3. **查官方文档确认端点与鉴权字段**，在 PR 与 DEVLOG 附出处。
4. 加单测到 `tst_providercatalog`。

### 7.2 新增模型
只改 `providers.json`，不要改代码。

### 7.3 新增解析器
实现 `Extractor` → 在 `ExtractorRegistry` 注册 → 补 `tst_extractors`。
元数据必须携带定位信息，否则引用不可用。

### 7.4 新增主题
在 `resources/themes/<name>.json` 按现有令牌**全量**提供，然后在 CMake 资源清单登记；`ThemePresetManager` 会自动列出，无需改代码。

---

## 8. 测试策略

- 所有出网逻辑必须可注入 `FakeNetworkTransport`。
- 优先覆盖：**异常流**（无 Key / 错 Key / 429 / 超时 / 断网 / 损坏文件 / 非法路径 / 空查询）。
- RAG 必须覆盖：阈值、每文件上限、邻居扩展、token 截断、降级、引用定位。
- 安全必须覆盖：密文不含明文、非法 Key 名、平台不支持时拒绝写入。
- 交付前至少：`ctest` 全绿 + `--smoke-test` 退出码 0 + 运行期 QML 警告为 0。

---

## 9. 当前进度快照

| 阶段 | 项目 | 状态 |
|---|---|---|
| 一 | 架构、目录、CI、单测框架、GitHub 私有仓库 | ✅ |
| 一 | 分支保护规则 / 飞书分发结构 | ⬜ |
| 二 | 安全机制、个性化、AI 接入、会话历史、主题、标题栏 | ✅ |
| 二 | AI 接入增强（多厂商/模型、思考档位、Key 校验、多模态、联网） | ✅ |
| 二 | 资料库 / RAG（小库、导入、关联、文本+图片、向量+关键词、引用、成本、同意） | ✅（待正式验收） |
| 二 | 防崩溃自检补齐（随各模块交付逐步覆盖） | 🟡 |
| 三 | Skill 调用、升级（蓝奏云）、稳定性验证 | ⬜ |
| 四 | Android / Web 适配、多端 Worktree、交付 | ⬜ |

> 详细逐条记录见 `DEVLOG.md`（**每完成一个模块必须追加**）。

---

## 10. 已知限制与技术债（按优先级）

1. **解析在 UI 线程**：大 PDF / XLSX 会短暂卡顿。建议后续迁到工作线程。
2. **无 rerank、无多轮检索（B 形态文件级）、无跨文件分层摘要**：跨几十个文件的综合问题命中率有限。
3. **无自动监视文件夹**：当前"快照 + 手动重建"，源文件变化后需手动刷新。
4. **视频未实现**：`VideoExtractor` 仅占位（返回不支持）。
5. **扫描版 PDF 无 OCR**：显示"无文本层"。
6. **`.webp` 依赖 imageformats 插件**：缺失时图片入库失败。
7. **无显式代理设置**：依赖系统代理；对需要代理的厂商可能连接失败。
8. **`.tmpdb/` 未纳入 `.gitignore`**：疑似本地测试/冒烟残留，提交前请确认并清理，避免误入库。
9. **`scripts/fetch-pdfium.ps1` 尚未提交**：需纳入仓库（PDFium 二进制仍忽略）。
10. **分支保护 / PR 流程未强制**：历史上多次"PR 待创建"，DoD 第 3 条未落地，需尽快配置。
11. **文档与实现偶有滞后**：`PLAN.md` 的勾选与待定项需随交付同步更新。
12. **Agent 式直接操作文件**（D11）仍待定；资料库当前**只读**。

---

## 11. 协作与交付流程

1. 动手前按 `docs/README.md` 顺序读规范，并读 `DEVLOG.md` 了解进度。
2. 分支按版本号命名（如 `v0.2.5`）→ 提 PR（**必须可见 diff**）→ Review → 合并。
3. 每完成一个模块：自测 + 异常边界测试 → 追加 `DEVLOG.md` → 更新 `PLAN.md` 勾选。
4. 交付由**总代理**按 DoD 核验；未通过不得进入下一阶段。
5. 删除小库/解除关联**绝不删除用户原文件**——这条必须永远有测试守着。
