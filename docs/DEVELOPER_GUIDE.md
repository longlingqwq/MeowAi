# MeowAi 开发 / 运维 / 销售参考手册

> 版本：v0.2.7.1
> 读者：接手开发的人、负责交付和排障的人、对外演示和销售的人
> 口径：本手册以 v0.2.7.1 分支 HEAD 的代码、`CMakeLists.txt`、`resources/providers.json`、`.git` 和 `docs/DEVLOG.md` 为准。v0.2.7.1 无 DEVLOG 条目，变更证据来自该分支提交。状态分为已交付、待验收、骨架、未实现。骨架表示有代码或接口但未接入主流程；未实现表示没有可用功能。

---

## 1. 一句话理解产品

MeowAi 是 Windows 桌面 AI 对话客户端。它不内置大模型，不在本机推理，依靠用户填写的厂商 API Key 获取回答。程序自己负责界面、会话、资料库检索和隐私控制。

产品边界：只交付 Windows 桌面版。Android 与 Web 只有平台适配骨架，没有可运行安装包。iOS 未开始。

---

## 2. 产品由哪几块组成

界面层在 `ui/`，全部是 QML。界面不直连数据库、不发网络请求、不读密钥，只调用注入的控制器对象。

应用层在 `core/`。`AppController` 负责装配和生命周期；`ChatController` 负责一轮对话；`LibraryController` 负责资料库；`ThemeManager` 负责主题与个性化资源。

厂商层在 `providers/`。`ProviderCatalog` 读取 `resources/providers.json`，`OpenAiProvider` 处理 OpenAI 兼容协议，`AnthropicProvider` 处理 Anthropic 协议，`ChatStreamParser` 解析 SSE。

存储层在 `storage/`。`Database` 管理 SQLite，`ChatStore` 管会话与消息，`LibraryStore` 管文件与集合，`DocumentStore` 管文档与章节，`SqliteVectorIndex` 管向量，`SecureStore` 管密钥，各 `*ConfigStore` 管配置。

平台层在 `platform/`。`PlatformAdapter` 定义应用数据目录、密钥加密、标题栏、自更新等能力。桌面实现用 DPAPI；Web 实现拒绝保存密钥。

构建与交付在根 `CMakeLists.txt`、`scripts/`、`.github/` 和 `产品/`。

---

## 3. 功能地图与交付状态

| 功能 | 入口 | 状态 | 关键实现 |
| --- | --- | --- | --- |
| 多厂商对话 | 对话页顶栏模型胶囊 | 已交付 | `ChatController`、`core/ai/ProviderCatalog.*`、`providers/*` |
| 流式与中断 | 输入条发送/中断 | 已交付 | `ChatController`、`ChatStreamParser` |
| 会话历史 | 侧栏会话列表 | 已交付 | `ChatHistoryModel`、`storage/ChatStore.*` |
| 会话删除 | 会话条右侧 `×` | 已交付 | `History.removeConversation`，点击即删 |
| 回答复制与重新生成 | 助手消息下方按钮 | 已交付 | `ChatController::regenerateLast`、QML 剪贴板 |
| 文字与图片分享 | 助手消息下方按钮 | 已交付 | `ConversationPage.shareImage`，`grabToImage` 保存 PNG |
| Markdown 渲染 | 助手消息 | 已交付 | `ChatController::renderMarkdown` |
| Token 与耗时 | 助手消息底部 | 已交付 | `ChatUsage`、数据库 schema v4 |
| 附图对话 | 输入条“附图” | 已交付 | `ChatController::sendWithImage`、`imagesConfirmed` |
| 智能联网 | 输入条“联网搜索”开关 | 已交付 | `core/web/WebSearchService.*`、`WebSearchTool.*`、工具循环 |
| 资料库导入与索引 | 资料库页 | 已交付 | `LibraryController`、`LibraryIndexer`、`core/library/extract/*` |
| 向量检索与关键词兜底 | 资料库配置抽屉 / 对话挂载 | 已交付 | `RagRetriever`、`SqliteVectorIndex`、`chunk_fts` |
| 参考资料模式 | 自动生效 | 已交付 | `FileLevelRetriever`、`FullTextAssembler`、`SectionPicker`、`BudgetCalculator`、`InjectionPlanner` |
| 资料库外发同意 | 首次启用/挂载 | 已交付 | `LibraryConsent.qml`、`LibraryController::grantConsent` |
| 资料库服务配置 | 资料库右侧抽屉 | 已交付 | `LibraryConfigPanel.qml`、`fetchEmbeddingModels`、`fetchCaptionModels`、`validate*` |
| 成本预估 | 资料库配置抽屉 | 已交付 | `CostEstimator`、`resources/pricing.json` |
| 主题 | 个性化页 | 已交付 | `ThemeManager`、`resources/themes/theme1.json`、`theme2.json` |
| 字体/背景/头像 | 个性化页 | 已交付 | `ThemeManager`、`AvatarCrop.qml`（512×512） |
| 内置开屏 | 启动时 | 已交付 | `SplashIntro.qml`（约 3.5 秒，按主题，可跳过） |
| 自定义开屏视频 | 个性化页 | 已交付 | `SplashVideo.qml`，上限 200 MB |
| 应用图标与品牌 | 安装包/标题栏 | 已交付 | `resources/branding/*`、`MeowAi.rc` |
| 文件分类 | 无界面入口 | 骨架 | `CategoryManager` 返回空结果 |
| 修改前备份 | 无界面入口 | 骨架 | `BackupManager` 返回空 |
| 应用内升级 | 无界面入口 | 骨架 | `UpdateManager::checkForUpdates` 发 `checkFailed` |
| Skill 调用 | 无入口 | 未实现 | `SkillRegistry` 只扫描清单，不接入对话 |
| 视频解析 | 无 | 未实现 | `VideoExtractor` 只登记，不产出文本 |
| OCR | 无 | 未实现 | 扫描版 PDF 无文本层 |
| Android / Web 客户端 | 无 | 骨架 | `WebPlatformAdapter` 等，无安装包 |
| iOS 客户端 | 无 | 未实现 | 规划走 Web |

