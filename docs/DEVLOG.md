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
