import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MeowAi

/**
 * 资料库配置抽屉内容（v0.2.6 · M3）。
 *
 * 外壳契约（main.qml 依赖，保持不变）：
 *  - property bool open
 *  - signal closeRequested()
 *
 * 内容：检索与隐私、检索参数、参考资料模式、内容向量化（embedding）、
 * 图片描述（caption）、成本预估、库内搜索。
 * 所有服务动作经 `Library` 门面；本文件不拼 HTTP、不读 SecureStore、不打印 Key。
 * 保存成功与「可用性校验」是两条独立信息：保存只代表落盘，不代表服务可用。
 */
Item {
    id: root

    property bool open: false
    signal closeRequested()

    visible: open

    // ------------------------------------------------------------ 厂商数据
    property var embedProviders: Library.embeddingProviderOptions()
    property var captionProviders: Library.captionProviderOptions()

    readonly property var embedProvider: (embedCombo.currentIndex >= 0
                                          && embedCombo.currentIndex < embedProviders.length)
                                         ? embedProviders[embedCombo.currentIndex] : null
    readonly property var captionProvider: (captionCombo.currentIndex >= 0
                                            && captionCombo.currentIndex < captionProviders.length)
                                           ? captionProviders[captionCombo.currentIndex] : null

    // 「自定义（OpenAI 兼容）」默认展开服务地址；其它厂商可通过「高级」手写
    property bool embedAdvanced: false
    property bool captionAdvanced: false
    readonly property bool embedBaseUrlVisible: providerIsCustom(embedProvider) || embedAdvanced
    readonly property bool captionBaseUrlVisible: providerIsCustom(captionProvider) || captionAdvanced

    // ------------------------------------------------------------ 拉取模型
    property var embeddingCandidates: []
    property var captionCandidates: []
    property bool embeddingFetching: false
    property bool captionFetching: false
    property string embeddingFetchMessage: ""
    property string captionFetchMessage: ""
    property bool embeddingFetchOk: false
    property bool captionFetchOk: false

    // ------------------------------------------------------------ 保存反馈
    property string embedSaveFeedback: ""
    property bool embedSaveOk: true
    property string embedKeyFeedback: ""
    property bool embedKeyOk: true
    property bool embedKeyJustSaved: false
    property string captionSaveFeedback: ""
    property bool captionSaveOk: true
    property string captionKeyFeedback: ""
    property bool captionKeyOk: true
    property bool captionKeyJustSaved: false

    // ------------------------------------------------------------ 校验状态
    readonly property string embedValidationState: Library.embeddingValidationStatus
    readonly property string captionValidationState: Library.captionValidationStatus

    function indexOfProvider(list, id) {
        for (let i = 0; i < list.length; ++i) {
            if (list[i].id === id)
                return i
        }
        return -1
    }

    function providerNames(list) {
        const names = []
        for (let i = 0; i < list.length; ++i)
            names.push(list[i].displayName)
        return names
    }

    function providerIsCustom(provider) {
        if (!provider)
            return false
        if (provider.id === "custom")
            return true
        return String(provider.displayName).indexOf("自定义") >= 0
    }

    function validationText(status, keyJustSaved) {
        if (keyJustSaved && status !== "checking")
            return qsTr("未校验（密钥刚更新）")
        switch (status) {
        case "checking":     return qsTr("校验中…")
        case "valid":        return qsTr("可用")
        case "invalid":      return qsTr("无效")
        case "unverifiable": return qsTr("无法判定")
        default:             return qsTr("未校验")
        }
    }

    /// 拉取失败文案：区分「服务没有 /models」与其它失败原因
    function fetchFailureText(error) {
        if (!error || error.length === 0)
            return qsTr("该服务未提供模型列表，请手动填写")
        if (error.indexOf("/models") >= 0 || error.indexOf("解析出模型清单") >= 0)
            return qsTr("该服务未提供模型列表，请手动填写（原因：%1）").arg(error)
        return qsTr("拉取失败：%1").arg(error)
    }

    function validationColor(status, keyJustSaved) {
        if (keyJustSaved && status !== "checking")
            return Theme.warn
        switch (status) {
        case "checking":     return Theme.accent
        case "valid":        return Theme.ok
        case "invalid":      return Theme.danger
        case "unverifiable": return Theme.warn
        default:             return Theme.textFaint
        }
    }

    // ------------------------------------------------------------ embedding
    function saveEmbedding() {
        const provider = root.embedProvider
        if (!provider) {
            root.embedSaveOk = false
            root.embedSaveFeedback = qsTr("保存失败：请先选择 embedding 厂商")
            return
        }
        const baseUrl = root.embedBaseUrlVisible ? embedBaseUrl.text.trim() : ""
        const ok = Library.saveEmbeddingConfig(provider.id, embedModel.text, 0, baseUrl)
        root.embedSaveOk = ok
        root.embedSaveFeedback = ok
                ? qsTr("配置已保存。可用性尚未校验，请点「验证可用性」。")
                : qsTr("配置保存失败：%1").arg(Library.message)
        root.embedKeyJustSaved = false
    }

    function saveEmbeddingKey() {
        const ok = Library.saveEmbeddingKey(embedKey.text)
        root.embedKeyOk = ok
        if (ok) {
            embedKey.text = ""
            root.embedKeyJustSaved = true
            root.embedKeyFeedback = qsTr("密钥已保存到本机加密保险库。可用性尚未校验，请点「验证可用性」。")
        } else {
            root.embedKeyFeedback = Library.message.length > 0
                    ? Library.message
                    : qsTr("密钥保存失败")
        }
    }

    function clearEmbeddingBaseUrl() {
        const ok = Library.clearEmbeddingBaseUrl()
        root.embedSaveOk = ok
        root.embedSaveFeedback = ok
                ? qsTr("已清除本服务地址：回退到对话自定义地址或厂商默认地址。")
                : qsTr("清除失败：%1").arg(Library.message)
        if (ok)
            embedBaseUrl.text = ""
    }

    function fetchEmbeddingModels() {
        root.embeddingFetching = true
        root.embeddingFetchMessage = qsTr("正在拉取模型列表…")
        root.embeddingFetchOk = false
        Library.fetchEmbeddingModels()
    }

    function validateEmbedding() {
        root.embedKeyJustSaved = false
        Library.validateEmbeddingConfig()
    }

    // -------------------------------------------------------------- caption
    function saveCaption() {
        const provider = root.captionProvider
        if (!provider) {
            root.captionSaveOk = false
            root.captionSaveFeedback = qsTr("保存失败：请先选择视觉模型厂商")
            return
        }
        const baseUrl = root.captionBaseUrlVisible ? captionBaseUrl.text.trim() : ""
        const ok = Library.saveCaptionConfig(provider.id, captionModel.text, baseUrl)
        root.captionSaveOk = ok
        root.captionSaveFeedback = ok
                ? qsTr("配置已保存。可用性尚未校验，请点「验证可用性」。")
                : qsTr("配置保存失败：%1").arg(Library.message)
        root.captionKeyJustSaved = false
    }

    function saveCaptionKey() {
        const ok = Library.saveCaptionKey(captionKey.text)
        root.captionKeyOk = ok
        if (ok) {
            captionKey.text = ""
            root.captionKeyJustSaved = true
            root.captionKeyFeedback = qsTr("密钥已保存到本机加密保险库。可用性尚未校验，请点「验证可用性」。")
        } else {
            root.captionKeyFeedback = Library.message.length > 0
                    ? Library.message
                    : qsTr("密钥保存失败")
        }
    }

    function clearCaptionBaseUrl() {
        const ok = Library.clearCaptionBaseUrl()
        root.captionSaveOk = ok
        root.captionSaveFeedback = ok
                ? qsTr("已清除本服务地址：回退到对话自定义地址或厂商默认地址。")
                : qsTr("清除失败：%1").arg(Library.message)
        if (ok)
            captionBaseUrl.text = ""
    }

    function fetchCaptionModels() {
        root.captionFetching = true
        root.captionFetchMessage = qsTr("正在拉取模型列表…")
        root.captionFetchOk = false
        Library.fetchCaptionModels()
    }

    function validateCaption() {
        root.captionKeyJustSaved = false
        Library.validateCaptionConfig()
    }

    Connections {
        target: Library

        function onEmbeddingModelsFetched(ids, error) {
            root.embeddingFetching = false
            if (ids && ids.length > 0) {
                root.embeddingCandidates = ids
                root.embeddingFetchOk = true
                root.embeddingFetchMessage = qsTr("已拉取 %1 个模型：从下拉中选择，再点保存生效。")
                                                    .arg(ids.length)
            } else {
                root.embeddingCandidates = []
                root.embeddingFetchOk = false
                root.embeddingFetchMessage = root.fetchFailureText(error)
            }
        }

        function onCaptionModelsFetched(ids, error) {
            root.captionFetching = false
            if (ids && ids.length > 0) {
                root.captionCandidates = ids
                root.captionFetchOk = true
                root.captionFetchMessage = qsTr("已拉取 %1 个模型：从下拉中选择，再点保存生效。")
                                                .arg(ids.length)
            } else {
                root.captionCandidates = []
                root.captionFetchOk = false
                root.captionFetchMessage = root.fetchFailureText(error)
            }
        }
    }

    // ------------------------------------------------------------ 可编辑下拉
    component ModelPicker: RowLayout {
        id: picker

        /// 初始值 / 外部建议值：用户一旦编辑或从下拉选择即断开绑定
        property string value: ""
        property alias text: field.text
        property string placeholderText: ""
        property var candidates: []

        signal picked()

        spacing: Theme.s1

        MField {
            id: field
            Layout.fillWidth: true
            placeholderText: picker.placeholderText
            text: picker.value
        }

        MButton {
            text: "\u25BE"
            variant: "ghost"
            small: true
            enabled: picker.candidates.length > 0
            onClicked: pickerMenu.open()
        }

        Popup {
            id: pickerMenu
            x: 0
            y: picker.height + 4
            width: picker.width
            padding: 4
            implicitHeight: Math.min(240, modelList.contentHeight + 8)

            contentItem: ListView {
                id: modelList
                clip: true
                implicitHeight: contentHeight
                model: pickerMenu.visible ? picker.candidates : null
                ScrollIndicator.vertical: ScrollIndicator { }

                delegate: ItemDelegate {
                    id: modelItem
                    required property var modelData
                    width: modelList.width - 8
                    height: 30
                    onClicked: {
                        picker.text = modelItem.modelData
                        picker.picked()
                        pickerMenu.close()
                    }

                    contentItem: Text {
                        text: modelItem.modelData
                        color: modelItem.hovered ? Theme.accentHi : Theme.text
                        font.pixelSize: Theme.fSmall
                        leftPadding: 8
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    background: Rectangle {
                        radius: Theme.rSm
                        color: modelItem.hovered ? Theme.accentGhost : "transparent"
                    }
                }
            }

            background: Rectangle {
                radius: Theme.rMd
                color: Theme.ink3
                border.width: 1
                border.color: Theme.hairlineStrong
            }
        }
    }

    // ---------------------------------------------------------------- 内容
    Flickable {
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: panelColumn.implicitHeight
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: panelColumn
            width: parent.width
            spacing: Theme.s3

            Text {
                width: parent.width
                text: qsTr("修改配置后请先保存，再验证可用性或拉取模型；校验与拉取都使用已保存的配置。")
                color: Theme.textGhost
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
            }

            Rectangle { width: parent.width; height: 1; color: Theme.hairline }

            // ======================================================= 检索与隐私
            MLabel { label: qsTr("检索与隐私") }

            Text {
                width: parent.width
                text: Library.consentGiven
                      ? qsTr("已同意内容外发（记录在本机）。撤回后不会再发送任何资料库内容。")
                      : qsTr("尚未同意：文件可本地入库，但不会建立向量、不会发给任何模型。")
                color: Library.consentGiven ? Theme.ok : Theme.warn
                font.pixelSize: Theme.fSmall
                wrapMode: Text.Wrap
            }

            Row {
                width: parent.width
                spacing: Theme.s2

                MButton {
                    text: Library.consentGiven ? qsTr("撤回同意") : qsTr("同意并启用")
                    variant: Library.consentGiven ? "danger" : "primary"
                    small: true
                    onClicked: {
                        if (Library.consentGiven)
                            Library.revokeConsent()
                        else
                            Library.requestEnable()
                    }
                }
                MButton {
                    text: Library.libraryEnabled ? qsTr("停用资料库") : qsTr("启用资料库")
                    variant: "ghost"
                    small: true
                    visible: Library.consentGiven
                    onClicked: Library.setEnabled(!Library.libraryEnabled)
                }
            }

            Text {
                width: parent.width
                text: qsTr("挂载到对话：%1 个小库").arg(Library.mountedCount)
                color: Theme.textDim
                font.pixelSize: Theme.fSmall
            }

            Rectangle { width: parent.width; height: 1; color: Theme.hairline }

            // ======================================================= 检索参数
            MLabel { label: qsTr("检索参数") }

            Text {
                width: parent.width
                text: qsTr("Top-K（最终注入片段数）")
                color: Theme.textFaint
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
            }
            MField {
                width: parent.width
                text: Library.topK
                validator: IntValidator { bottom: 1; top: 30 }
                onEditingFinished: {
                    if (text.length > 0)
                        Library.topK = parseInt(text)
                }
            }

            Text {
                width: parent.width
                text: qsTr("每文件片段上限")
                color: Theme.textFaint
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
            }
            MField {
                width: parent.width
                text: Library.perFileLimit
                validator: IntValidator { bottom: 1; top: 10 }
                onEditingFinished: {
                    if (text.length > 0)
                        Library.perFileLimit = parseInt(text)
                }
            }

            Text {
                width: parent.width
                text: qsTr("注入 token 预算")
                color: Theme.textFaint
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
            }
            MField {
                width: parent.width
                text: Library.tokenBudget
                validator: IntValidator { bottom: 200; top: 20000 }
                onEditingFinished: {
                    if (text.length > 0)
                        Library.tokenBudget = parseInt(text)
                }
            }

            Text {
                width: parent.width
                text: qsTr("相似度阈值（0–1）")
                color: Theme.textFaint
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
            }
            MField {
                width: parent.width
                text: Library.similarityThreshold
                validator: DoubleValidator { bottom: 0; top: 1 }
                onEditingFinished: {
                    if (text.length > 0)
                        Library.similarityThreshold = parseFloat(text)
                }
            }

            Rectangle { width: parent.width; height: 1; color: Theme.hairline }

            // ================================================ 参考资料模式
            MLabel { label: qsTr("参考资料模式（文件级注入）") }

            Text {
                width: parent.width
                text: qsTr("单文件预算（tokens）")
                color: Theme.textFaint
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
            }
            MField {
                width: parent.width
                text: Library.perFileBudget
                validator: IntValidator { bottom: 1000; top: 200000 }
                onEditingFinished: {
                    if (text.length > 0)
                        Library.perFileBudget = parseInt(text)
                }
            }

            Text {
                width: parent.width
                text: qsTr("全局预算（tokens，所有相关文件合计）")
                color: Theme.textFaint
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
            }
            MField {
                width: parent.width
                text: Library.globalBudget
                validator: IntValidator { bottom: 1000; top: 500000 }
                onEditingFinished: {
                    if (text.length > 0)
                        Library.globalBudget = parseInt(text)
                }
            }

            Text {
                width: parent.width
                text: qsTr("回答预留（tokens，超出上下文窗口时自动降级）")
                color: Theme.textFaint
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
            }
            MField {
                width: parent.width
                text: Library.contextReserve
                validator: IntValidator { bottom: 200; top: 32000 }
                onEditingFinished: {
                    if (text.length > 0)
                        Library.contextReserve = parseInt(text)
                }
            }

            Text {
                width: parent.width
                text: qsTr("未知窗口估算值（tokens；模型未提供上下文长度时使用）")
                color: Theme.textFaint
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
            }
            MField {
                width: parent.width
                text: Library.fallbackContextWindow
                validator: IntValidator { bottom: 2000; top: 4000000 }
                onEditingFinished: {
                    if (text.length > 0)
                        Library.fallbackContextWindow = parseInt(text)
                }
            }

            Text {
                width: parent.width
                text: qsTr("邻居扩展半径（前后各 N 段）")
                color: Theme.textFaint
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
            }
            MField {
                width: parent.width
                text: Library.neighborRadius
                validator: IntValidator { bottom: 0; top: 3 }
                onEditingFinished: {
                    if (text.length > 0)
                        Library.neighborRadius = parseInt(text)
                }
            }

            Row {
                width: parent.width
                spacing: Theme.s2

                Text {
                    width: parent.width - 56
                    text: qsTr("embedding 不可用时用关键词兜底")
                    color: Theme.textDim
                    font.pixelSize: Theme.fSmall
                    wrapMode: Text.Wrap
                }
                MSwitch {
                    checked: Library.ftsFallback
                    onToggled: Library.ftsFallback = checked
                }
            }

            Text {
                width: parent.width
                text: qsTr("切分尺寸（字符）")
                color: Theme.textFaint
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
            }
            MField {
                width: parent.width
                text: Library.chunkSize
                validator: IntValidator { bottom: 200; top: 4000 }
                onEditingFinished: {
                    if (text.length > 0)
                        Library.chunkSize = parseInt(text)
                }
            }

            Text {
                width: parent.width
                text: qsTr("片段重叠（字符）")
                color: Theme.textFaint
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
            }
            MField {
                width: parent.width
                text: Library.chunkOverlap
                validator: IntValidator { bottom: 0; top: 2000 }
                onEditingFinished: {
                    if (text.length > 0)
                        Library.chunkOverlap = parseInt(text)
                }
            }

            Rectangle { width: parent.width; height: 1; color: Theme.hairline }

            // ============================================ 内容向量化（embedding）
            MLabel { label: qsTr("内容向量化（embedding）") }

            MCombo {
                id: embedCombo
                width: parent.width
                model: root.providerNames(root.embedProviders)
                placeholderText: qsTr("选择厂商")
                currentIndex: root.indexOfProvider(root.embedProviders, Library.embeddingProviderId)
                onActivated: {
                    const provider = root.embedProviders[currentIndex]
                    if (provider)
                        embedModel.text = Library.suggestedEmbeddingModel(provider.id)
                }
            }

            RowLayout {
                width: parent.width
                spacing: Theme.s2

                ModelPicker {
                    id: embedModel
                    Layout.fillWidth: true
                    value: Library.embeddingModelId
                    placeholderText: qsTr("embedding 模型名")
                    candidates: root.embeddingCandidates
                    onPicked: root.embedSaveFeedback = qsTr("已选择模型，仍需点「保存 embedding 配置」生效。")
                }

                MButton {
                    text: root.embeddingFetching ? qsTr("拉取中…") : qsTr("拉取模型")
                    variant: "ghost"
                    small: true
                    enabled: !root.embeddingFetching
                    onClicked: root.fetchEmbeddingModels()
                }
            }

            Text {
                width: parent.width
                visible: root.embeddingFetchMessage.length > 0
                text: root.embeddingFetchMessage
                color: root.embeddingFetchOk ? Theme.ok : Theme.warn
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
            }

            Row {
                width: parent.width
                spacing: Theme.s2

                MButton {
                    text: root.embedAdvanced ? qsTr("收起高级") : qsTr("高级")
                    variant: "quiet"
                    small: true
                    onClicked: root.embedAdvanced = !root.embedAdvanced
                }
                Text {
                    width: parent.width - 80
                    text: root.embedBaseUrlVisible
                          ? qsTr("可填写本服务专用地址")
                          : qsTr("自定义厂商会自动展开服务地址")
                    color: Theme.textGhost
                    font.pixelSize: Theme.fLabel
                    wrapMode: Text.Wrap
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            Column {
                width: parent.width
                spacing: Theme.s1
                visible: root.embedBaseUrlVisible

                Text {
                    width: parent.width
                    text: qsTr("服务地址（baseUrl）")
                    color: Theme.textFaint
                    font.pixelSize: Theme.fLabel
                    wrapMode: Text.Wrap
                }
                MField {
                    id: embedBaseUrl
                    width: parent.width
                    placeholderText: qsTr("https://...（留空沿用已保存地址或对话地址）")
                }
                Text {
                    width: parent.width
                    text: qsTr("留空保存不会覆盖已存在的本服务地址；要清空已保存地址，请点「清除本服务地址」。")
                    color: Theme.textGhost
                    font.pixelSize: Theme.fLabel
                    wrapMode: Text.Wrap
                }
                MButton {
                    text: qsTr("清除本服务地址")
                    variant: "quiet"
                    small: true
                    onClicked: root.clearEmbeddingBaseUrl()
                }
            }

            Text {
                width: parent.width
                text: Library.embeddingStatus
                color: Library.embeddingReady ? Theme.ok : Theme.textFaint
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
            }

            MButton {
                width: parent.width
                text: qsTr("保存 embedding 配置")
                variant: "ghost"
                small: true
                enabled: embedCombo.currentIndex >= 0 && embedModel.text.trim().length > 0
                onClicked: root.saveEmbedding()
            }

            Text {
                width: parent.width
                visible: root.embedSaveFeedback.length > 0
                text: root.embedSaveFeedback
                color: root.embedSaveOk ? Theme.ok : Theme.danger
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
            }

            MField {
                id: embedKey
                width: parent.width
                echoMode: TextInput.Password
                // 依赖 embeddingStatus 以在保存 Key 后刷新占位提示
                placeholderText: Library.embeddingStatus !== undefined && Library.hasEmbeddingKey()
                                 ? qsTr("已保存（留空不修改）")
                                 : qsTr("单独填写 embedding API Key（可选）")
            }

            MButton {
                width: parent.width
                text: qsTr("保存 embedding Key")
                variant: "quiet"
                small: true
                enabled: embedKey.text.trim().length > 0
                onClicked: root.saveEmbeddingKey()
            }

            Text {
                width: parent.width
                visible: root.embedKeyFeedback.length > 0
                text: root.embedKeyFeedback
                color: root.embedKeyOk ? Theme.ok : Theme.danger
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
            }

            RowLayout {
                width: parent.width
                spacing: Theme.s2

                MButton {
                    text: root.embedValidationState === "checking"
                          ? qsTr("验证中…")
                          : qsTr("验证可用性")
                    variant: "ghost"
                    small: true
                    enabled: root.embedValidationState !== "checking"
                             && embedCombo.currentIndex >= 0
                    onClicked: root.validateEmbedding()
                }

                Text {
                    Layout.fillWidth: true
                    text: root.validationText(root.embedValidationState, root.embedKeyJustSaved)
                    color: root.validationColor(root.embedValidationState, root.embedKeyJustSaved)
                    font.pixelSize: Theme.fLabel
                    wrapMode: Text.Wrap
                }
            }

            Text {
                width: parent.width
                visible: Library.embeddingValidationMessage.length > 0
                text: Library.embeddingValidationMessage
                color: root.validationColor(root.embedValidationState, false)
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
                lineHeight: 1.35
            }

            Text {
                width: parent.width
                visible: root.embedValidationState === "idle" && !root.embedKeyJustSaved
                text: qsTr("尚未校验：点「验证可用性」会向服务发送一次最小请求（可能产生极小费用）。保存配置或更换 Key 后需重新校验。")
                color: Theme.textGhost
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
            }

            Rectangle { width: parent.width; height: 1; color: Theme.hairline }

            // ================================================= 图片描述（caption）
            MLabel { label: qsTr("图片描述（caption）") }

            MCombo {
                id: captionCombo
                width: parent.width
                model: root.providerNames(root.captionProviders)
                placeholderText: qsTr("选择视觉模型厂商")
                currentIndex: root.indexOfProvider(root.captionProviders, Library.captionProviderId)
                onActivated: {
                    const provider = root.captionProviders[currentIndex]
                    if (provider)
                        captionModel.text = Library.suggestedCaptionModel(provider.id)
                }
            }

            RowLayout {
                width: parent.width
                spacing: Theme.s2

                ModelPicker {
                    id: captionModel
                    Layout.fillWidth: true
                    value: Library.captionModelId
                    placeholderText: qsTr("视觉模型名")
                    candidates: root.captionCandidates
                    onPicked: root.captionSaveFeedback = qsTr("已选择模型，仍需点「保存图片描述配置」生效。")
                }

                MButton {
                    text: root.captionFetching ? qsTr("拉取中…") : qsTr("拉取模型")
                    variant: "ghost"
                    small: true
                    enabled: !root.captionFetching
                    onClicked: root.fetchCaptionModels()
                }
            }

            Text {
                width: parent.width
                visible: root.captionFetchMessage.length > 0
                text: root.captionFetchMessage
                color: root.captionFetchOk ? Theme.ok : Theme.warn
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
            }

            Row {
                width: parent.width
                spacing: Theme.s2

                MButton {
                    text: root.captionAdvanced ? qsTr("收起高级") : qsTr("高级")
                    variant: "quiet"
                    small: true
                    onClicked: root.captionAdvanced = !root.captionAdvanced
                }
                Text {
                    width: parent.width - 80
                    text: root.captionBaseUrlVisible
                          ? qsTr("可填写本服务专用地址")
                          : qsTr("自定义厂商会自动展开服务地址")
                    color: Theme.textGhost
                    font.pixelSize: Theme.fLabel
                    wrapMode: Text.Wrap
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            Column {
                width: parent.width
                spacing: Theme.s1
                visible: root.captionBaseUrlVisible

                Text {
                    width: parent.width
                    text: qsTr("服务地址（baseUrl）")
                    color: Theme.textFaint
                    font.pixelSize: Theme.fLabel
                    wrapMode: Text.Wrap
                }
                MField {
                    id: captionBaseUrl
                    width: parent.width
                    placeholderText: qsTr("https://...（留空沿用已保存地址或对话地址）")
                }
                Text {
                    width: parent.width
                    text: qsTr("留空保存不会覆盖已存在的本服务地址；要清空已保存地址，请点「清除本服务地址」。")
                    color: Theme.textGhost
                    font.pixelSize: Theme.fLabel
                    wrapMode: Text.Wrap
                }
                MButton {
                    text: qsTr("清除本服务地址")
                    variant: "quiet"
                    small: true
                    onClicked: root.clearCaptionBaseUrl()
                }
            }

            Text {
                width: parent.width
                text: Library.captionStatus
                color: Library.captionReady ? Theme.ok : Theme.textFaint
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
            }

            MButton {
                width: parent.width
                text: qsTr("保存图片描述配置")
                variant: "ghost"
                small: true
                enabled: captionCombo.currentIndex >= 0 && captionModel.text.trim().length > 0
                onClicked: root.saveCaption()
            }

            Text {
                width: parent.width
                visible: root.captionSaveFeedback.length > 0
                text: root.captionSaveFeedback
                color: root.captionSaveOk ? Theme.ok : Theme.danger
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
            }

            MField {
                id: captionKey
                width: parent.width
                echoMode: TextInput.Password
                // 依赖 captionStatus 以在保存 Key 后刷新占位提示
                placeholderText: Library.captionStatus !== undefined && Library.hasCaptionKey()
                                 ? qsTr("已保存（留空不修改）")
                                 : qsTr("单独填写视觉 API Key（可选）")
            }

            MButton {
                width: parent.width
                text: qsTr("保存图片描述 Key")
                variant: "quiet"
                small: true
                enabled: captionKey.text.trim().length > 0
                onClicked: root.saveCaptionKey()
            }

            Text {
                width: parent.width
                visible: root.captionKeyFeedback.length > 0
                text: root.captionKeyFeedback
                color: root.captionKeyOk ? Theme.ok : Theme.danger
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
            }

            RowLayout {
                width: parent.width
                spacing: Theme.s2

                MButton {
                    text: root.captionValidationState === "checking"
                          ? qsTr("验证中…")
                          : qsTr("验证可用性")
                    variant: "ghost"
                    small: true
                    enabled: root.captionValidationState !== "checking"
                             && captionCombo.currentIndex >= 0
                    onClicked: root.validateCaption()
                }

                Text {
                    Layout.fillWidth: true
                    text: root.validationText(root.captionValidationState, root.captionKeyJustSaved)
                    color: root.validationColor(root.captionValidationState, root.captionKeyJustSaved)
                    font.pixelSize: Theme.fLabel
                    wrapMode: Text.Wrap
                }
            }

            Text {
                width: parent.width
                visible: Library.captionValidationMessage.length > 0
                text: Library.captionValidationMessage
                color: root.validationColor(root.captionValidationState, false)
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
                lineHeight: 1.35
            }

            Text {
                width: parent.width
                visible: root.captionValidationState === "idle" && !root.captionKeyJustSaved
                text: qsTr("尚未校验：点「验证可用性」会发送一张内置小图与短提示（可能产生极小费用），不受图片外发同意开关影响。")
                color: Theme.textGhost
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
            }

            Rectangle { width: parent.width; height: 1; color: Theme.hairline }

            // ======================================================= 成本预估
            MLabel { label: qsTr("成本预估（参考）") }

            Text {
                width: parent.width
                text: qsTr("文本 %1 token · 图片 %2 张")
                      .arg(Library.estimate.textTokens || 0)
                      .arg(Library.estimate.imageCount || 0)
                color: Theme.textDim
                font.pixelSize: Theme.fSmall
                wrapMode: Text.Wrap
            }
            Text {
                width: parent.width
                text: qsTr("首次建向量：%1（%2 token）")
                      .arg(Library.estimate.embeddingCost || "—")
                      .arg(Library.estimate.embeddingTokens || 0)
                color: Theme.textDim
                font.pixelSize: Theme.fSmall
                wrapMode: Text.Wrap
            }
            Text {
                width: parent.width
                visible: (Library.estimate.imageCount || 0) > 0
                text: qsTr("图片描述：%1（约 %2 token）")
                      .arg(Library.estimate.visionCost || "—")
                      .arg(Library.estimate.visionTokens || 0)
                color: Theme.textDim
                font.pixelSize: Theme.fSmall
                wrapMode: Text.Wrap
            }
            Text {
                width: parent.width
                text: qsTr("每次对话注入上限：%1 token ≈ %2")
                      .arg(Library.estimate.contextTokens || 0)
                      .arg(Library.estimate.contextCost || "—")
                color: Theme.textDim
                font.pixelSize: Theme.fSmall
                wrapMode: Text.Wrap
            }
            Text {
                width: parent.width
                text: Library.estimate.note || ""
                color: Theme.textGhost
                font.pixelSize: Theme.fLabel
                wrapMode: Text.Wrap
            }

            Rectangle { width: parent.width; height: 1; color: Theme.hairline }

            // =================================================== 库内搜索（本地）
            MLabel { label: qsTr("库内搜索（本地关键词）") }

            MField {
                id: searchField
                width: parent.width
                placeholderText: qsTr("输入关键词后回车")
                onAccepted: Library.search(text)
                onTextChanged: {
                    if (text.length === 0)
                        Library.clearSearch()
                }
            }

            ListView {
                width: parent.width
                height: Math.min(180, contentHeight)
                clip: true
                spacing: Theme.s1
                model: Library.searchResults

                delegate: Column {
                    required property var modelData
                    width: ListView.view.width
                    spacing: 2

                    Text {
                        width: parent.width
                        text: modelData.fileName + " · " + modelData.locLabel
                        color: Theme.text
                        font.pixelSize: Theme.fLabel
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: modelData.snippet
                        color: Theme.textFaint
                        font.pixelSize: Theme.fLabel
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }
            }

            MButton {
                width: parent.width
                text: qsTr("关闭配置")
                variant: "ghost"
                small: true
                onClicked: root.closeRequested()
            }

            Item { width: 1; height: Theme.s4 }
        }
    }
}