---

## 4. 核心功能原理

启动链路。`main.cpp` 创建 `QGuiApplication`，设置组织名和应用名，选择 `PlatformAdapter`。`AppPaths` 按平台数据目录生成布局，`Database` 打开 `metadata.db` 并迁移 schema。随后装配 `SecureStore`、`AiConfigStore`、`ChatStore`、`LibraryConfigStore`、`PersonalizationStore` 等，创建 `AppController`，把控制器注入 QML 上下文，加载 `qrc:/ui/main.qml`。

一轮对话。用户发送后，`ChatController` 先写入用户消息，再组装上下文：最近 20 条历史消息、挂载小库的检索结果、联网工具结果。然后按厂商协议发流式请求。`ChatStreamParser` 解析增量文本，界面实时追加。收到 `[DONE]` 或结束标记后写入完整助手消息。失败时保留已生成内容并标记 `partial` 或 `error`。

工具循环。开启联网后，模型可以返回工具调用。`ChatController` 执行 `WebSearchTool`，把搜索结果作为工具消息回填，最多 3 轮。超过轮次上限后停止工具调用，要求模型直接回答。工具调用过程显示“联网中…”。

图片对话。`ChatController::sendWithImage` 先检查 `imagesConfirmed`。关闭时直接拒绝并返回错误。开启后读取图片路径，交给 `ImageTranscoder` 转码，再以视觉消息格式发给厂商。图片只在这一轮使用，不写入资料库。

资料库入库。`LibraryIndexer` 按扩展名选择 `Extractor`。文本类解析出纯文本，`ChunkSplitter` 按 700 字符、120 重叠切分。图片走 `ImageTranscoder` 和 `ImageCaptionExtractor`，需要视觉模型生成描述。片段写入 `chunks`，向量写入 `chunk_vectors`，关键词写入 `chunk_fts`。PDF 需要 PDFium；缺失时 `PdfTextExtractor` 降级，文件标为“无文本层”。

检索。问题先做 embedding，再在 `SqliteVectorIndex` 里算相似度，取 Top-K。向量结果不足时用 FTS5 trigram 兜底。结果按相似度和文件聚合权重排序，做邻居扩展，再按 token 预算裁剪。命中片段带文件 ID、偏移和引用信息。

参考资料模式。先由 `FileLevelRetriever` 按文件聚合。`BudgetCalculator` 计算单文件预算、全局预算和回答预留。`FullTextAssembler` 尝试注入全文；放不下时交给 `SectionPicker` 选章节；再放不下时回到片段。`InjectionPlanner` 生成最终注入计划和引用。未知上下文窗口回退 32768 token。

用量。厂商返回 `usage` 时，`ChatUsage` 记录 prompt、completion、total token。`ChatController` 同时记录本轮耗时。两者写入 `messages` 的 `prompt_tokens`、`completion_tokens`、`total_tokens`、`elapsed_ms` 列，界面在助手消息底部展示。厂商不返回用量时显示 `—`。

密钥。`SecureStore` 把明文交给 `PlatformAdapter::protectSecret`。Windows 使用 DPAPI，附加熵 `MeowAi.Security.v1`，密文写入 `secure/<provider>.vault`。`config.json` 只存“是否已配置”等非敏感状态。Linux 和 macOS 的桌面实现返回未实现；Web 实现 `supportsSecretEncryption()` 为 false，保存直接拒绝。

主题与个性化。`ThemeManager` 读取 `resources/themes/theme1.json` 与 `theme2.json`，把颜色、圆角、字号映射成 QML 可读属性。字体、背景、头像、开屏视频复制到 `assets/`。头像经 `AvatarCrop.qml` 裁剪成 512×512。开屏优先自定义视频；没有视频时用 `SplashIntro.qml` 按当前主题播放内置动画。`PersonalizationStore` 持久化开关和文件路径。

---

## 5. 部署与分发

构建环境：Windows 10/11 x64，Visual Studio 2022，Qt 6.8.3 `msvc2022_64`，CMake 3.21 以上。最低 Qt 版本 6.5，C++17。

