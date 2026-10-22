# MeowAi 开发 / 运维 / 销售参考手册

> 版本：v0.2.6
> 对应提交：`9bbe54f`（Windows 交付版；基线 `ce1bdae`，热修复后为 `9bbe54f`）
> 读者：接手开发的人、负责交付和排障的人、对外演示和销售的人
> 口径：本手册以仓库代码、`CMakeLists.txt`、`resources/providers.json`、`.git` 和 `docs/DEVLOG.md` 为准。状态分为已交付、待验收、骨架、未实现。骨架表示有代码或接口但未接入主流程；未实现表示没有可用功能。

---

## 1. 一句话理解产品

MeowAi 是 Windows 桌面 AI 对话客户端。它不内置大模型，不在本机推理，依靠用户填写的厂商 API Key 获取回答。

产品的三个支点是：多厂商对话、资料库检索、本地优先的数据与密钥保护。它不做账号体系，不做云端同步，不做本机模型部署。

对外介绍时，先说“装在电脑上的 AI 客户端，可以接自己的 API Key，可以建资料库让 AI 读自己的文件”。不要把它说成“本地大模型”或“开箱即用的 ChatGPT 替代品”。第一次使用必须先配置厂商和 Key。

---

## 2. 产品由哪几块组成

代码分五层，依赖方向固定为 `ui → core → providers/storage → platform`。QML 只调用 `AppController`、`ChatController`、`LibraryController` 等门面对象，不直接访问网络、数据库或密钥库。`core` 不反向依赖 `ui`。`storage` 不依赖 `core`。`platform` 只提供路径、密钥加密、窗口和平台能力。

| 层 | 主要文件 | 职责 |
| --- | --- | --- |
| 界面层 | `ui/main.qml`、`ui/ConversationPage.qml`、`ui/LibraryPage.qml`、`ui/PersonalizationPage.qml`、`ui/ModelMenu.qml`、`ui/WebSearchPanel.qml`、`ui/LibraryConsent.qml`、`ui/components/*.qml` | 渲染、交互、错误展示；不承载业务规则 |
| 门面层 | `core/AppController.*` | 创建平台、路径、数据库、各控制器；向 QML 暴露上下文属性；处理启动、窗口、冒烟自检 |
| 业务层 | `core/chat/*`、`core/library/*`、`core/retriever/*`、`core/skill/*`、`core/update/*` | 对话编排、资料库索引与检索、参考资料模式、技能扫描、升级检查 |
| 适配层 | `providers/*`、`core/ai/*`、`storage/*` | OpenAI/Anthropic 协议、SSE、厂商目录、SQLite、配置存储、密钥保险库 |
| 平台层 | `platform/impl/*` | Windows/Android/Web 的路径、DPAPI/Keystore、无边框窗口、资源根目录 |

`AppController` 在构造时完成这些动作：选择平台后端；计算数据目录、资源目录、资料库目录、技能目录；打开 `metadata.db` 并迁移 schema；创建 `ChatHistoryModel`、`ChatController`、`LibraryController`、`ThemeManager`；扫描技能；恢复 AI 配置和资料库配置；把对象注册为 QML 上下文属性。

`ChatController` 负责会话切换、消息追加、历史裁剪、厂商请求、流式增量、中断、错误映射、联网工具循环和资料库上下文组装。`LibraryController` 负责小库、文件、文件夹快照、索引队列、embedding、图片描述、检索参数、成本预估、库内搜索和同意状态。

---

## 3. 功能地图与交付状态

| 功能 | 入口 | 状态 | 代码依据 |
| --- | --- | --- | --- |
| 多厂商对话 | 对话页接入卡 | 已交付 | `core/ai/ProviderCatalog.*`、`providers/OpenAiProvider.*`、`providers/AnthropicProvider.*`、`core/chat/ChatController.*` |
| 模型清单与拉取 | 接入卡“模型”“拉取清单” | 已交付 | `ProviderCatalog::models`、`ChatController::fetchModelList` |
| Key 保存与校验 | 接入卡“密钥”“保存”“验证”“清除” | 已交付 | `core/chat/ChatController.*`、`storage/SecureStore.*`、`platform/impl/DesktopPlatformAdapter.*` |
| 思考档位与高级参数 | 底部“当前”胶囊 → 模型菜单 | 已交付 | `ui/ModelMenu.qml`、`ChatController::setThinkingLevel`、`setAdvancedParams` |
| 流式回答与中断 | 输入条“发送”“中断” | 已交付 | `OpenAiProvider` SSE、`ChatController::cancel` |
| 多会话历史 | 左侧会话列表 | 已交付 | `storage/ChatStore.*`、`storage/Database.*`、`core/chat/ChatHistoryModel.*` |
| 附图对话 | 输入条“附图” + 接入卡“安全”开关 | 已交付 | `ChatController::sendWithImage`、`imagesConfirmed`、`OpenAiProvider` 视觉消息 |
| 智能联网 | 输入条“智能联网”开关 | 已交付 | `core/web/WebSearchService.*`、`core/web/WebSearchTool.*`、`ChatController` 内工具循环、`ui/WebSearchPanel.qml` |
| 资料库导入与索引 | 资料库页 | 已交付 | `core/library/LibraryController.*`、`LibraryIndexer.*`、`core/library/extract/*` |
| 向量检索与关键词兜底 | 资料库参数 / 对话挂载 | 已交付 | `core/retriever/RagRetriever.*`、`storage/SqliteVectorIndex.*`、`Database` 的 `chunk_fts` |
| 参考资料模式 | 自动生效 | 已交付 | `core/retriever/FileLevelRetriever.*`、`FullTextAssembler.*`、`SectionPicker.*`、`BudgetCalculator.*`、`InjectionPlanner.*` |
| 成本预估 | 资料库页右栏 | 已交付 | `core/library/CostEstimator.*`、`resources/pricing.json` |
| 主题与个性化 | 个性化页 | 已交付 | `core/theme/*`、`storage/ThemeStore.*`、`storage/PersonalizationStore.*` |
| 图片描述 | 资料库右栏 | 已交付 | `core/library/ImageCaptionService.*`、`core/library/extract/ImageTranscoder.*`、`ImageCaptionExtractor.*` |
| Windows 安装包 | `产品/MeowAi-Setup-0.2.6.exe`（发布记录） | 已交付 | `scripts/MeowAi.iss`、`scripts/*` 打包流程；仓库无产物 |
| 应用内升级 | 无 | 骨架 | `core/update/UpdateManager.*`，`checkForUpdates()` 直接发 `checkFailed` |
| 文件分类 | 无 | 骨架 | `core/library/CategoryManager.*`，`categoriesFor()` 返回空列表，`generateCategories()` 返回 false |
| 修改前备份 | 无 | 骨架 | `core/library/BackupManager.*`，`backupBeforeModify()` 返回空串 |
| Skill 接入对话 | 无 | 未实现 | `core/skill/SkillRegistry.*` 只扫描 `resources/skills/<id>/manifest.json`，没有调用入口 |
| 视频解析 | 资料库 | 未实现 | 视频只登记为 `video`，状态标“不支持” |
| 扫描版 PDF OCR | 资料库 | 未实现 | 无文本层时标 `no_text_layer`，不做识别 |
| Android 客户端 | 无 | 骨架 | `platform/impl/AndroidPlatformAdapter.*`，无构建产物 |
| Web 客户端 | 无 | 骨架 | `platform/impl/WebPlatformAdapter.*`，密钥保存直接拒绝 |
| iOS 客户端 | 无 | 未实现 | 仓库没有 iOS 交付物 |

