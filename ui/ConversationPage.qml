import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

/**
 * 对话页：接入设置（厂商 / 模型 / 档位 / 密钥）+ 消息流 + 输入条。
 *
 * 会话列表与模型选择条已抽为独立组件（ConversationList / ModelStrip），
 * 由外壳统一放置；本页不再内置左侧会话面板与“当前模型”条，消息区占满整宽。
 *
 * 纯展示与绑定：
 *  - 消息流直接绑定 core 的会话历史模型 `History`（ChatHistoryModel），
 *    持久化、上下文裁剪、流式节流都在 core 层，本文件不碰数据库；
 *  - 发送经 `Chat`（ChatController）转发，本文件不碰网络与密钥；
 *  - 助手消息经 `Chat.renderMarkdown()` 转成 RichText HTML；链接交给系统浏览器，
 *    复制 / 重新生成 / 分享图片等动作只转发 `Chat` 或本页的 UI 能力；
 *  - 厂商 / 模型 / 档位全部来自 `Chat.providerOptions()` / `modelOptions()` / `thinkingLevels`，
 *    即 core/ai/ProviderCatalog 的数据视图——**新增厂商零 UI 改动**。
 *
 * 布局全部在本文件内自由编排（阶段三要求：UI 组件位置可在 ui 模块内自定义）。
 */