配置与构建：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64" `
  -DMEOWAI_BUILD_TESTS=ON
cmake --build build --config Release
```

构建测试。测试目标默认 `EXCLUDE_FROM_ALL`，日常构建不会编译。需要显式构建聚合目标：

```powershell
cmake --build build --config Debug --target meowai_tests
ctest --test-dir build -C Debug --output-on-failure
```

当前 `tests/CMakeLists.txt` 注册 26 个测试目标，覆盖存储、密钥、主题、厂商解析、SSE、工具循环、资料库、检索、参考资料模式、会话用量、资料库服务与冒烟。测试文件顶部注释仍写“21 个”，属于注释过期，以 26 为准。

PDFium。PDF 解析依赖 `third_party/pdfium`。运行 `pwsh scripts/fetch-pdfium.ps1` 下载；未放置 PDFium 时项目仍可构建，但 PDF 会走“无文本层”分支。仓库当前没有 `third_party/pdfium`，因此直接从源码构建得到的程序不能解析 PDF。DEVLOG 记录发布安装包内置 PDFium，但仓库没有保留安装包解包内容，产物内部是否包含无法从源码核对。

打包。先构建 Release，再用 `windeployqt --release --qmldir ui` 把 `MeowAi.exe` 与 Qt 运行库部署到临时目录，最后编译安装脚本。仓库内提供 Inno Setup 脚本 `scripts/MeowAi.iss`，`#define AppVersion "0.2.7.1"`，输出 `dist\MeowAi-Setup-0.2.7.1.exe`。本地实际打包另用 NSIS 脚本 `build-mingw\dist\MeowAi.nsi`（构建目录，不纳入版本库），版本字段同为 0.2.7.1，图标取自 `resources/branding/app.ico`。两种安装程序都按当前用户安装，默认目录 `C:\Users\<用户名>\AppData\Local\Programs\MeowAi`，不需要管理员权限。安装包内附带 `docs/USER_GUIDE.md`，个性化页的“打开用户手册”直接定位到该文件；发布时把安装包和两份手册放进同一个发放目录。

当前交付状态。`产品/` 目录里有 `MeowAi-Setup-0.2.7.1.exe`（NSIS 构建，约 33.3 MB）与 `MeowAi-Setup-0.2.7.exe`（历史产物）。`产品/` 被 `.gitignore` 忽略，不在版本库内。仓库没有 `dist/`。`scripts/MeowAi.iss` 的版本号已随 v0.2.7.1 提交。

冒烟检查。`MeowAi.exe --smoke-test` 在主界面加载后立即退出。退出码 0 只说明 QML 根对象加载成功，不覆盖配置、网络、数据库和资料库功能。程序是 WIN32 GUI 子系统，终端看不到标准输出，但可以读退出码：

```powershell
& "dist\MeowAi\MeowAi.exe" --smoke-test
$LASTEXITCODE
```

CI。`.github/workflows/ci.yml` 只构建默认目标，再执行 ctest。测试目标不在默认构建里，ctest 找不到可执行文件。当前 CI 不能提供有效测试证据，测试需要本地按上面的命令跑。

---

## 6. 运维排查手册

启动即退出。先用 `--smoke-test` 看退出码。非 0 时检查 Qt 运行库是否齐全、`qrc:/ui/main.qml` 是否打进资源、显卡驱动是否支持 Quick。发布包缺 `platforms/qwindows.dll` 时会在启动阶段报错。

界面空白或布局错乱。检查 `resources/branding`、`resources/themes`、`resources/splash` 是否打进 qrc。v0.2.7 使用 Svg 模块加载品牌图，部署时需要 `Qt6Svg.dll` 和 `imageformats/qsvg.dll`。缺失时 logo 不显示，其他界面仍可用。

Key 保存失败。Windows 下看 `secure/` 是否可写、DPAPI 是否可用。Linux/macOS 当前不支持加密，保存会失败。Web 端直接拒绝。错误码会写进界面提示，不写日志。

Key 验证失败。401/403 是 Key 无效。429 是限流。超时和连接失败是网络问题。厂商返回 404 多为服务地址少了 `/v1` 或模型名不存在。验证只发一次最小请求，不消耗对话上下文。

对话无回答。按顺序检查：厂商、模型、Key 是否齐全；档位参数是否被高级 JSON 覆盖成非法值；网络是否可达；SSE 是否被代理缓冲。代理缓冲表现为长时间无增量，最后一次性返回。需要求网关关闭缓冲。

图片发送被拒。看 `imagesConfirmed` 是否为 true，模型是否声明视觉能力。自定义厂商默认不支持视觉，除非网关兼容视觉消息格式。

资料库一直待建向量。看同意状态、embedding 厂商、模型名、Key、服务地址。看索引队列是否在运行。点“重建”后观察状态变化。HTTP 400 多为模型名或批量格式问题；v0.2.6 已按厂商 `maxBatchSize` 分片。HTTP 429 需要降低并发或换时段。

PDF 无文本层。确认构建时是否启用 PDFium。源码构建默认没有 `third_party/pdfium`。扫描版 PDF 即使有 PDFium 也提不出文字，需要 OCR，本版本未实现。