---

## 4. 核心功能原理

AI 接入。多数厂商走 OpenAI 兼容协议，Anthropic 走独立协议。`OpenAiProvider` 和 `AnthropicProvider` 分别负责拼请求、带鉴权头、解析 SSE、增量回调、取消和错误映射。自定义厂商允许配置 `baseUrl` 和模型名，走 OpenAI 兼容协议。厂商目录在 `resources/providers.json`，运行时从 `:/providers/providers.json` 读取。目录里定义 `id`、`displayName`、`baseUrl`、`models`、`authStyle`、`embedding`、`caption`、`nativeSearch` 等字段。新增 OpenAI 兼容厂商原则上只改这个 JSON，不改请求代码。

模型清单。优先使用内置清单。点“拉取清单”时请求 `/models`，成功则替换当前清单，失败则保留内置清单。上下文窗口优先取模型条目里的 `contextWindow`，缺失时用 `providers.json` 的内置值，再缺失时用资料库配置里的 `fallbackContextWindow`，默认 32768。

密钥。Key 的存储键包括 `provider.<id>.api_key`、`library.embedding.<provider>.api_key`、`library.caption.<provider>.api_key`、`web.search.api_key`。Windows 下 `SecureStore` 通过 `DesktopPlatformAdapter::protectSecret` 调用 DPAPI，密文落盘到数据目录 `secure/`。`config.json` 不保存 Key。Linux/macOS/Web 返回“不支持加密存储”，Web 明确拒绝保存。验证 Key 时用一次最小请求：401/403 判“无效”，429、超时、断网、5xx 判“无法验证”，成功判“有效”。

会话历史。`ChatStore` 使用 `metadata.db` 的 `conversations`、`messages`、`message_attachments` 表。发送时只取最近 20 条消息（`kMaxHistoryMessages`），避免上下文无限增长。消息状态有 `streaming`、`complete`、`partial`、`error`。中断时把当前助手消息标为 `partial`，保留已生成内容。

资料库。`LibraryController` 管理小库、文件、归库关系、文件夹快照和索引队列。导入上传时把文件复制到 `library/`，记录 `origin_path` 但绝不删除原文件。关联文件夹只保存源路径和快照，不复制文件。解析器按扩展名分派：文本走纯文本解析，Office 走 docx/xlsx/pptx 解析，PDF 走 PDFium，图片走转码和视觉描述，视频只登记。文本切分后写入 `chunks`，再按 embedding 配置写入 `chunk_vectors`。关键词检索优先用 `chunk_fts` 的 trigram FTS5；虚拟表建不出来时降级为 LIKE，不影响其他能力。

参考资料模式。v0.2.6 的主链路是文件级召回，不再只按片段注入。`FileLevelRetriever` 先召回相关文件，`BudgetCalculator` 算可用预算，`SectionPicker` 选章节，`FullTextAssembler` 判断能否全文注入，`InjectionPlanner` 决定全文、章节还是片段，并生成截断标记。数据库 v3 新增 `documents` 和 `sections` 两张表。docx 按标题样式或大纲级别分章，Markdown 按标题分章，xlsx 按工作表、pptx 按幻灯片、PDF 按页近似分章。旧文件没有章节记录时自动回退片段模式。

联网搜索。A 形态是“检索后把结果拼进上下文”；B 形态是“把搜索作为工具交给模型，由模型决定是否调用”。v0.2.4 起使用 B 形态，由 `ChatController` 内的工具循环配合 `WebSearchTool` 最多执行 3 轮工具调用。DeepSeek 使用厂商原生搜索，零配置，复用 DeepSeek Key。其他厂商使用自定义搜索端点，端点返回 JSON，程序解析结果并抓取前 2 条正文。搜索失败、超时、401、403、429 都降级为普通对话。

