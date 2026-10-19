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