Item {
    id: page

    /// 气泡最大宽度：富文本同样受这个上限约束，避免表格 / 代码块横贯整窗
    readonly property real bubbleMaxWidth: Math.min(760, Math.max(240, page.width - 96))
    /// 统一展示的错误：优先网络 / 密钥错误，其次会话历史错误
    readonly property string errorText: Chat.lastError.length > 0 ? Chat.lastError : History.lastError

    /// grabToImage 的产物，等保存对话框确认后落盘
    property var pendingMessageImage: null

    /// 接入配置面板是否展开：由外壳次顶部 Dock 的「配置」按钮驱动，本页不做开关逻辑
    property bool configOpen: false

    /// 厂商 / 模型下拉的数据快照：ComboBox 需要字符串数组，id 单独并行保存
    property var providerNames: []
    property var providerIds: []
    property var modelNames: []
    property var modelIds: []
    /// 当前「厂商 + 模型」是否存在高级原生参数覆盖
    property bool hasAdvanced: false
    /// 等待用户同意后再挂载的小库 id（首次启用资料库时用）
    property int pendingMountId: 0

    /// 档位显示名：档位是契约常量，展示名只做本地化，不影响下发字段
    function levelLabel(level) {
        switch (level) {
        case "off": return qsTr("关")
        case "low": return qsTr("低")
        case "medium": return qsTr("中")
        case "high": return qsTr("高")
        default: return level
        }
    }

    /// 助手正文的 RichText：M5 负责 Markdown → HTML，QML 不做 Markdown 解析。
    /// `QTextDocumentFragment::toHtml()` 会在 <body> 上写死 'Sans Serif'/9pt，
    /// 会盖掉 Theme 字体，这里只清掉这两个声明，其余结构原样交给 Text.RichText。
    function markdownHtml(text) {
        return Chat.renderMarkdown(text).replace(/<body([^>]*)>/i, function (tag, attrs) {
            return "<body" + attrs.replace(/\s*font-(?:family|size)\s*:\s*[^;"]*;?/gi, "") + ">"
        })
    }

    /// 写入系统剪贴板：TextEdit 是 QtQuick 的 UI 能力，不涉及业务数据
    function copyText(value) {
        clipHelper.text = value
        clipHelper.selectAll()
        clipHelper.copy()
    }

    /// FileDialog 的 selectedFile 是 file:// URL，saveToFile 需要本地路径
    function localPath(urlValue) {
        const raw = String(urlValue)
        if (raw.indexOf("file:///") !== 0)
            return raw
        let path = decodeURIComponent(raw.substring(8))
        if (!/^[A-Za-z]:/.test(path))
            path = "/" + path
        return path
    }

    /// 抓取气泡成 PNG，再交给保存对话框
    function shareImage(item) {
        item.grabToImage(function (result) {
            if (!result)
                return
            page.pendingMessageImage = result
            saveImageDialog.open()
        })
    }

    function refreshProviders() {
        const options = Chat.providerOptions()
        const names = []
        const ids = []
        for (let i = 0; i < options.length; ++i) {
            names.push(options[i].displayName)
            ids.push(options[i].id)
        }
        page.providerNames = names
        page.providerIds = ids
        providerBox.currentIndex = ids.indexOf(Chat.providerId)
    }

    function refreshModels() {
        const options = Chat.modelOptions(Chat.providerId)
        const names = []
        const ids = []
        for (let i = 0; i < options.length; ++i) {
            names.push(options[i].displayName)
            ids.push(options[i].id)
        }
        page.modelNames = names
        page.modelIds = ids
        modelBox.currentIndex = ids.indexOf(Chat.modelId)
    }

    function refreshAdvanced() {
        page.hasAdvanced = Object.keys(Chat.advancedParams()).length > 0
    }

    function refreshSelection() {
        refreshModels()
        refreshAdvanced()
    }

    /// 密钥三态徽标（文案与色调都只在这里映射，避免散落各处）
    function credentialTone() {
        switch (Chat.credentialStatus) {
        case "valid": return "ok"
        case "invalid": return "danger"
        case "notConfigured": return "danger"
        case "unverifiable": return "accent"
        default: return "neutral"
        }
    }
    function credentialText() {
        if (Chat.validating)
            return qsTr("校验中…")
        switch (Chat.credentialStatus) {
        case "valid": return qsTr("密钥有效")
        case "invalid": return qsTr("密钥无效")
        case "notConfigured": return qsTr("未配置密钥")
        case "unverifiable":
            return Chat.credentialMessage.length > 0 ? Chat.credentialMessage : qsTr("无法验证")
        default:
            return Chat.hasApiKey ? qsTr("密钥已就绪（未校验）") : qsTr("未配置密钥")
        }
    }

    /// 组装一次发送：用户消息由 core 落库入模型，这里只负责转发与清空输入
    function submit() {
        const text = composerField.text.trim()
        if (text.length === 0 || Chat.busy)
            return

        const imagePath = imageField.text.trim()
        if (imagePath.length > 0 && Chat.supportsVision)
            Chat.sendWithImage(text, imagePath)
        else
            Chat.send(text)

        composerField.text = ""
        transcriptView.positionViewAtEnd()
    }

    Component.onCompleted: {
        refreshProviders()
        refreshSelection()
    }

    // ============================================================ 接入配置面板
    // 展开状态由外壳（次顶部 Dock 的「配置」按钮）驱动；收起时整卡高度归零，
    // 页面不再保留「接入 / chips / 配置」这一行。
    MSurface {
        id: connCard
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: page.configOpen ? connBody.implicitHeight + Theme.s4 * 2 : 0
        clip: true

        Behavior on height {
            NumberAnimation { duration: Theme.dBase; easing.type: Theme.easeInOut }
        }

        // ---- 展开体 ----
        Column {
            id: connBody
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: Theme.s4
            anchors.rightMargin: Theme.s4
            anchors.topMargin: Theme.s4
            spacing: Theme.s3
            opacity: page.configOpen ? 1 : 0
            enabled: page.configOpen

            Behavior on opacity {
                NumberAnimation { duration: Theme.dBase }
            }

            // 厂商 + 模型
            Row {
                width: parent.width
                spacing: Theme.s3

                MLabel {
                    label: qsTr("厂商")
                    anchors.verticalCenter: parent.verticalCenter
                }

                MCombo {
                    id: providerBox
                    width: Math.min(176, parent.width * 0.25)
                    model: page.providerNames
                    currentIndex: -1
                    placeholderText: qsTr("选择厂商")
                    onActivated: (index) => Chat.setProvider(page.providerIds[index])
                }

                MLabel {
                    label: qsTr("模型")
                    anchors.verticalCenter: parent.verticalCenter
                }

                MCombo {
                    id: modelBox
                    width: Math.min(250, parent.width * 0.35)
                    model: page.modelNames
                    currentIndex: -1
                    enabled: page.modelNames.length > 0
                    placeholderText: Chat.customizable ? qsTr("自定义厂商请在下方手填模型名")
                                                       : qsTr("选择模型")
                    onActivated: (index) => Chat.setModelId(page.modelIds[index])
                }
            }

            // 自定义厂商：手填 API 链接与模型名称
            Row {
                width: parent.width
                spacing: Theme.s3
                visible: Chat.customizable

                MLabel {
                    label: qsTr("API 链接")
                    anchors.verticalCenter: parent.verticalCenter
                }

                MField {
                    id: baseUrlField
                    width: Math.min(320, parent.width * 0.45)
                    text: Chat.customBaseUrl
                    placeholderText: qsTr("https://your-gateway.example/v1")
                    onEditingFinished: Chat.setCustomBaseUrl(text)
                }

                MLabel {
                    label: qsTr("模型名")
                    anchors.verticalCenter: parent.verticalCenter
                }

                MField {
                    id: customModelField
                    width: Math.min(190, parent.width * 0.3)
                    text: Chat.modelId
                    placeholderText: qsTr("例如 gpt-5")
                    onEditingFinished: Chat.modelId = text
                }
            }

            Text {
                width: parent.width
                visible: Chat.customizable
                text: qsTr("自定义厂商只按 OpenAI 兼容协议发送（/chat/completions）；链接与模型名填全后才会发请求。")
                color: Theme.textGhost
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
            }

            // 思考档位 + 高级参数入口
            Row {
                spacing: Theme.s2

                MLabel {
                    label: qsTr("档位")
                    anchors.verticalCenter: parent.verticalCenter
                }

                Repeater {
                    model: Chat.thinkingLevels

                    Rectangle {
                        id: levelChip
                        required property string modelData

                        readonly property bool active: Chat.thinkingLevel === levelChip.modelData

                        width: levelText.implicitWidth + 16
                        height: 24
                        radius: Theme.rPill
                        color: levelChip.active ? Theme.accent : Theme.overlayHover
                        border.width: 1
                        border.color: levelChip.active ? Theme.accent : Theme.hairline

                        Behavior on color {
                            ColorAnimation { duration: Theme.dFast }
                        }

                        Text {
                            id: levelText
                            anchors.centerIn: parent
                            text: page.levelLabel(levelChip.modelData)
                            color: levelChip.active ? Theme.accentInk : Theme.textDim
                            font.pixelSize: Theme.fLabel
                            font.weight: Font.Medium
                        }

                        TapHandler { onTapped: Chat.setThinkingLevel(levelChip.modelData) }
                    }
                }

                Text {
                    visible: Chat.thinkingLevels.length === 0
                    text: qsTr("该模型未声明思考档位")
                    color: Theme.textGhost
                    font.pixelSize: Theme.fSmall
                    anchors.verticalCenter: parent.verticalCenter
                }

                MChip {
                    visible: page.hasAdvanced
                    tone: "accent"
                    text: qsTr("高级参数已覆盖档位")
                    anchors.verticalCenter: parent.verticalCenter
                }

                MButton {
                    text: qsTr("拉取清单")
                    variant: "quiet"
                    small: true
                    visible: Chat.modelListFetchable
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: Chat.fetchModelList()
                }
            }

            // 密钥
            Row {
                spacing: Theme.s3

                MLabel {
                    label: qsTr("密钥")
                    anchors.verticalCenter: parent.verticalCenter
                }

                MField {
                    id: keyField
                    width: 300
                    echoMode: TextInput.Password
                    placeholderText: Chat.dataStoreAvailable
                                     ? qsTr("仅加密存入本机保险库，绝不明文落盘")
                                     : qsTr("本平台不支持加密存储，无法保存")
                    enabled: Chat.dataStoreAvailable
                }

                MButton {
                    text: qsTr("保存")
                    variant: "primary"
                    small: true
                    enabled: Chat.dataStoreAvailable && keyField.text.length > 0
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: {
                        if (Chat.saveApiKey(keyField.text))
                            keyField.text = ""
                    }
                }

                MButton {
                    text: qsTr("清除")
                    variant: "quiet"
                    small: true
                    enabled: Chat.dataStoreAvailable && Chat.hasApiKey
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: Chat.eraseApiKey()
                }

                MButton {
                    text: qsTr("验证")
                    variant: "ghost"
                    small: true
                    enabled: Chat.dataStoreAvailable && Chat.hasApiKey && !Chat.validating
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: Chat.validateApiKey()
                }

                MChip {
                    dot: true
                    tone: page.credentialTone()
                    text: page.credentialText()
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // 图片上传确认（安全约束：必须显式确认）
            Row {
                spacing: Theme.s3

                MLabel {
                    label: qsTr("安全")
                    anchors.verticalCenter: parent.verticalCenter
                }

                MSwitch {
                    id: imageConfirmSwitch
                    checked: Chat.imagesConfirmed
                    onToggled: (value) => Chat.imagesConfirmed = value
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    text: qsTr("允许把图片上传给厂商（默认关闭；关闭时带图请求会被拒绝）")
                    color: Chat.imagesConfirmed ? Theme.warn : Theme.textFaint
                    font.pixelSize: Theme.fSmall
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // 环境异常提示：仅在对应能力不可用时出现
            Row {
                width: parent.width
                spacing: Theme.s2

                MChip {
                    visible: !Chat.dataStoreAvailable
                    tone: "danger"
                    text: qsTr("本平台不支持加密存储")
                }

                MChip {
                    visible: !History.available
                    tone: "danger"
                    text: qsTr("会话历史不可用")
                }
            }

        }
    }

    // ---------------------------------------------------------- 底部区
    Column {
        id: bottomArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: Theme.s2

        // 错误横幅：只在有错误时占位
        MSurface {
            id: errorBanner
            width: parent.width
            height: 42
            visible: page.errorText.length > 0
            radius: Theme.rMd
            fill: Theme.dangerGhost
            stroke: Theme.dangerHair

            Row {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.s3
                anchors.rightMargin: Theme.s3
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.s2

                MLabel {
                    label: qsTr("错误")
                    tone: Theme.danger
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    width: parent.width - 60
                    text: page.errorText
                    color: Theme.danger
                    font.pixelSize: Theme.fSmall
                    elide: Text.ElideRight
                    maximumLineCount: 2
                    wrapMode: Text.Wrap
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        // 联网来源 chip：仅在本轮检索命中时出现（数据来自 Chat；本文件不碰网络）
        Item {
            width: parent.width
            height: webSourceFlow.height
            visible: Chat.webSearchSources.length > 0

            Flow {
                id: webSourceFlow
                width: parent.width
                spacing: Theme.s2

                MChip {
                    dot: true
                    tone: "ok"
                    maxWidth: webSourceFlow.width
                    text: qsTr("已联网 · %1 条来源").arg(Chat.webSearchSources.length)
                }

                Repeater {
                    model: Chat.webSearchSources.slice(0, 3)

                    MChip {
                        required property var modelData
                        tone: "neutral"
                        maxWidth: webSourceFlow.width
                        text: modelData.title.length > 20
                              ? modelData.title.substring(0, 20) + "…"
                              : modelData.title
                    }
                }
            }
        }

        // 来源 chip：回答里引用的文件名 + 定位（可核对到页 / 表 / 行）
        Item {
            width: parent.width
            height: sourceFlow.height
            visible: Chat.librarySources.length > 0

            Flow {
                id: sourceFlow
                width: parent.width
                spacing: Theme.s2

                MLabel {
                    label: qsTr("来源")
                    height: 22
                }

                Repeater {
                    model: Chat.librarySources.slice(0, 3)

                    MChip {
                        required property var modelData
                        tone: "accent"
                        maxWidth: sourceFlow.width - 40
                        text: modelData.fileName + " · " + modelData.locLabel
                    }
                }
            }
        }

        // 注入级别：每个相关文件的注入方式（全文 / 章节 / 片段）与截断状态
        Item {
            width: parent.width
            height: injectionFlow.height
            visible: Chat.libraryInjections.length > 0

            Flow {
                id: injectionFlow
                width: parent.width
                spacing: Theme.s2

                MLabel {
                    label: qsTr("注入")
                    height: 22
                }

                Repeater {
                    model: Chat.libraryInjections.slice(0, 3)

                    MChip {
                        required property var modelData
                        tone: modelData.truncated ? "accent" : "neutral"
                        maxWidth: injectionFlow.width - 40
                        text: modelData.fileName
                              + " · " + modelData.level
                              + " · " + modelData.tokenEst + " token"
                              + (modelData.approximate ? qsTr("（近似章节）") : "")
                              + (modelData.truncated ? qsTr("（已截断）") : "")
                    }
                }
            }
        }

        // 输入条
        MSurface {
            id: composer
            width: parent.width
            height: composerCol.implicitHeight + Theme.s4 * 2
            elevated: true

            Column {
                id: composerCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.s4
                anchors.rightMargin: Theme.s4
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.s2

                Row {
                    width: parent.width
                    spacing: Theme.s2

                    // 「联网搜索」开关：像网页端一样随手开合（UI 侧唯一显眼入口，配置收进第三级弹层）
                    Rectangle {
                        id: webToggle
                        width: webToggleRow.implicitWidth + 20
                        height: 34
                        radius: Theme.rPill
                        color: Chat.webSearchEnabled
                               ? (webToggleHover.hovered ? Theme.accentHi : Theme.accent)
                               : (webToggleHover.hovered ? Theme.overlayHover : Theme.overlayFaint)
                        border.width: 1
                        border.color: Chat.webSearchEnabled ? Theme.accent : Theme.hairline

                        Behavior on color {
                            ColorAnimation { duration: Theme.dFast }
                        }

                        Row {
                            id: webToggleRow
                            anchors.centerIn: parent
                            spacing: 6

                            Rectangle {
                                width: 6
                                height: 6
                                radius: 3
                                anchors.verticalCenter: parent.verticalCenter
                                color: Chat.webSearchEnabled
                                       ? Theme.accentInk
                                       : (Chat.webSearchReady ? Theme.ok : Theme.textFaint)
                            }

                            Text {
                                text: Chat.webSearchSearching ? qsTr("联网中…") : qsTr("联网搜索")
                                color: Chat.webSearchEnabled ? Theme.accentInk : Theme.textDim
                                font.pixelSize: Theme.fLabel
                                font.weight: Font.Medium
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        HoverHandler { id: webToggleHover }

                        TapHandler {
                            onTapped: Chat.webSearchEnabled = !Chat.webSearchEnabled
                        }
                    }

                    // 「资料库」入口：选择要参与本轮检索的小库（只影响上下文，不改库内容）
                    Rectangle {
                        id: libraryToggle
                        width: libraryToggleRow.implicitWidth + 20
                        height: 34
                        radius: Theme.rPill
                        color: Chat.libraryMounted
                               ? (libraryToggleHover.hovered ? Theme.accentHi : Theme.accent)
                               : (libraryToggleHover.hovered ? Theme.overlayHover : Theme.overlayFaint)
                        border.width: 1
                        border.color: Chat.libraryMounted ? Theme.accent : Theme.hairline

                        Behavior on color {
                            ColorAnimation { duration: Theme.dFast }
                        }

                        Row {
                            id: libraryToggleRow
                            anchors.centerIn: parent
                            spacing: 6

                            Rectangle {
                                width: 6
                                height: 6
                                radius: 3
                                anchors.verticalCenter: parent.verticalCenter
                                color: Chat.libraryMounted
                                       ? Theme.accentInk
                                       : (Library.consentGiven ? Theme.ok : Theme.textFaint)
                            }

                            Text {
                                text: Chat.libraryMounted
                                      ? qsTr("资料库 · %1").arg(Chat.mountedCollections.length)
                                      : qsTr("资料库")
                                color: Chat.libraryMounted ? Theme.accentInk : Theme.textDim
                                font.pixelSize: Theme.fLabel
                                font.weight: Font.Medium
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        HoverHandler { id: libraryToggleHover }

                        TapHandler {
                            onTapped: libraryMenu.opened ? libraryMenu.close() : libraryMenu.open()
                        }
                    }

                    Popup {
                        id: libraryMenu
                        parent: libraryToggle
                        x: 0
                        y: -height - 8
                        width: 300
                        padding: 10
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                        background: Rectangle {
                            radius: Theme.rMd
                            color: Theme.ink3
                            border.width: 1
                            border.color: Theme.hairlineStrong
                        }

                        contentItem: Column {
                            spacing: Theme.s1

                            MLabel { label: qsTr("挂载到对话的小库") }

                            Text {
                                width: 280
                                visible: Library.collections.length === 0
                                text: qsTr("还没有小库：去左侧「资料库」创建并导入文件")
                                color: Theme.textFaint
                                font.pixelSize: Theme.fLabel
                                wrapMode: Text.Wrap
                            }

                            ListView {
                                width: 280
                                // 高度封顶：小库再多也不会把弹层顶出屏幕
                                height: Math.min(240, contentHeight)
                                clip: true
                                spacing: Theme.s1
                                model: Library.mountOptions

                                delegate: Item {
                                    id: mountRow
                                    required property var modelData
                                    width: ListView.view ? ListView.view.width : 0
                                    height: 34

                                    Text {
                                        anchors.left: parent.left
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width - 56
                                        text: mountRow.modelData.name
                                        color: Theme.text
                                        font.pixelSize: Theme.fSmall
                                        elide: Text.ElideRight
                                    }

                                    MSwitch {
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        checked: mountRow.modelData.mounted
                                        onToggled: {
                                            if (checked && !Library.consentGiven) {
                                                page.pendingMountId = mountRow.modelData.id
                                                libraryMenu.close()
                                                libraryConsentDialog.open = true
                                            } else {
                                                Library.setMounted(mountRow.modelData.id, checked)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    MField {
                        id: composerField
                        // 下限兜底：窗口再窄也不会把输入框压成负数而溢出行布局
                        width: Math.max(120, parent.width - webToggle.width - libraryToggle.width - sendButton.width
                                        - (cancelButton.visible ? cancelButton.width : 0)
                                        - parent.spacing * (cancelButton.visible ? 4 : 3))
                        placeholderText: Chat.busy
                                         ? qsTr("正在生成…")
                                         : qsTr("输入消息，回车发送")
                        enabled: !Chat.busy
                        onAccepted: page.submit()
                    }

                    MButton {
                        id: sendButton
                        text: qsTr("发送")
                        variant: "primary"
                        enabled: !Chat.busy && composerField.text.trim().length > 0
                        onClicked: page.submit()
                    }

                    MButton {
                        id: cancelButton
                        text: qsTr("中断")
                        visible: Chat.busy
                        onClicked: Chat.cancel()
                    }
                }

                Row {
                    width: parent.width
                    spacing: Theme.s2

                    MLabel {
                        label: qsTr("附图")
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    MField {
                        id: imageField
                        width: 280
                        height: 28
                        leftPadding: 10
                        rightPadding: 10
                        topPadding: 4
                        bottomPadding: 4
                        font.pixelSize: Theme.fSmall
                        placeholderText: Chat.supportsVision
                                         ? qsTr("图片路径（可选）")
                                         : qsTr("当前模型不支持图片输入")
                        enabled: !Chat.busy && Chat.supportsVision
                    }

                    Text {
                        visible: Chat.supportsVision
                        width: parent.width - 280 - Theme.s3 - 40
                        elide: Text.ElideRight
                        text: Chat.imagesConfirmed
                              ? qsTr("已确认：该图片会被上传")
                              : qsTr("未确认：该图片不会被上传")
                        color: Chat.imagesConfirmed ? Theme.warn : Theme.textGhost
                        font.pixelSize: Theme.fSmall
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        visible: !Chat.supportsVision
                        width: parent.width - 280 - Theme.s3 - 40
                        elide: Text.ElideRight
                        text: qsTr("该模型未声明视觉能力，附图入口已关闭")
                        color: Theme.textGhost
                        font.pixelSize: Theme.fSmall
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------- 消息流
    ListView {
        id: transcriptView
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: connCard.bottom
        anchors.bottom: bottomArea.top
        // 顶部不额外留白：滚动时消息直接裁在 Dock 下沿，避免出现空白带
        anchors.topMargin: page.configOpen ? Theme.s4 : 0
        anchors.bottomMargin: Theme.s4
        clip: true
        spacing: 0
        boundsBehavior: Flickable.StopAtBounds
        model: History

        onCountChanged: positionViewAtEnd()

        ScrollBar.vertical: ScrollBar {
            id: vScroll
            policy: ScrollBar.AsNeeded
            width: 8
            background: Item { }

            contentItem: Rectangle {
                implicitWidth: 3
                radius: 2
                color: vScroll.pressed ? Theme.accent : Theme.hairlineStrong
                opacity: vScroll.active ? 0.9 : 0.35
            }
        }

        delegate: Item {
            id: wrapper
            required property string role
            required property string content
            required property string status
            required property int messageId
            required property var totalTokens
            required property var elapsedMs

            width: ListView.view ? ListView.view.width : 0
            height: bubble.height + (actions.visible ? actions.height + 6 : 0) + Theme.s4

            readonly property bool isUser: wrapper.role === "user"
            readonly property bool thinking: wrapper.status === "streaming" && wrapper.content.length === 0
            readonly property bool completeAssistant: !wrapper.isUser && wrapper.status === "complete"
            readonly property real maxWidth: page.bubbleMaxWidth

            /// 用户消息纯文本；助手消息由 M5 转成 RichText HTML，QML 内不解析 Markdown
            readonly property int bodyFormat: (wrapper.isUser || wrapper.thinking) ? Text.PlainText
                                                                                   : Text.RichText
            readonly property string displayText: wrapper.thinking
                                                  ? qsTr("正在思考…")
                                                  : (wrapper.isUser
                                                     ? wrapper.content
                                                     : page.markdownHtml(wrapper.content))

            /// 复制成功的短暂反馈
            property bool copied: false
            /// 流式时竖线呼吸
            property real pulse: 1.0

            SequentialAnimation on pulse {
                running: wrapper.status === "streaming"
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 0.3; duration: 620; easing.type: Easing.InOutQuad }
                NumberAnimation { from: 0.3; to: 1.0; duration: 620; easing.type: Easing.InOutQuad }
            }

            Timer {
                id: copyTimer
                interval: 1500
                onTriggered: wrapper.copied = false
            }

            // 自然宽度测量：与正文同字体、同 HTML，但禁止换行；
            // 富文本不能用“单行文本宽度”估高，宽度只用于决定气泡横向尺寸。
            Text {
                id: measureText
                visible: false
                textFormat: wrapper.bodyFormat
                text: wrapper.displayText
                font.pixelSize: Theme.fBody
                wrapMode: Text.NoWrap
            }

            Rectangle {
                id: bubble
                readonly property int hPad: 16
                readonly property int vPad: 12
                readonly property int railWidth: wrapper.isUser ? 0 : 10

                width: Math.min(wrapper.maxWidth,
                                Math.ceil(measureText.implicitWidth) + bubble.hPad * 2 + bubble.railWidth + 2)
                height: roleTag.implicitHeight + 6 + bodyText.implicitHeight + bubble.vPad * 2
                anchors.top: parent.top
                anchors.left: wrapper.isUser ? undefined : parent.left
                anchors.right: wrapper.isUser ? parent.right : undefined
                radius: Theme.rLg
                color: wrapper.isUser ? Theme.ink3 : Theme.overlaySubtle
                border.width: wrapper.isUser ? 1 : 0
                border.color: Theme.hairline

                // 助手侧：一道琥珀竖线，作为"AI 的声音"的视觉锚；流式时呼吸
                Rectangle {
                    visible: !wrapper.isUser
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.topMargin: 6
                    anchors.bottomMargin: 6
                    width: 2
                    radius: 1
                    color: Theme.accent
                    opacity: wrapper.status === "streaming" ? wrapper.pulse : 1.0
                }

                MLabel {
                    id: roleTag
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: bubble.hPad + bubble.railWidth
                    anchors.topMargin: bubble.vPad
                    label: wrapper.isUser ? qsTr("你") : qsTr("MeowAi")
                    tone: wrapper.isUser ? Theme.textGhost
                                         : (wrapper.status === "error" ? Theme.danger : Theme.accent)
                }

                Text {
                    id: bodyText
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: roleTag.bottom
                    anchors.leftMargin: bubble.hPad + bubble.railWidth
                    anchors.rightMargin: bubble.hPad
                    anchors.topMargin: 6
                    textFormat: wrapper.bodyFormat
                    text: wrapper.displayText
                    color: wrapper.thinking ? Theme.textFaint
                                            : (wrapper.status === "error" ? Theme.danger : Theme.text)
                    linkColor: Theme.accentHi
                    font.pixelSize: Theme.fBody
                    lineHeight: 1.55
                    wrapMode: Text.Wrap

                    onLinkActivated: function (link) {
                        Qt.openUrlExternally(link)
                    }
                }
            }

            // 回答操作条：只给已完成（complete）的助手消息；流式与错误消息不显示。
            // 四个动作均为 14×14 自绘线条图标（矩形 / 旋转矩形），不依赖图标字体。
            Flow {
                id: actions
                visible: wrapper.completeAssistant
                anchors.left: bubble.left
                anchors.top: bubble.bottom
                anchors.topMargin: 6
                width: wrapper.width
                height: implicitHeight
                spacing: Theme.s2

                // 复制：两个错位矩形
                Item {
                    id: copyAction
                    width: 14
                    height: 14

                    readonly property color iconColor: wrapper.copied ? Theme.accentHi
                                                                      : (copyHover.hovered ? Theme.text : Theme.textFaint)

                    Rectangle {
                        x: 1; y: 4; width: 7; height: 9
                        color: "transparent"
                        border.width: 1.2
                        border.color: copyAction.iconColor
                    }
                    Rectangle {
                        x: 5; y: 1; width: 8; height: 9
                        color: "transparent"
                        border.width: 1.2
                        border.color: copyAction.iconColor
                    }

                    HoverHandler { id: copyHover }

                    TapHandler {
                        cursorShape: Qt.PointingHandCursor
                        onTapped: {
                            page.copyText(wrapper.content)
                            wrapper.copied = true
                            copyTimer.restart()
                        }
                    }

                    ToolTip {
                        id: copyTip
                        delay: 600
                        visible: copyHover.hovered
                        text: wrapper.copied ? qsTr("已复制") : qsTr("复制")
                        x: Math.round((copyAction.width - width) / 2)
                        y: -height - 6
                        padding: 6
                        background: Rectangle {
                            radius: Theme.rSm
                            color: Theme.ink3
                            border.width: 1
                            border.color: Theme.hairlineStrong
                        }
                        contentItem: Text {
                            text: copyTip.text
                            color: Theme.text
                            font.pixelSize: Theme.fLabel
                        }
                    }
                }

                // 重新生成：圆环 + 右上箭头
                Item {
                    id: regenAction
                    width: 14
                    height: 14
                    opacity: Chat.busy ? 0.4 : 1.0

                    readonly property color iconColor: regenHover.hovered && !Chat.busy ? Theme.text
                                                                                        : Theme.textFaint

                    Rectangle {
                        x: 2.5; y: 2.5; width: 9; height: 9; radius: 4.5
                        color: "transparent"
                        border.width: 1.2
                        border.color: regenAction.iconColor
                    }
                    Rectangle {
                        x: 8.4; y: 1; width: 3.4; height: 1.2
                        color: regenAction.iconColor
                    }
                    Rectangle {
                        x: 10.4; y: 0
                        width: 1.2; height: 2.2
                        color: regenAction.iconColor
                        rotation: 45
                        transformOrigin: Item.Center
                    }
                    Rectangle {
                        x: 10.4; y: 1.9
                        width: 1.2; height: 2.2
                        color: regenAction.iconColor
                        rotation: -45
                        transformOrigin: Item.Center
                    }

                    HoverHandler { id: regenHover }

                    TapHandler {
                        enabled: !Chat.busy
                        cursorShape: Qt.PointingHandCursor
                        onTapped: Chat.regenerateLast()
                    }

                    ToolTip {
                        id: regenTip
                        delay: 600
                        visible: regenHover.hovered
                        text: qsTr("重新生成")
                        x: Math.round((regenAction.width - width) / 2)
                        y: -height - 6
                        padding: 6
                        background: Rectangle {
                            radius: Theme.rSm
                            color: Theme.ink3
                            border.width: 1
                            border.color: Theme.hairlineStrong
                        }
                        contentItem: Text {
                            text: regenTip.text
                            color: Theme.text
                            font.pixelSize: Theme.fLabel
                        }
                    }
                }

                // 分享文字：方框 + 字母形（横杠 + 中竖）
                Item {
                    id: shareTextAction
                    width: 14
                    height: 14

                    readonly property color iconColor: shareTextHover.hovered ? Theme.text : Theme.textFaint

                    Rectangle {
                        x: 1.5; y: 3; width: 11; height: 9
                        color: "transparent"
                        border.width: 1.2
                        border.color: shareTextAction.iconColor
                    }
                    Rectangle {
                        x: 4.4; y: 5.6; width: 5; height: 1.1
                        color: shareTextAction.iconColor
                    }
                    Rectangle {
                        x: 6.5; y: 5.6; width: 1.1; height: 3.6
                        color: shareTextAction.iconColor
                    }

                    HoverHandler { id: shareTextHover }

                    TapHandler {
                        cursorShape: Qt.PointingHandCursor
                        onTapped: {
                            page.copyText(wrapper.content)
                            wrapper.copied = true
                            copyTimer.restart()
                        }
                    }

                    ToolTip {
                        id: shareTextTip
                        delay: 600
                        visible: shareTextHover.hovered
                        text: qsTr("分享文字")
                        x: Math.round((shareTextAction.width - width) / 2)
                        y: -height - 6
                        padding: 6
                        background: Rectangle {
                            radius: Theme.rSm
                            color: Theme.ink3
                            border.width: 1
                            border.color: Theme.hairlineStrong
                        }
                        contentItem: Text {
                            text: shareTextTip.text
                            color: Theme.text
                            font.pixelSize: Theme.fLabel
                        }
                    }
                }

                // 分享图片：方框 + 山形（两根旋转矩形拼成山峰，加一枚方形太阳）
                Item {
                    id: shareImageAction
                    width: 14
                    height: 14

                    readonly property color iconColor: shareImageHover.hovered ? Theme.text : Theme.textFaint

                    Rectangle {
                        x: 1.5; y: 3; width: 11; height: 9
                        color: "transparent"
                        border.width: 1.2
                        border.color: shareImageAction.iconColor
                    }
                    Rectangle {
                        x: 3.8; y: 5.25
                        width: 1.1; height: 4.5
                        color: shareImageAction.iconColor
                        rotation: 45
                        transformOrigin: Item.Center
                    }
                    Rectangle {
                        x: 7.1; y: 5.25
                        width: 1.1; height: 4.5
                        color: shareImageAction.iconColor
                        rotation: -45
                        transformOrigin: Item.Center
                    }
                    Rectangle {
                        x: 3.6; y: 4.6; width: 1.8; height: 1.8
                        color: shareImageAction.iconColor
                    }

                    HoverHandler { id: shareImageHover }

                    TapHandler {
                        cursorShape: Qt.PointingHandCursor
                        onTapped: page.shareImage(bubble)
                    }

                    ToolTip {
                        id: shareImageTip
                        delay: 600
                        visible: shareImageHover.hovered
                        text: qsTr("分享图片")
                        x: Math.round((shareImageAction.width - width) / 2)
                        y: -height - 6
                        padding: 6
                        background: Rectangle {
                            radius: Theme.rSm
                            color: Theme.ink3
                            border.width: 1
                            border.color: Theme.hairlineStrong
                        }
                        contentItem: Text {
                            text: shareImageTip.text
                            color: Theme.text
                            font.pixelSize: Theme.fLabel
                        }
                    }
                }

                // 图标组与文本之间留一段间距（不用「·」）
                Rectangle {
                    width: Theme.s2
                    height: 1
                    color: "transparent"
                }

                Text {
                    text: qsTr("Token %1").arg(wrapper.totalTokens < 0 ? "—"
                                                                      : wrapper.totalTokens)
                    color: Theme.textGhost
                    font.pixelSize: Theme.fSmall
                }

                Text {
                    text: "·"
                    color: Theme.textGhost
                    font.pixelSize: Theme.fSmall
                }

                Text {
                    text: qsTr("耗时 %1").arg(wrapper.elapsedMs < 0
                                              ? "—"
                                              : qsTr("%1 秒").arg((wrapper.elapsedMs / 1000).toFixed(1)))
                    color: Theme.textGhost
                    font.pixelSize: Theme.fSmall
                }
            }
        }
    }

    // 外部源变化时回同步（避免用户操作打断绑定后状态漂移）
    Connections {
        target: Chat

        function onProviderChanged() {
            providerBox.currentIndex = page.providerIds.indexOf(Chat.providerId)
            baseUrlField.text = Chat.customBaseUrl
            customModelField.text = Chat.modelId
            page.refreshSelection()
        }
        function onModelChanged() {
            modelBox.currentIndex = page.modelIds.indexOf(Chat.modelId)
            customModelField.text = Chat.modelId
            page.refreshAdvanced()
            if (!Chat.supportsVision)
                imageField.text = ""
        }
        function onThinkingChanged() {
            page.refreshAdvanced()
        }
        function onApiKeyStateChanged() {
            if (!Chat.dataStoreAvailable)
                keyField.text = ""
        }
        function onImagesConfirmedChanged() {
            imageConfirmSwitch.checked = Chat.imagesConfirmed
        }
        function onModelListChanged() {
            page.refreshModels()
        }
    }

    // 一轮结束 / 流式增量：把视图贴到最新
    Connections {
        target: Chat

        function onBusyChanged() {
            transcriptView.positionViewAtEnd()
        }

        function onReplyTextChanged() {
            if (Chat.busy)
                transcriptView.positionViewAtEnd()
        }
    }

    // 空态
    Item {
        anchors.fill: transcriptView
        visible: transcriptView.count === 0 && !Chat.busy
        enabled: false

        BrandLogo {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: -48
            width: 56
            height: 56
            markOnly: true
            opacity: 0.4
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: 4
            text: qsTr("还没有对话")
            color: Theme.textDim
            font.pixelSize: Theme.fH2
            font.weight: Font.Medium
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: 32
            text: Chat.hasApiKey
                  ? qsTr("在下方输入一句话，开始与本地助理交谈")
                  : qsTr("先在「配置」里填入 API Key")
            color: Theme.textGhost
            font.pixelSize: Theme.fSmall
        }
    }

    // 剪贴板落点：隐藏的 TextEdit 提供 copy()，不在 QML 里直连平台剪贴板
    TextEdit {
        id: clipHelper
        visible: false
        width: 0
        height: 0
        textFormat: TextEdit.PlainText
    }

    // 分享图片：保存对话框；确认后把 grabToImage 的结果写成 PNG
    FileDialog {
        id: saveImageDialog
        title: qsTr("保存消息图片")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: [qsTr("PNG 图片 (*.png)")]

        onAccepted: {
            if (page.pendingMessageImage) {
                page.pendingMessageImage.saveToFile(page.localPath(saveImageDialog.selectedFile))
                page.pendingMessageImage = null
            }
        }
        onRejected: page.pendingMessageImage = null
    }

    // 资料库首次启用 / 内容外发同意：只在用户点「挂载」且尚未同意时出现
    LibraryConsent {
        id: libraryConsentDialog
        anchors.fill: parent
        onAccepted: {
            Library.grantConsent()
            if (page.pendingMountId > 0) {
                Library.setMounted(page.pendingMountId, true)
                page.pendingMountId = 0
            }
        }
        onRejected: page.pendingMountId = 0
    }
}