图片。图片进入资料库后先本地转码，统一到最长边 1536 像素，再调用视觉模型生成 500 token 以内的描述，描述与文本走同一套切分和向量链路。对话附图是另一条链路：只有模型声明视觉能力、用户打开“允许把图片上传给厂商”时才发送。开关关闭时请求直接被拒绝，这是有意的隐私保护。

隐私与同意。资料库首次启用或首次挂载时弹出同意说明。未同意时文件可以本地入库，但不建向量、不发图片。撤回同意后立即停止挂载、启用、索引和图片描述。删除小库、解除文件夹关联、移除文件都不删除导入前的原始文件。

---

## 5. 部署与分发

构建环境。Windows 10/11 64 位；Visual Studio 2022（MSVC x64）；CMake 3.21 及以上；Qt 6.5 及以上，交付验证使用 6.8.3 `msvc2022_64`；Inno Setup 6；PowerShell 7 可选，用于下载 PDFium。

配置与构建。在仓库根目录执行：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
cmake --build build --config Release
```

测试构建与执行。测试目标 `meowai_tests` 是 `EXCLUDE_FROM_ALL`，普通构建不会生成测试可执行文件。本地要先单独构建测试目标，再跑 ctest：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64" `
  -DMEOWAI_BUILD_TESTS=ON