图片没有描述。确认 caption 厂商、模型、Key、服务地址，确认模型支持图片。未配置时图片只入库，不参与检索。

检索没有结果。确认小库已挂载、文件状态是“已索引”、相似度阈值不过高、FTS 兜底未被关闭。嵌入模型换了以后必须重建索引，旧向量维度不兼容。

会话丢失。检查 `%APPDATA%\MeowAi\MeowAi\metadata.db` 是否存在、是否可写。数据库迁移失败会进入内存模式，重启后会话丢失。备份时同时复制 `metadata.db` 和 `library/`、`assets/`。

升级失败。当前没有应用内升级。`UpdateManager` 只返回失败信号。手动下载新安装包覆盖安装，数据目录保留。

开屏异常。内置开屏不播放时看“启动时播放”开关。自定义视频不播放时看编码和体积上限 200 MB。视频损坏会回退到内置开屏。开屏期间点“跳过”应直接进入主界面。

头像不显示。看 `assets/` 下是否有裁剪结果，看图片是否损坏。裁剪输出固定 512×512，源图过小会提示加载失败。

---

## 7. 进度与路线

已交付到 v0.2.7.1。Windows 主链路完整：配置、对话、联网、资料库、参考模式、个性化、开屏、安装包。

当前优先级最高的收尾项：

1. 给 v0.2.7 与 v0.2.7.1 补 DEVLOG 记录，写清提交、变更、验收结果。
2. 修 CI：先构建 `meowai_tests`，再执行 ctest。
3. 在干净 Windows 虚拟机上做一次安装、升级、卸载验收。
4. 确认发布包是否内置 PDFium，并把结论写进发布说明。
5. 补资料库同意文案与实现的定期回归，避免文案再次出现“另行同意图片外发”。

未排期的模块：文件分类、修改前备份、应用内升级、Skill 调用、视频解析、OCR、Android/Web 客户端。这些模块目前只有骨架或未实现，不能对外承诺日期。

---

## 8. 已知限制