cmake --build build --config Debug --target meowai_tests
ctest --test-dir build -C Debug --output-on-failure
```

当前 `tests/CMakeLists.txt` 注册 23 个测试目标，覆盖联调测试、数据库、密钥、厂商解析、工具循环、资料库、检索、参考资料模式和冒烟。测试文件里的注释仍写“21 个”，属于注释过期，以 23 为准。

PDFium。PDF 解析依赖 `third_party/pdfium`。运行 `pwsh scripts/fetch-pdfium.ps1` 下载；未放置 PDFium 时项目仍可构建，但 PDF 会走“无文本层”分支。仓库当前没有 `third_party/pdfium`，因此直接从源码构建得到的程序不能解析 PDF。DEVLOG 记录发布安装包内置 PDFium，但仓库没有保留安装包，产物内容无法核对。

打包。先构建 Release，再准备 `dist/MeowAi/`（`MeowAi.exe` 和 Qt 运行库），然后用 Inno Setup 编译 `scripts/MeowAi.iss`，输出 `dist/MeowAi-Setup-0.2.6.exe`。安装程序按当前用户安装，默认目录 `C:\Users\<用户名>\AppData\Local\Programs\MeowAi`，不需要管理员权限。安装包不包含手册；发布时把安装包和两份手册放进同一个发放目录。DEVLOG 使用的目录名是 `产品/`，但当前仓库既没有 `产品/`，也没有 `dist/`，需要重新构建产物。

冒烟检查。`MeowAi.exe --smoke-test` 在主界面加载后立即退出。退出码 0 只说明 QML 根对象加载成功，不覆盖配置、网络、数据库和资料库功能。程序是 WIN32 GUI 子系统，终端看不到标准输出，但可以读退出码：

```powershell
& "dist\MeowAi\MeowAi.exe" --smoke-test
$LASTEXITCODE
```

发布检查清单。确认版本号在 `CMakeLists.txt` 为 0.2.6；确认安装包文件名正确；确认 `resources/providers.json`、`resources/pricing.json`、`resources/themes/*.json` 已打包；确认 PDFium DLL 已复制；在干净 Windows 虚拟机安装并启动；用真实 Key 发一条消息；导入一个 PDF 和一个图片并索引；挂载资料库提一个问题；卸载后确认数据目录仍在。

升级与回滚。当前没有应用内升级。发布新版本时覆盖安装，数据目录保留。回滚时安装旧版安装包。schema 只做增量迁移，v3 不会删除 v1/v2 表；但旧版程序不识别 v3 表，回滚后新功能数据不可见，会话和资料库旧数据仍可用。

CI 现状。`.github/workflows/ci.yml` 只构建默认目标，然后执行 `ctest`。由于 `meowai_tests` 设置了 `EXCLUDE_FROM_ALL`，测试可执行文件不会被构建，ctest 找不到可执行文件。当前 CI 的测试步骤不可信。修复方式是在 ctest 前增加 `cmake --build build --config Release --target meowai_tests`。本轮只改文档，没有改 CI。

---

## 6. 运维排查手册

| 现象 | 先查什么 | 处理 |
| --- | --- | --- |
| 启动后窗口在屏幕外 | 最近是否换过显示器或改过缩放 | 任务管理器结束进程后重启；程序会按当前屏幕收回窗口 |
| 双击无反应 | 是否已有 MeowAi 进程 | 结束旧进程再启动；检查安全软件拦截 |
| SmartScreen 提示未知发布者 | 安装包是否签名 | 当前未签名，属于已知限制；确认来源后点“更多信息 → 仍要运行” |
| 程序能开，发消息报 401/403 | Key 是否正确、厂商是否匹配 | 重新保存 Key；自定义厂商检查 `baseUrl` 是否到 `/v1` |
| 报 429 | 厂商限流或余额 | 降低频率，检查厂商控制台额度 |
| 报超时/断网 | 代理、防火墙、公司网络 | 关闭或配置代理；换网络重试 |
| Key 状态“无法验证” | 是否限流、超时、5xx | 不代表 Key 错误，稍后重试 |
| Key 保存失败 | 数据目录是否可写、`secure/` 是否被拦截 | 修复权限或信任目录后重试 |
| 模型清单拉取失败 | 网络、Key、`/models` 权限 | 保留内置清单不影响使用；检查 Key 权限 |
| 自定义厂商发不出去 | `baseUrl`、模型名是否为空 | 两项都填；链接以 `/v1` 结尾 |
| 附图被拒绝 | 模型是否支持视觉、上传确认是否打开 | 换视觉模型，打开接入卡“安全”开关 |
| 智能联网不生效 | 当前厂商是否有搜索源 | DeepSeek 零配置；其他厂商配置自定义端点 |
| 联网提示“未就绪” | 端点、密钥 | 端点返回 JSON；DeepSeek 检查 Key |
| 资料库文件“待建向量” | embedding 配置 | 配置厂商/模型/Key 后重建索引 |
| 图片一直“待索引” | 视觉模型、同意状态 | 配置图片描述；确认已同意；必要时转 png/jpg |
| PDF“无文本层” | 是否扫描件、是否有 PDFium | 官方包内置 PDFium；扫描件当前不支持 OCR |
| 导入大文件界面卡住 | 文件大小 | 当前解析在 UI 线程同步执行，等待完成，不要重复点击 |
| 删除小库后找不到原文件 | 是否误解“删除小库” | 删除小库不删原文件；检查原始路径和 `origin_path` |
| 数据迁移后 Key 失效 | 是否换电脑或换 Windows 账户 | DPAPI 密文不可跨账户解密，重新填 Key |
| 开屏视频无声 | 音效开关、系统音量、视频音轨 | 打开“播放音效”；确认视频自带音轨 |
| 主题错色 | 主题令牌是否完整 | 内置两套主题不会出现；自定义主题补齐令牌 |

当前程序没有落盘日志文件，也没有 `qInstallMessageHandler`。排障依赖界面错误横幅、`--smoke-test` 退出码、Windows 事件查看器和手工复现。这是已知短板，后续应增加滚动日志。

---

## 7. 进度与路线

| 阶段 | 内容 | 状态 |
| --- | --- | --- |
| 阶段一 | 框架、构建、平台适配、存储、骨架模块、首批测试、CI | 已交付（v0.1.0） |
| 阶段二 | 真实 AI 接入、密钥加密、个性化、UI 重做、会话历史、主题、无边框标题栏 | 已交付（v0.2.0 / v0.2.1） |
| 阶段三 | 多厂商目录、模型菜单、档位、密钥校验、联网 A/B 形态 | 已交付（v0.2.3 / v0.2.4，初步验收） |
| 阶段四 | 资料库、RAG、成本预估、同意机制、参考资料模式 | 已交付（v0.2.5 / v0.2.6，初步验收） |
| 阶段五 | 文件分类、AI 修改前备份、Skill 接入、应用内升级、Android/Web | 骨架或未实现 |
| 未排期 | OCR、视频解析、云端同步、代码签名、自动更新源 | 未实现 |

`docs/PLAN.md` 里仍有“图标与 UI 定稿 [ ]”“防崩溃自检 [ ]”等未勾选项，但 v0.2.6 已经交付图标、安装包和 `--smoke-test`。PLAN 的勾选状态落后于代码，以本手册和 DEVLOG 为准。

---

## 8. 已知限制

资料库解析和索引在 UI 线程同步执行。大 PDF、大 Excel 会造成数秒卡顿。后续应把解析和索引移到工作线程，并用信号回传进度。

文件夹关联是快照模式，不是实时监视。源目录变化后必须点“重建快照”。没有文件系统监听器。

资料库是只读参考，不提供 AI 回写文件。`CategoryManager` 和 `BackupManager` 是骨架：前者不生成分类，后者不复制备份。任何“AI 修改文件前会自动备份”的说法都不成立。

`UpdateManager` 是骨架。`checkForUpdates()` 直接发出“更新检查尚未实现（阶段三接入）”，没有更新源、下载、校验和安装入口。

Skill 只扫描不调用。`SkillRegistry` 会读取 `resources/skills/<id>/manifest.json` 和技能文档，但 `ChatController` 没有注入或调用技能的路径。界面没有技能入口。

Web 端拒绝保存密钥。`WebPlatformAdapter::supportsSecretEncryption()` 返回 false，保存 Key 时返回“网页端不保存密钥，请每次会话重新填写”。这是设计选择，不是缺陷。

Android 与 Web 只有平台适配代码，没有构建产物、没有安装包、没有经过真机验收。Android Keystore 代码存在但未验证。

没有代码签名。Windows SmartScreen 会提示未知发布者。安装向导是英文界面，中文用户需要按 Next/Install 操作。

没有应用内升级，也没有自动更新源。升级方式是覆盖安装新版本安装包。

没有日志文件。没有 OCR、视频解析、重排序模型、云端同步、多用户、代理设置界面。

上下文窗口依赖厂商返回值。厂商不返回窗口时使用内置值或 `fallbackContextWindow`，估算偏小会截断资料，偏大会被厂商拒绝。

图片描述依赖视觉模型和 Qt 图片插件。`webp` 在部分 Qt 构建里缺少插件，会转码失败；资料库会保留文件并标记失败，不会崩溃。

CI 的 ctest 步骤当前不会构建测试目标，测试结果不可信。测试注释写 21 个，实际注册 23 个。

`docs/PLAN.md`、部分 DEVLOG 细节和界面帮助文案存在落后或冲突，见第 14 节与交付说明。文档不改历史记录。

---

## 9. 演示脚本

15 分钟演示按这个顺序走。每一步都先给入口，再给结果，不跳步。

第 1 分钟，打开程序。指出左侧三个导航：对话、个性化、资料库。指出顶栏右上角头像和底部状态栏。说明数据在本机，不需要注册账号。

第 2 到 4 分钟，配置 Key。点接入卡“配置”，选 DeepSeek 或 OpenAI，选模型，粘贴 Key，点“保存”，点“验证”。指出状态从“未配置”变成“已就绪”或“有效”。强调 Key 加密保存在本机，不写进配置文件。

第 4 到 6 分钟，发一条普通消息。展示流式输出。生成中点“中断”，说明已生成内容保留。新建一个会话，再切回旧会话，展示历史持久化。

第 6 到 8 分钟，展示模型菜单。鼠标停留在底部“当前”胶囊约 1 秒，打开菜单。切换模型和档位。展开“高级原生参数”，说明不熟悉不要填。打开“联网搜索”配置，指出 DeepSeek 是“内置源 · 零配置”。

第 8 到 11 分钟，建资料库。切到资料库页，点“同意并启用”，读一遍同意说明。新建小库，导入一个 PDF 和一个文本文件，展示状态从“待索引”到“已索引”或“待建向量”。指出删除小库不删原文件。

第 11 到 13 分钟，挂载资料库。回到对话页，打开“资料库”开关，挂载刚建的小库。提一个只有文件里才有答案的问题。展示回答上方的“资料库 · 1 个小库”“来源”“注入”标签。说明没有命中时不会硬塞资料。

第 13 到 14 分钟，展示联网。打开“智能联网”，提一个需要最新信息的问题。展示“已联网 · N 条来源”。说明搜索是工具，模型可以决定不搜。

第 14 到 15 分钟，收尾。切到个性化页，换主题，换背景或头像。指出“帮助”里的开发者和联系方式。说明当前未实现应用内升级、OCR、视频解析、Skill 调用，不要现场承诺。

演示禁忌。不要展示没有配置 Key 的“发送”；不要把骨架功能说成已完成；不要说“文件会被 AI 自动修改”；不要说“资料库内容绝对不会离开本机”，正确说法是“未同意不会离开，同意后只发给选定的厂商”。

---

## 10. 开发协作速查

分支。功能分支从当前交付基线拉出。版本分支用 `v0.2.x` 命名。仓库当前的版本标识是分支引用，不是 annotated tag；`git tag -l` 为空。发布时如果要 tag，需要单独创建并推送。

提交。提交信息用中文，格式建议“类型: 内容”，例如“feat: 资料库支持文件夹快照”“fix: 修正嵌入批次上限”。一次提交只做一件事。不要在提交里混入文档以外的生成物。

DEVLOG。新记录追加到 `docs/DEVLOG.md` 顶部，日期时间用本地时间。记录必须写清：变更范围、涉及文件、验证命令与结果、未完成项、已知风险。历史条目只读，不改写。发现历史记录错误，在 PR 或交付说明里报告。

PLAN。阶段任务完成后更新 `docs/PLAN.md` 的勾选和状态。PLAN 是路线视图，不是验收证据；验收以代码、测试和 DEVLOG 为准。

文档。用户手册和开发手册的版本号、路径、命令、功能名称必须一致。改功能时同步改文档。文档不承诺未实现功能。状态用“已交付 / 待验收 / 骨架 / 未实现”，不用模糊的“基本完成”。

测试。新增功能至少补一个测试目标，并在 `tests/CMakeLists.txt` 注册。提交前本地执行测试目标的构建和 ctest。CI 目前不会自动构建测试目标，不能只看 CI 绿灯。

密钥红线。任何代码、日志、错误信息、URL、配置样例都不得包含真实 Key。调试输出要脱敏。新增平台时必须实现 `supportsSecretEncryption`，不能退化成明文保存。

资料库红线。不得删除用户原始文件。不得在未同意时外发文件内容或图片。不得把资料库内容写进日志。修改文件的能力当前不存在；将来接入时必须先经过 `BackupManager`。

跨层约束。QML 不直接发 HTTP、不直接开数据库、不直接读写密钥文件。业务逻辑放 `core`。平台差异放 `platform`。厂商差异优先放 `resources/providers.json`。

---

## 11. 术语表

| 术语 | 含义 |
| --- | --- |
| 小库 / collection | 资料库里的一个分组，可包含导入文件和关联文件夹 |
| 挂载 / mount | 让某个小库参与当前对话的检索 |
| 片段 / chunk | 文本切分后的最小检索单元，默认 700 字符，重叠 120 |
| 向量 / embedding | 文本片段的数值表示，用于相似度检索 |
| 参考资料模式 | 文件级召回，按全文、章节、片段三级注入 |
| 注入预算 | 允许放进提示词的 token 上限，超出就截断 |
| 回答预留 | 给模型生成回答保留的 token，默认 2000 |
| 未知窗口估算值 | 厂商没有返回上下文窗口时使用的兜底值，默认 32768 |
| 同意 / consent | 用户允许资料库内容发给所选厂商的状态 |
| 骨架 | 有类、有接口，但未接入主流程，不能对外承诺 |
| 待验收 | 代码完成，尚未完成真机或真实 Key 验收 |
| 已交付 | 有可用入口，经过至少一轮验收 |
| A 形态联网 | 程序先检索，再把结果拼进上下文 |
| B 形态联网 | 搜索作为工具交给模型，由模型决定是否调用 |
| DPAPI | Windows 数据保护接口，按用户加密本机数据 |
| Keystore | Android 密钥库，代码存在但未验收 |
| PDFium | PDF 解析库，可选依赖 |

---

## 12. 对外沟通统一口径

可以说：Windows 桌面 AI 客户端；支持多家厂商和自定义 OpenAI 兼容接口；Key 加密保存在本机；会话和资料库保存在本机；可以导入 PDF、Word、Excel、PPT、图片做检索；资料外发需要用户同意；可以中断生成；可以切换模型和思考档位；DeepSeek 联网零配置；有成本预估。

不要说：本地大模型；离线可用；绝对不联网；AI 会自动整理和修改文件；自动备份；自动升级；支持 OCR；支持视频理解；支持 Skill 调用；Android/Web 已可用；安装包已签名。

价格与额度。程序不代收费用，不提供额度。用户直接向厂商付费。成本预估只是参考，不构成账单承诺。

隐私表述。准确说法是“未同意时资料库内容不会离开本机；同意后只会发给你选择的对话厂商、embedding 厂商、视觉模型厂商或搜索服务”。不要说“任何数据都不会离开本机”，因为对话本身必须发给厂商。

未实现功能的标准回答。“这个功能当前是骨架/未实现，计划在后续阶段接入，现在没有可用入口。”不要给日期承诺。

---

## 13. 工程结构与构建参考

目录。`core/` 放业务；`providers/` 放厂商协议；`storage/` 放 SQLite、配置、密钥；`platform/` 放平台适配；`ui/` 放 QML；`resources/` 放 `providers.json`、`pricing.json`、主题、技能示例；`tests/` 放测试；`scripts/` 放 PDFium 下载和 Inno Setup 脚本；`docs/` 放项目文档。

核心类。`AppController` 是启动与门面；`ChatController` 是对话编排；`ChatHistoryModel` 是会话列表模型；`ChatStore` 管会话持久化；`ProviderCatalog` 是厂商目录；`OpenAiProvider` 是 OpenAI 兼容 HTTP/SSE 实现；`AnthropicProvider` 是 Anthropic 协议实现；`LibraryController` 是资料库门面；`LibraryManager` 管小库与文件；`CollectionManager` 管小库；`LibraryStore` 管资料库元数据；`LibraryIndexer` 管索引队列；`ExtractorRegistry` 按扩展名分派解析器；`ChunkSplitter` 管切分；`EmbeddingService` 管向量；`ImageTranscoder` 管图片转码；`ImageCaptionService` 管图片描述；`CostEstimator` 管成本；`SqliteVectorIndex` 管向量存取；`RagRetriever` 管向量召回；`FileLevelRetriever` 管文件级召回；`BudgetCalculator` 管预算；`InjectionPlanner` 管注入；`SecureStore` 管密钥；`Database` 管 schema；`ThemeManager` 管主题与个性化资源；`SkillRegistry` 管技能扫描；`UpdateManager` 是升级骨架。

数据库。文件是数据目录下的 `metadata.db`，当前 `schema_version = 3`。v1 表：`meta`、`conversations`、`messages`、`message_attachments`。v2 表：`collections`、`library_files`、`file_collections`、`folder_links`、`chunks`、`chunk_vectors`、`citations`、`chunk_fts`。v3 表：`documents`、`sections`。`chunk_fts` 使用 FTS5 trigram；不可用时降级默认分词器；再不可用时关闭 FTS，关键词检索走 LIKE。

配置。`config.json` 在数据目录。主要段：`ai`（`providerId`、`modelId`、`customBaseUrl`、`thinkingLevels`、`nativeOverrides`）、`library`（`enabled`、`consentGiven`、`consentAt`、检索参数、`mountedCollections`、embedding/caption 配置）、`web`（`enabled`、`endpoint`）、`theme`（`preset`）、`personalization`（字体、背景、头像、开屏）。Key 不在其中，统一在 `secure/`。

资源。`providers.json` 当前 7 个厂商：OpenAI、DeepSeek、通义千问、月之暗面 Kimi、智谱 GLM、Anthropic、自定义。带 embedding 的是 OpenAI、通义千问、智谱 GLM；带原生搜索的是 DeepSeek；其余厂商的 embedding/caption 需要显式配置或走自定义。`pricing.json` 是本地参考价，按厂商+模型精确匹配，未收录显示“—”。

平台后端。`DesktopPlatformAdapter`：Windows 用 DPAPI，支持自绘标题栏；Linux/macOS 使用同一后端但不支持密钥加密，自绘标题栏可用。`AndroidPlatformAdapter`：支持自绘标题栏，密钥走 Keystore，未验收。`WebPlatformAdapter`：不支持自绘标题栏，明确拒绝保存密钥。平台选择在编译期通过 CMake 定义决定。

构建变量。`MEOWAI_BUILD_TESTS` 控制测试子目录，默认 ON，但测试目标仍是 `EXCLUDE_FROM_ALL`；`CMAKE_PREFIX_PATH` 指向 Qt；`WIN32` 让主程序不弹控制台；PDFium 存在时自动加入链接和复制步骤。CMake 显式查找的 Qt 模块是 Core、Gui、Qml、Quick、Network、Sql、Test、Multimedia、QuickControls2。Widgets 和 Svg 没有显式声明；SVG 背景/头像的渲染依赖发布时部署 Qt 的 SVG 图片插件。

常用命令。

```powershell
# 配置
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64" `
  -DMEOWAI_BUILD_TESTS=ON

# 主程序
cmake --build build --config Release

# 测试目标（必须单独构建）
cmake --build build --config Debug --target meowai_tests
ctest --test-dir build -C Debug --output-on-failure

# 冒烟
& "build\Release\MeowAi.exe" --smoke-test
$LASTEXITCODE

# PDFium（可选）
pwsh scripts/fetch-pdfium.ps1

# 安装包
# 用 Inno Setup Compiler 打开 scripts/MeowAi.iss 并编译
```

验收证据。v0.2.6 的 DEVLOG 记录本机 `23/23` 测试通过；当前 `tests/CMakeLists.txt` 注册 23 个目标。CI 仍未构建测试目标，不能把 CI 结果当作测试证据。

---

## 14. 版本变更记录（v0.1.0 → v0.2.6）

版本映射表。仓库没有 annotated tag，`git tag -l` 为空；下表的 ref 是本地或远端分支引用。v0.2.3 与 v0.2.4 共用同一个提交，没有独立 ref。

| 版本 | 日期 | commit | 证据来源 |
| --- | --- | --- | --- |
| v0.1.0 | 2026-10-19 | `d23d1b0` | 远端分支 `origin/v0.1.0`；`git show d23d1b0`；DEVLOG L763–775 |
| v0.2.0 | 2026-10-19 | `0145c43` | 本地/远端分支 `v0.2.0`；`git show 0145c43`；DEVLOG L687–761 |
| v0.2.1 | 2026-10-19 | `4ead947` | 远端分支 `origin/v0.2.1`；`git show 4ead947`；DEVLOG L623–685 |
| v0.2.3 | 2026-10-20 | `9661eff` | 与 v0.2.4 合并提交；无独立 ref；DEVLOG L539–621 |
| v0.2.4 | 2026-10-20 | `9661eff` | 与 v0.2.3 合并提交；无独立 ref；DEVLOG L518–537 |
| v0.2.5 | 2026-10-21（提交） | `aee4aa5` | 远端分支 `origin/v0.2.5`；DEVLOG 记录日期 2026-10-20，L139–518 |
| v0.2.6 | 2026-10-21 | `9bbe54f` | HEAD、本地/远端 `v0.2.6`；基线 `ce1bdae`（无独立 ref）+ 热修复 `9bbe54f`；DEVLOG L42–121 |

**v0.1.0**

| 字段 | 内容 |
| --- | --- |
| 版本号 | v0.1.0 |
| 日期 | 2026-10-19 |
| commit | `d23d1b0` |
| 新增 | 可编译的 Qt/CMake 框架；`AppController`；`Main.qml` 三页骨架；`Desktop/Android/Web` 平台适配器；`AppPaths`；`Database` v1 会话表；`SecureStore` 接口；`ChatController`、`Provider` 接口；`CategoryManager`、`BackupManager`、`UpdateManager`、`SkillRegistry` 骨架；首批 4 个测试；GitHub Actions 工作流；文档体系 |
| 修改 | 无，首个版本 |
| 修复 | 无 |
| 未完成或已知限制 | 没有真实 AI 请求；没有会话历史 UI；没有资料库 RAG；没有联网；没有 Key 校验；骨架模块均未接入；CI 未构建测试目标；没有 PR |
| 证据来源 | `git show d23d1b0`；DEVLOG L763–775 |
| 状态 | 骨架已交付，整体待验收 |

**v0.2.0**

| 字段 | 内容 |
| --- | --- |
| 版本号 | v0.2.0 |
| 日期 | 2026-10-19 |
| commit | `0145c43` |
| 新增 | 真实厂商接入链路；OpenAI 兼容 Provider；流式增量与取消；接入卡；`SecureStore` + Windows DPAPI；个性化（字体、背景、头像、开屏视频）；主题令牌体系；UI 全面重做；错误横幅；AI 配置持久化 |
| 修改 | CMake 增加 Network、Multimedia、QuickControls2（`git show 0145c43:CMakeLists.txt` 未声明 Svg）；`Main.qml` 重构；`ChatController` 从占位改为真实编排 |
| 修复 | 修复 QML 单例注册、主题令牌缺失、`onAccent` 缺失等首次接入问题 |
| 未完成或已知限制 | 厂商目录只有单厂商链路；会话历史只存在 QML 内存；标题栏仍是系统原生；没有 Key 三态校验；Android Keystore 未验证；没有 PR；真实 Key 手工验收不完整 |
| 证据来源 | `git show 0145c43`；DEVLOG L687–761 |
| 状态 | 待验收 |

**v0.2.1**

| 字段 | 内容 |
| --- | --- |
| 版本号 | v0.2.1 |
| 日期 | 2026-10-19 |
| commit | `4ead947` |
| 新增 | `ChatHistoryModel` + SQLite 会话历史；`ThemePresetManager` + `theme1/theme2`；自绘无边框标题栏；窗口拖拽、双击最大化、边缘缩放、屏幕收回 |
| 修改 | 主程序改为 `WIN32` 目标，不再弹控制台；`Main.qml` 接入历史列表和标题栏；主题切换持久化 |
| 修复 | 修复控制台窗口弹出；修复主题切换不刷新；修复历史切换丢失当前会话 |
| 未完成或已知限制 | 多厂商目录和模型菜单未做；Key 校验未做；联网未做；手工 GUI 验收未完成；Android/Web 未验收；没有 PR |
| 证据来源 | `git show 4ead947`；DEVLOG L623–685 |
| 状态 | 待验收 |

**v0.2.3**

| 字段 | 内容 |
| --- | --- |
| 版本号 | v0.2.3 |
| 日期 | 2026-10-20 |
| commit | `9661eff`（与 v0.2.4 合并提交；无独立 ref） |
| 新增 | `resources/providers.json` 7 家厂商目录；`ProviderCatalog`；`AiConfigStore`；模型下拉与“拉取清单”；模型与档位胶囊；思考档位“关/低/中/高”；高级原生参数 JSON；Key 三态校验；时间上下文注入；DeepSeek 视觉能力标记；联网 A 形态（检索后拼上下文） |
| 修改 | `ChatController` 改为按厂商构建请求；接入卡改为厂商/模型/档位/密钥/安全分区；错误映射细分 401/403/429/超时 |
| 修复 | 修复 DeepSeek 视觉模型未标记；修复鉴权失败被笼统归为网络错误；修复模型切换后档位不重置 |
| 未完成或已知限制 | 真实 Key 手工测试不完整；联网 A 形态没有工具循环；没有引用展示；没有 PR；Android/Web 未验收 |
| 证据来源 | `git show 9661eff`；DEVLOG L539–621。DEVLOG 有记录，仓库无独立 ref |
| 状态 | 待验收 |

**v0.2.4**

| 字段 | 内容 |
| --- | --- |
| 版本号 | v0.2.4 |
| 日期 | 2026-10-20 |
| commit | `9661eff`（与 v0.2.3 合并提交；无独立 ref） |
| 新增 | `ChatController` 内工具循环 + `WebSearchTool`；`WebSearchService`；`WebSearchPanel`；DeepSeek 原生搜索零配置；搜索作为工具交给模型，最多 3 轮；来源条展示前 3 条；自定义搜索端点与密钥；抓取前 2 条网页正文 |
| 修改 | 联网从 A 形态切换为 B 形态；输入条开关改为“智能联网”；非 DeepSeek 厂商改为自定义源；配置不可用时自动降级 |
| 修复 | 修复联网配置面板未按厂商禁用；修复搜索失败中断整轮；修复来源展示缺失 |
| 未完成或已知限制 | 只有 DeepSeek 内置源；Anthropic 等没有内置搜索；网页抓取受目标站点限制；真实 Key 手工测试不完整；没有 PR；Android/Web 未验收 |
| 证据来源 | `git show 9661eff`；DEVLOG L518–537。DEVLOG 有记录，仓库无独立 ref |
| 状态 | 待验收 |

**v0.2.5**

| 字段 | 内容 |
| --- | --- |
| 版本号 | v0.2.5 |
| 日期 | 2026-10-21（提交日期）；DEVLOG 记录日期为 2026-10-20 |
| commit | `aee4aa5` |
| 新增 | 资料库 schema v2；`collections`、`library_files`、`file_collections`、`folder_links`、`chunks`、`chunk_vectors`、`citations`、`chunk_fts`；`LibraryController`、`LibraryManager`、`LibraryIndexer`；文本/Office/PDF/图片解析；`ChunkSplitter`；`EmbeddingService`；`ImageCaptionService`；`CostEstimator`；`RagRetriever`；关键词兜底；资料库页；同意弹层；导入/拖拽/关联文件夹/重建快照/库内搜索；成本预估；21 个测试目标（DEVLOG 记 22/22 通过，计数口径不同） |
| 修改 | CMake 加入资料库模块和 PDFium 可选链接；`ChatController` 接入检索上下文；文档补充资料库章节 |
| 修复 | 修复图片导入无反馈；修复文件移除后的悬空引用；修复 FTS5 不可用时的崩溃风险；修复成本表模糊匹配；修复预算超限未钳制 |
| 未完成或已知限制 | 解析和索引在 UI 线程同步执行；`webp` 可能缺插件；视频只登记；没有 OCR；没有重排序；没有文件夹实时监听；PDFium 需要手工放置；没有 PR |
| 证据来源 | `git show aee4aa5`；DEVLOG L139–518 |
| 状态 | 已交付（初步验收；DEVLOG 记 22/22 测试通过，仓库当时注册 21 个测试目标） |

**v0.2.6**

| 字段 | 内容 |
| --- | --- |
| 版本号 | v0.2.6 |
| 日期 | 2026-10-21 |
| commit | `9bbe54f`（基线 `ce1bdae` 无独立 ref，热修复后 `9bbe54f` 为 HEAD/`v0.2.6`） |
| 新增 | 参考资料模式：`FileLevelRetriever`、`FullTextAssembler`、`SectionPicker`、`BudgetCalculator`、`InjectionPlanner`；数据库 schema v3：`documents`、`sections`；章节识别（docx 标题样式、Markdown 标题、xlsx 工作表、pptx 幻灯片、PDF 页）；全文/章节/片段三级注入；来源与注入标签；截断标记；应用图标；应用内“帮助”；Windows Inno Setup 安装包；`--smoke-test`；用户手册与开发手册；`providers.json` 增加 `maxBatchSize`；新增 2 个测试，共 23 个 |
| 修改 | 检索从片段级改为文件级召回 + 三级注入；UI 收紧间距、修复溢出和文本截断；标题栏改单行；窗口启动位置收回屏幕；安装包脚本；文档按 v0.2.6 重写 |
| 修复 | 修复通义千问 embedding 批次上限导致的 400；修复标题栏双行；修复长文本/胶囊截断与重叠；修复窗口在屏幕外；修复 QML 警告；修复资料库注入超预算 |
| 未完成或已知限制 | docx 自定义样式且无大纲级别时识别不到章节；厂商不返回窗口时依赖估算值；安装包未签名且向导为英文；未在干净虚拟机完成全流程验收；没有 PR；没有应用内升级、OCR、视频解析、Skill 调用 |
| 证据来源 | `git show ce1bdae`、`git show 9bbe54f`；DEVLOG L42–121 |
| 状态 | 已交付（Windows 安装包，初步验收，23/23 测试） |