- v0.2.7 与 v0.2.7.1 没有 DEVLOG 条目，变更证据只有 git 提交。
- v0.2.7.1 的安装包在本地用 NSIS 脚本构建，该脚本位于 `build-mingw\dist\`，不纳入版本库。
- CI 不构建测试目标，测试证据只能来自本地。
- 仓库没有 `third_party/pdfium`，源码构建不能解析 PDF 文本层。
- 仓库没有 `dist/`，安装包只在被忽略的 `产品/` 目录里。
- 安装向导只有英文。
- 应用内升级是骨架，检查更新必定失败。
- 文件分类和修改前备份是骨架，界面没有入口。
- Skill 只扫描清单，不接入对话。
- 视频只登记，不解析；扫描版 PDF 不做 OCR。
- Android 与 Web 只有平台适配骨架；iOS 未开始。
- Linux 和 macOS 没有真实密钥加密，也没有安装包。
- 数据目录不提供自动备份。用户需要手动复制。
- 会话删除、文件移除、恢复默认外观都没有二次确认。
- 测试注释写“21 个”，实际 26 个。

---

## 9. 演示脚本

演示前准备：一个可用的 API Key；一个包含 PDF、Word、Excel、图片的资料文件夹；一台 1920×1080 的 Windows 电脑；断网备用热点。

1. 双击安装包，按英文向导完成安装。
2. 启动后等内置开屏播放，点“跳过”进入主界面。
3. 点顶栏“配置”，选 DeepSeek，填 Key，点“保存”，点“验证”，显示“已验证”。
4. 点模型胶囊，演示悬停打开、单击打开、长按打开三种方式。
5. 选一个支持视觉的模型，发送一条纯文字问题，展示流式输出、Token 与耗时。
6. 点“复制”，再点“重新生成”，展示消息操作。
7. 打开“联网搜索”，提问一个需要实时信息的问题，展示“联网中…”和来源胶囊。
8. 打开配置面板“安全”开关，附图提问，展示图片消息。
9. 切到资料库，新建小库，导入 PDF、Word、Excel 和图片。
10. 首次弹出同意层，讲清外发范围和撤回方式，点“同意并启用”。
11. 配置 embedding 与 caption，点“拉取模型”“验证可用性”，重建索引。
12. 回到对话，挂载小库，提问文件里的细节，展示引用和参考资料模式。
13. 切到个性化，切换两套主题，预览内置开屏，演示头像裁剪。
14. 打开帮助，点“打开用户手册”，展示随包文档。
15. 最后说明边界：分类、备份、升级、Skill、视频、OCR 尚未交付。

演示时不要说“自动更新”“本地大模型”“OCR 识别扫描件”“Skill 已接入”“Android 可用”。这些都不成立。

---

## 10. 开发协作速查

改动前先读 `agent.md`、`docs/AGENT.md`、`docs/PROJECT_CHARTER.md`、`docs/PLAN.md`、`docs/WORKFLOW.md`。

分层规则：QML 只调用注入的控制器；`core/` 不直接读 QML；`providers/` 只处理协议；`storage/` 只处理持久化；`platform/` 只处理平台差异。新增依赖先看是否已有抽象，不要跨层直连。

新增功能的最小闭环：控制器公开属性或 `Q_INVOKABLE`；QML 接线；状态与错误可显示；持久化可恢复；测试覆盖关键分支；手册写清入口和失败表现。

提交规范：一个提交只做一类事。提交信息用 `feat`、`fix`、`chore`、`docs`、`test` 前缀，写清模块和版本。不要把构建产物、安装包、密钥、截图临时文件提交进仓库。

测试规范：新逻辑至少覆盖成功、失败、边界三种路径。测试目标加进 `tests/CMakeLists.txt`。本地跑 `meowai_tests` 和 ctest，不依赖 CI。

文档规范：状态必须写已交付、待验收、骨架、未实现之一。路径、类名、命令必须能在仓库里核对。无法核对的产物写“无可考记录”，不要编造。

密钥规范：测试和演示不要提交真实 Key。日志、截图、错误信息里出现 Key 时按泄露处理，立即吊销。

---

## 11. 术语表

| 术语 | 含义 |
| --- | --- |
| 接入卡 / 配置面板 | 对话页顶部展开的厂商、模型、密钥、安全设置区域 |
| 模型胶囊 | 顶栏显示当前模型的按钮，点开是模型菜单 |
| 档位 | 厂商参数预设，分关、低、中、高 |
| 小库 | 资料库里的文件集合，可挂载到对话 |
| 挂载 | 让某一轮对话使用指定小库检索 |
| 片段 | 文件切分后的最小检索单元 |
| 向量 | 片段经 embedding 模型生成的数值表示 |
| FTS5 | SQLite 全文检索模块，本项目用 trigram 做关键词兜底 |
| 参考资料模式 | 按全文、章节、片段三级降级注入的检索策略 |
| 注入预算 | 允许放进上下文的资料库 token 上限 |
| 同意层 | 首次外发资料库内容前的隐私确认界面 |
| 安全开关 | 控制是否允许把图片上传给厂商 |
| 内置开屏 | 按主题播放的品牌动画，约 3.5 秒 |
| Skill | 预留的扩展能力清单，当前只扫描不调用 |
| 骨架 | 有接口或文件，但没有接入主流程 |
| smoke test | 只验证主界面能加载的冒烟检查 |

---

## 12. 对外沟通统一口径

可以说：Windows 桌面 AI 对话客户端；用户自备厂商 Key；本地保存会话和资料库；支持 PDF、Word、Excel、PPT 和图片入库；检索结果可追溯；资料外发需要明确同意；有两套主题和内置开屏；有 Windows 安装包。

不能说：内置大模型；本地推理；自动升级；Skill 已接入；视频理解；OCR 识别扫描件；Android/Web 已可用；密钥在 Linux/macOS 也加密；CI 已验证全部测试；安装包一定包含 PDFium。

价格和模型能力以厂商当期文档为准。不要承诺固定价格、固定速率或固定模型清单。

---

## 13. 工程结构与构建参考

目录结构：

```text
MeowAi/
├─ main.cpp                 应用入口、QML 上下文注入
├─ CMakeLists.txt           版本、Qt 模块、qrc、PDFium、测试开关
├─ agent.md                 仓库根 AI 入口规则
├─ core/                    应用层
│  ├─ AppController.*       装配、生命周期、帮助、开屏状态
│  ├─ ai/ProviderCatalog.*  厂商目录与模型清单
│  ├─ chat/                 ChatController、ChatHistoryModel
│  ├─ library/              LibraryController、LibraryIndexer、extract/*
│  ├─ retriever/            RagRetriever、FileLevelRetriever、BudgetCalculator 等
│  ├─ web/                  WebSearchService、WebSearchTool
│  ├─ theme/                ThemeManager、ThemePresetManager
│  ├─ skill/SkillRegistry.* 只扫描清单
│  └─ update/UpdateManager.* 骨架
├─ providers/               OpenAI、Anthropic、SSE 解析、ChatTypes
├─ storage/                 Database、各 Store、AppPaths、SecureStore
├─ platform/                PlatformAdapter 与 desktop/web 实现
├─ ui/                      QML 页面、组件、开屏、裁剪
├─ resources/               providers.json、pricing.json、themes、branding、splash、skills
├─ tests/                   26 个测试目标
├─ scripts/                 MeowAi.iss、fetch-pdfium.ps1
├─ docs/                    规范、计划、日志、两份手册
└─ 产品/                    本地发放目录，被 gitignore
```

数据库 schema 版本 4。

| 版本 | 表 |
| --- | --- |
| v1 | `meta`、`conversations`、`messages`、`message_attachments` |
| v2 | `collections`、`library_files`、`file_collections`、`folder_links`、`chunks`、`chunk_vectors`、`citations`、`chunk_fts` |
| v3 | `documents`、`sections` |
| v4 | `messages` 增加 `prompt_tokens`、`completion_tokens`、`total_tokens`、`elapsed_ms` |

配置分段落：`ai`（厂商、模型、档位、高级参数、自定义端点）、`library`（检索参数、参考资料模式、embedding、caption）、`web`（搜索端点与开关）、`theme`（主题、字体、背景、头像、开屏）、`personalization`（开关状态）。

构建与测试命令：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64" `
  -DMEOWAI_BUILD_TESTS=ON
cmake --build build --config Release
cmake --build build --config Debug --target meowai_tests
ctest --test-dir build -C Debug --output-on-failure
& "build\Release\MeowAi.exe" --smoke-test
$LASTEXITCODE
pwsh scripts/fetch-pdfium.ps1
```

打包命令：用 Inno Setup 编译器打开 `scripts/MeowAi.iss`，先确认 `#define AppVersion "0.2.7.1"`，再编译。产物复制到 `产品/`，同时复制 `docs/USER_GUIDE.md` 和 `docs/DEVELOPER_GUIDE.md`。用 NSIS 打包时，先确认 `build-mingw\dist\MeowAi.nsi` 中的 `APP_VERSION`、`Name`、`OutFile`、`VIProductVersion`、`FileVersion`、`ProductVersion`、`DisplayVersion` 七处版本号一致，再执行 `makensis build-mingw\dist\MeowAi.nsi`。

---

## 14. 版本变更记录（v0.1.0 → v0.2.7.1）

仓库没有 annotated tag 历史，原有版本标识实际是分支 ref。2026-10-22 为 v0.2.3、v0.2.4 补建了本地 annotated tag，均指向合并提交 `9661eff`；标签未推送远端。

| 版本 | 日期 | commit | 证据来源 |
| --- | --- | --- | --- |
| v0.1.0 | 2026-10-19 | `d23d1b0` | `origin/v0.1.0`；`git show d23d1b0`；DEVLOG L763–775 |
| v0.2.0 | 2026-10-19 | `0145c43` | 本地/远端 `v0.2.0`；`git show 0145c43`；DEVLOG L687–761 |
| v0.2.1 | 2026-10-19 | `4ead947` | `origin/v0.2.1`；`git show 4ead947`；DEVLOG L623–685 |
| v0.2.3 | 2026-10-20 | `9661eff` | 与 v0.2.4 合并提交；2026-10-22 补建 tag `v0.2.3`；DEVLOG L539–621 |
| v0.2.4 | 2026-10-20 | `9661eff` | 同上；补建 tag `v0.2.4`；DEVLOG L518–537 |
| v0.2.5 | 2026-10-21（提交） | `aee4aa5` | `origin/v0.2.5`；DEVLOG 记录日期 2026-10-20，L139–518 |
| v0.2.6 | 2026-10-21 | `9bbe54f` | HEAD 前代；基线 `ce1bdae` 无 ref；DEVLOG L42–121 |
| v0.2.7 | 2026-10-22 | `5622ac4` | 基线 `ca2d4a5`；`origin/v0.2.7`；DEVLOG 无记录 |
| v0.2.7.1 | 2026-10-24 | `{{COMMIT}}` | 基线 `ad3e664`；`origin/v0.2.7.1`；DEVLOG 无记录 |

v0.1.0

| 字段 | 内容 |
| --- | --- |
| 版本号 | v0.1.0 |
| 日期 | 2026-10-19 |
| commit | `d23d1b0` |
| 新增 | Qt 6 + CMake 工程骨架；`AppController`；`PlatformAdapter` 桌面实现；`AppPaths` 数据布局；`SecureStore` + DPAPI；`ProviderCatalog`；SQLite schema v1；对话页与标题栏雏形；4 个测试目标 |
| 修改 | 无前置版本 |
| 修复 | 无前置版本 |
| 未完成或已知限制 | 只有骨架；对话未接真实厂商；资料库未开始；主题未开始 |
| 证据来源 | `git show d23d1b0`；DEVLOG L763–775 |
| 状态 | 已交付（骨架） |

v0.2.0

| 字段 | 内容 |
| --- | --- |
| 版本号 | v0.2.0 |
| 日期 | 2026-10-19 |
| commit | `0145c43` |
| 新增 | 真实对话链路；`OpenAiProvider` SSE；`AnthropicProvider`；`ChatStreamParser`；`ChatStore` 会话与消息持久化；`ChatController`；主流式界面；模型菜单基础交互 |
| 修改 | CMake 增加 Network、Multimedia、QuickControls2（`git show 0145c43:CMakeLists.txt` 未声明 Svg）；`Main.qml` 重构 |
| 修复 | SSE 增量拼接、错误事件与结束事件的边界处理 |
| 未完成或已知限制 | 联网、资料库、个性化未开始；只有 OpenAI 兼容协议和 Anthropic 两条链路 |
| 证据来源 | `git show 0145c43`；DEVLOG L687–761 |
| 状态 | 已交付 |

v0.2.1

| 字段 | 内容 |
| --- | --- |
| 版本号 | v0.2.1 |
| 日期 | 2026-10-19 |
| commit | `4ead947` |
| 新增 | `ThemeManager`；`PersonalizationStore`；两套主题预设；字体、背景、头像导入；自定义开屏视频；帮助弹层；恢复默认外观 |
| 修改 | `Main.qml` 接入主题与个性化；资源目录纳入 qrc |
| 修复 | 资源文件体积校验；导入文件名清洗；同名文件自动改名 |
| 未完成或已知限制 | 头像只有选择没有裁剪；开屏只有视频没有内置动画；主题只有数据层验证 |
| 证据来源 | `git show 4ead947`；DEVLOG L623–685 |
| 状态 | 已交付 |

v0.2.3

| 字段 | 内容 |
| --- | --- |
| 版本号 | v0.2.3 |
| 日期 | 2026-10-20 |
| commit | `9661eff`（与 v0.2.4 合并提交；2026-10-22 补建 tag） |
| 新增 | 联网搜索：`WebSearchService`、`WebSearchTool`、`WebConfigStore`；搜索来源展示；工具循环最多 3 轮；自定义搜索端点与密钥 |
| 修改 | `ChatController` 增加工具调用分支；对话页增加联网开关；配置增加联网段落 |
| 修复 | 工具结果为空时的降级、工具调用轮次上限、来源去重 |
| 未完成或已知限制 | 只有 DeepSeek 内置源；其他厂商需要手动配置端点；搜索质量依赖外部服务 |
| 证据来源 | `git show 9661eff`；DEVLOG L539–621；补建 tag `v0.2.3` |
| 状态 | 已交付 |

v0.2.4

| 字段 | 内容 |
| --- | --- |
| 版本号 | v0.2.4 |
| 日期 | 2026-10-20 |
| commit | `9661eff`（与 v0.2.3 合并提交；2026-10-22 补建 tag） |
| 新增 | 联网来源胶囊与来源明细；搜索配置校验与未就绪提示 |
| 修改 | 联网开关状态文案、来源展示位置、错误提示 |
| 修复 | 搜索密钥缺失时的开关行为、来源链接点击 |
| 未完成或已知限制 | 没有独立提交，无法把 v0.2.4 的改动逐行从合并提交中拆出 |
| 证据来源 | `git show 9661eff`；DEVLOG L518–537；补建 tag `v0.2.4` |
| 状态 | 已交付 |

v0.2.5

| 字段 | 内容 |
| --- | --- |
| 版本号 | v0.2.5 |
| 日期 | 2026-10-21（提交）；DEVLOG 记录日期 2026-10-20 |
| commit | `aee4aa5` |
| 新增 | 资料库主链路：小库、文件导入、文件夹关联、索引队列、状态标签；文本与图片提取器；`ChunkSplitter`；`EmbeddingService`；`SqliteVectorIndex`；`RagRetriever`；FTS5 兜底；引用；同意层；成本预估；库内搜索；资料库页面 |
| 修改 | schema 升到 v2；`ChatController` 接入检索上下文；`AiConfigStore` 增加 embedding、caption 配置 |
| 修复 | 图片转码、文件名冲突、删除库不删原文件、索引队列停止 |
| 未完成或已知限制 | 参考资料模式未开始；PDF 依赖 PDFium；扫描件无 OCR；21 个测试目标（DEVLOG 记 22/22 通过，计数口径不同） |
| 证据来源 | `git show aee4aa5`；DEVLOG L139–518 |
| 状态 | 已交付（初步验收） |

v0.2.6

| 字段 | 内容 |
| --- | --- |
| 版本号 | v0.2.6 |
| 日期 | 2026-10-21 |
| commit | `9bbe54f`（基线 `ce1bdae` 无 ref；热修复后 `9bbe54f` 为 HEAD） |
| 新增 | 参考资料模式：`FileLevelRetriever`、`FullTextAssembler`、`SectionPicker`、`BudgetCalculator`、`InjectionPlanner`；schema v3 的 `documents`、`sections`；引用明细与降级原因；23 个测试目标 |
| 修改 | `ChatController` 注入计划；资料库配置增加单文件预算、全局预算、回答预留、聚合权重、未知窗口回退；检索默认值集中到 `LibraryConfigStore` |
| 修复 | 图片 embedding 按厂商 `maxBatchSize` 分片（Qwen 10 等）；超长文本截断；限流退避；未知窗口回退改为 32768 |
| 未完成或已知限制 | 全文注入受模型窗口限制；章节选择是启发式；手工验收记录不完整；DEVLOG 原记录写 16384，与代码 32768 不一致，已于 2026-10-22 勘误 |
| 证据来源 | `git show 9bbe54f`；`git show ce1bdae`；DEVLOG L42–121 |
| 状态 | 已交付 |

v0.2.7

| 字段 | 内容 |
| --- | --- |
| 版本号 | v0.2.7 |
| 日期 | 2026-10-22 |
| commit | `5622ac4`（基线 `ca2d4a5`；文档 `a61859d`；安装脚本 `82fcb60`，均属于 v0.2.7） |
| 新增 | 界面重构：可折叠侧栏、会话列表、顶栏模型胶囊与模型菜单、资料库右侧配置抽屉、标题栏品牌区；`AvatarCrop.qml` 512×512 裁剪；`SplashIntro.qml` 内置开屏（约 3.5 秒、按主题、可跳过、音效开关）；消息复制与重新生成；分享文字与分享图片（PNG 保存）；Markdown 渲染；助手消息 Token 与耗时；schema v4 的 `messages` 用量列；资料库配置抽屉的拉取模型、服务地址、保存与验证；`branding` 品牌资源与应用图标；Svg Qt 模块；新增 `tst_chatfeatures`、`tst_libraryservices`、`tst_libraryurls`，测试目标共 26 个 |
| 修改 | `main.qml` 重构为侧栏 + 内容区 + 顶栏；`ConversationPage.qml` 改为顶部模型条与内联配置面板；`LibraryPage.qml` 拆分出 `LibraryConfigPanel.qml`；`PersonalizationPage.qml` 增加内置开屏预览与“打开用户手册”；`AppController` 增加手册定位与开屏状态；`ChatController` 增加 `renderMarkdown`、`regenerateLast` 与用量记录；版本号统一为 0.2.7 |
| 修复 | 无 DEVLOG 记录，提交信息为 `feat` 与 `chore`，未列出修复项，无法逐条核对 |
| 未完成或已知限制 | DEVLOG 没有 v0.2.7 条目；仓库没有 `dist/`，安装包只存在于被忽略的 `产品/`；没有干净虚拟机验收记录；CI 仍不构建测试；PDFium、分类、备份、升级、Skill、视频、OCR、Android/Web 的限制不变 |
| 证据来源 | `git show 5622ac4`；`git show ca2d4a5`；`git show a61859d`；`git show 82fcb60`；`git log --oneline 9bbe54f..82fcb60`；DEVLOG 无 v0.2.7 记录 |
| 状态 | 已交付（Windows 初步验收；发布记录不完整） |

v0.2.7.1

| 字段 | 内容 |
| --- | --- |
| 版本号 | v0.2.7.1 |
| 日期 | 2026-10-24 |
| commit | `{{COMMIT}}`（基线 `ad3e664`；本次合入 M13、M14、M15 与版本号更新） |
| 新增 | 顶部 Toast：新增 `ui/MToast.qml`，由 `main.qml` 统一持有，资料库导入与解析失败、个性化页背景 / 开屏 / 头像读取失败改由页面顶部弹出；`LibraryPage.qml` 记录已提示的失败文件，历史失败不重复弹；`AvatarCrop.qml` 增加 `errorReported` 信号，图片无法读取时收起裁剪层并转 Toast；`SplashVideo.qml` 增加 `finishPlayback()`，跳过与自然结束共用一次性结束逻辑；`main.qml` 增加开屏层状态（`splashLayerVisible`、`mainOpacity`、`mainRevealed`）与开屏配色映射；文档上限调整：文档类声明上限 1 GB、整文件载入解析的保护阈值 512 MB、图片单张上限 128 MB |
| 修改 | `main.qml` 窗口底色改为透明，开屏底板移入圆角遮罩内，修复自定义背景与开屏在四角的白色露出；不开开屏时首帧直接显示主内容；离开页面时自动收起该页专属面板（离开对话页关配置面板、离开资料库页关配置抽屉）；`SplashIntro.qml` 删除“播放中 · 有声/静音”小字与进度条，底部只保留小 logo 与品牌名；`SplashVideo.qml` 删除同一组小字、进度条与“正在载入…”提示；`ConversationPage.qml` 增加资料库与联网搜索未就绪原因提示，联网开关开启态改用 `ok` 语义色；`PersonalizationPage.qml` 背景与开屏说明文字写明支持格式与体积上限；`ThemeManager.cpp` 与 `Extractor.h` 抽出字节换算与体积校验工具函数，提示数字由常量推导；`resources/themes/theme2.json` 的 `ok`、`okGhost`、`okHair` 加深，浅蓝主题下的成功态文字更清晰；`ui/Theme.qml` 补成功语义令牌注释；版本号统一为 0.2.7.1（`CMakeLists.txt`、`README.md`、两份手册、`scripts/MeowAi.iss`） |
| 修复 | 自定义背景与开屏在窗口四角的白色露出；首帧先显示主内容再被开屏覆盖的闪烁；开屏交叉淡化期间尾音继续输出；`PersonalizationPage.qml` 与 `LibraryPage.qml` 文件类错误在页面底部与 Toast 重复展示；体积提示“512 MB 超出 512 MB”一类取整歧义 |
| 未完成或已知限制 | DEVLOG 没有 v0.2.7.1 条目；`build-mingw\dist\MeowAi.nsi` 不在版本库，安装包只能在本机复现；M17–M20（会话切换页、角色卡、Skill 仓库、Agent 隔离区）未实现；文档类 1 GB 为声明上限，文本 / Office / PDF 解析仍需整文件载入，实际超过 512 MB 会提前拒绝；CI 仍不构建测试；PDFium、分类、备份、升级、视频、OCR、Android/Web 的限制不变 |
| 证据来源 | v0.2.7.1 分支首个提交（`{{COMMIT}}`）；`git diff ad3e664`；`git status`；DEVLOG 无 v0.2.7.1 记录 |
| 状态 | 已交付（Windows 构建与冒烟通过；安装包本机构建；发布记录不完整） |
