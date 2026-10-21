import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MeowAi

/**
 * 资料库页（v0.2.5 · M7）。
 *
 * 三栏：小库 → 文件与关联文件夹 → 检索 / 服务 / 隐私 / 成本。
 * 所有业务都经 `Library`（LibraryController）转发，本文件不直连数据库、不发网络请求。
 * 删除类操作一律二次确认；文案反复强调「用户原文件不会被删除」。
 */
Item {
    id: page

    property var embedProviders: Library.embeddingProviderOptions()
    property var captionProviders: Library.captionProviderOptions()
    property int pendingRemoval: 0
    property bool dropActive: false

    /// 当前小库里「等待前置条件」的文件数量，用于给出可操作的下一步提示
    readonly property int pendingImageCount: countFiles("pending", "image")
    readonly property int pendingEmbeddingCount: countFiles("needs_embedding", "")

    function countFiles(status, kind) {
        let count = 0
        const items = Library.files
        for (let i = 0; i < items.length; ++i) {
            if (status.length > 0 && items[i].status !== status)
                continue
            if (kind.length > 0 && items[i].kind !== kind)
                continue
            ++count
        }
        return count
    }

    function scrollToItem(item) {
        if (!item)
            return
        settingsFlick.contentY = Math.max(0, item.y - Theme.s2)
    }

    /// 把 url / url 列表统一转成字符串路径列表，避免 QML 序列类型直接透传给 C++
    function toPathList(urls) {
        const paths = []
        for (let i = 0; i < urls.length; ++i) {
            const raw = urls[i].toString()
            if (raw.length > 0)
                paths.push(raw)
        }
        return paths
    }

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

    Connections {
        target: Library
        function onConsentRequired() { consentDialog.open = true }
    }

    // ---------------------------------------------------------------- 三栏
    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.s5
        spacing: Theme.s4

        // ============================================================ 小库
        MSurface {
            Layout.preferredWidth: 220
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.s4
                spacing: Theme.s2

                MLabel { label: "小库" }

                MField {
                    id: newCollectionName
                    Layout.fillWidth: true
                    placeholderText: qsTr("新建小库名称")
                    onAccepted: createButton.clicked()
                }

                MButton {
                    id: createButton
                    Layout.fillWidth: true
                    text: qsTr("+ 新建小库")
                    variant: "ghost"
                    small: true
                    enabled: newCollectionName.text.trim().length > 0
                    onClicked: {
                        Library.createCollection(newCollectionName.text)
                        newCollectionName.text = ""
                    }
                }

                ListView {
                    id: collectionList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: Theme.s1
                    model: Library.collections

                    delegate: Item {
                        id: collectionEntry
                        required property var modelData
                        width: collectionList.width
                        height: 48

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.rMd
                            color: Library.currentCollectionId === collectionEntry.modelData.id
                                   ? Theme.accentGhost
                                   : (hover.hovered ? Theme.overlayFaint : "transparent")
                            border.width: 1
                            border.color: Library.currentCollectionId === collectionEntry.modelData.id
                                          ? Theme.accentHair
                                          : "transparent"

                            HoverHandler { id: hover }

                            TapHandler {
                                onTapped: Library.selectCollection(collectionEntry.modelData.id)
                            }

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: Theme.s2
                                anchors.right: countChip.left
                                anchors.rightMargin: Theme.s1
                                anchors.verticalCenter: parent.verticalCenter
                                text: collectionEntry.modelData.name
                                color: Theme.text
                                font.pixelSize: Theme.fSmall
                                elide: Text.ElideRight
                            }

                            Row {
                                anchors.right: parent.right
                                anchors.rightMargin: Theme.s1
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Theme.s1

                                MChip {
                                    id: countChip
                                    text: collectionEntry.modelData.fileCount
                                    dot: collectionEntry.modelData.mounted
                                    tone: collectionEntry.modelData.mounted ? "ok" : "neutral"
                                }

                                MButton {
                                    width: 24
                                    height: 24
                                    text: "×"
                                    variant: "quiet"
                                    small: true
                                    leftPadding: 0
                                    rightPadding: 0
                                    onClicked: page.pendingRemoval = collectionEntry.modelData.id
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 62
                    radius: Theme.rMd
                    visible: page.pendingRemoval > 0
                    color: Theme.dangerGhost
                    border.width: 1
                    border.color: Theme.dangerHair

                    Column {
                        anchors.centerIn: parent
                        spacing: Theme.s1

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: qsTr("删除小库只删索引，不删原文件")
                            color: Theme.danger
                            font.pixelSize: Theme.fLabel
                        }

                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: Theme.s2

                            MButton {
                                text: qsTr("确认删除")
                                variant: "danger"
                                small: true
                                onClicked: {
                                    Library.removeCollection(page.pendingRemoval)
                                    page.pendingRemoval = 0
                                }
                            }
                            MButton {
                                text: qsTr("取消")
                                variant: "quiet"
                                small: true
                                onClicked: page.pendingRemoval = 0
                            }
                        }
                    }
                }
            }
        }

        // =================================================== 文件 / 文件夹
        MSurface {
            id: filesPanel
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.s4
                spacing: Theme.s2

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.s2

                    MLabel {
                        Layout.fillWidth: true
                        label: Library.currentCollectionName.length > 0
                               ? Library.currentCollectionName
                               : "文件（请先选择小库）"
                    }

                    MButton {
                        text: qsTr("导入文件")
                        variant: "ghost"
                        small: true
                        enabled: Library.available
                        onClicked: fileDialog.open()
                    }
                    MButton {
                        text: qsTr("关联文件夹")
                        variant: "ghost"
                        small: true
                        enabled: Library.available
                        onClicked: folderDialog.open()
                    }
                    MButton {
                        text: qsTr("重建索引")
                        variant: "ghost"
                        small: true
                        enabled: Library.currentCollectionId > 0 && !Library.indexing
                        onClicked: Library.reindexCollection()
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("提示：也可以直接把文件拖到本区域导入（会复制一份入库副本）。")
                    color: Theme.textGhost
                    font.pixelSize: Theme.fLabel
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    visible: Library.message.length > 0
                    text: Library.message
                    color: Theme.textDim
                    font.pixelSize: Theme.fSmall
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }

                // 自动索引已执行，但缺少前置条件时给出可操作的下一步
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.s2
                    visible: page.pendingImageCount > 0 && !Library.captionReady

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("%1 张图片已入库，但缺少视觉模型，暂时无法生成描述与索引。")
                              .arg(page.pendingImageCount)
                        color: Theme.warn
                        font.pixelSize: Theme.fLabel
                        wrapMode: Text.Wrap
                    }
                    MButton {
                        text: qsTr("去配置")
                        variant: "quiet"
                        small: true
                        onClicked: page.scrollToItem(captionHeader)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.s2
                    visible: page.pendingEmbeddingCount > 0 && !Library.embeddingReady

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("%1 个文件已切分成片段，等待向量化（需配置 embedding）。")
                              .arg(page.pendingEmbeddingCount)
                        color: Theme.warn
                        font.pixelSize: Theme.fLabel
                        wrapMode: Text.Wrap
                    }
                    MButton {
                        text: qsTr("去配置")
                        variant: "quiet"
                        small: true
                        onClicked: page.scrollToItem(embeddingHeader)
                    }
                }

                // 关联文件夹快照：手动重建，绝不自动监视用户目录
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.s1
                    visible: Library.folderLinks.length > 0

                    Repeater {
                        model: Library.folderLinks
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: Theme.s2

                            Text {
                                Layout.fillWidth: true
                                text: modelData.rootPath
                                color: Theme.textDim
                                font.pixelSize: Theme.fLabel
                                elide: Text.ElideMiddle
                            }
                            Text {
                                text: qsTr("%1 个文件 · %2").arg(modelData.fileCount).arg(modelData.snapshotAt)
                                color: Theme.textFaint
                                font.pixelSize: Theme.fLabel
                            }
                            MButton {
                                text: qsTr("重建快照")
                                variant: "quiet"
                                small: true
                                onClicked: Library.rebuildFolder(modelData.id)
                            }
                            MButton {
                                text: qsTr("解除关联")
                                variant: "danger"
                                small: true
                                onClicked: Library.unlinkFolder(modelData.id)
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Theme.hairline
                }

                ListView {
                    id: fileList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: Theme.s1
                    model: Library.files

                    delegate: Rectangle {
                        id: fileEntry
                        required property var modelData
                        width: fileList.width
                        height: 62
                        radius: Theme.rMd
                        color: hover.hovered ? Theme.overlayFaint : "transparent"

                        HoverHandler { id: hover }

                        Column {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.s2
                            anchors.right: actions.left
                            anchors.rightMargin: Theme.s2
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 3

                            Row {
                                spacing: Theme.s2

                                Text {
                                    text: fileEntry.modelData.displayName
                                    color: Theme.text
                                    font.pixelSize: Theme.fSmall
                                    elide: Text.ElideRight
                                    width: Math.min(implicitWidth, fileEntry.width - 220)
                                }

                                MChip {
                                    text: fileEntry.modelData.statusLabel
                                    dot: true
                                    tone: fileEntry.modelData.status === "indexed" ? "ok"
                                          : (fileEntry.modelData.status === "failed" ? "danger" : "neutral")
                                }
                            }

                            Text {
                                width: parent.width
                                text: {
                                    const parts = []
                                    parts.push(qsTr("%1 片段").arg(fileEntry.modelData.chunkCount))
                                    if (fileEntry.modelData.indexedAt.length > 0)
                                        parts.push(fileEntry.modelData.indexedAt)
                                    if (fileEntry.modelData.errorText.length > 0)
                                        parts.push(fileEntry.modelData.errorText)
                                    return parts.join(" · ")
                                }
                                color: fileEntry.modelData.errorText.length > 0
                                       ? Theme.warn : Theme.textFaint
                                font.pixelSize: Theme.fLabel
                                elide: Text.ElideRight
                            }
                        }

                        Row {
                            id: actions
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.s1
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.s1

                            MButton {
                                text: qsTr("重建")
                                variant: "quiet"
                                small: true
                                onClicked: Library.reindexFile(fileEntry.modelData.id)
                            }
                            MButton {
                                text: qsTr("移除")
                                variant: "danger"
                                small: true
                                onClicked: Library.removeFile(fileEntry.modelData.id)
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: Library.files.length === 0
                    text: Library.currentCollectionId > 0
                          ? qsTr("还没有文件：点击「导入文件」，或把文件直接拖到这里（复制入库存副本）；也可以关联一个本地文件夹（只建索引，不复制）。")
                          : qsTr("左侧新建或选择一个小库后开始。")
                    color: Theme.textFaint
                    font.pixelSize: Theme.fSmall
                    wrapMode: Text.Wrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.s2

                    Text {
                        Layout.fillWidth: true
                        text: Library.indexingStatus
                        color: Library.indexing ? Theme.accent : Theme.textFaint
                        font.pixelSize: Theme.fLabel
                        elide: Text.ElideRight
                    }
                    MButton {
                        text: qsTr("停止")
                        variant: "quiet"
                        small: true
                        visible: Library.indexing
                        onClicked: Library.cancelIndexing()
                    }
                }
            }

            // 拖拽导入：整个中栏都是投放区，松开即复制入库（原文件不动）
            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                radius: Theme.rLg
                z: 10
                visible: page.dropActive
                color: Theme.accentGhost
                border.width: 1
                border.color: Theme.accentHair

                Column {
                    anchors.centerIn: parent
                    spacing: Theme.s2

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("松开即可导入到「%1」")
                              .arg(Library.currentCollectionName.length > 0
                                   ? Library.currentCollectionName
                                   : qsTr("默认资料库"))
                        color: Theme.text
                        font.pixelSize: Theme.fBody
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("会复制一份入库副本，原文件保持不动")
                        color: Theme.textDim
                        font.pixelSize: Theme.fSmall
                    }
                }
            }

            DropArea {
                anchors.fill: parent
                keys: ["text/uri-list"]
                onEntered: function (drag) {
                    page.dropActive = true
                    drag.accepted = true
                }
                onExited: page.dropActive = false
                onDropped: function (drop) {
                    page.dropActive = false
                    const paths = page.toPathList(drop.urls)
                    if (paths.length === 0) {
                        drop.accepted = false
                        return
                    }
                    drop.acceptProposedAction()
                    Library.importFiles(paths)
                }
            }
        }
        MSurface {
            Layout.preferredWidth: 330
            Layout.fillHeight: true

            Flickable {
                id: settingsFlick
                anchors.fill: parent
                anchors.margins: Theme.s4
                clip: true
                contentWidth: width
                contentHeight: settingsColumn.implicitHeight
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: settingsColumn
                    width: parent.width
                    spacing: Theme.s3

                    MLabel { label: "检索与隐私" }

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

                    MLabel { label: "检索参数" }

                    Text { text: qsTr("Top-K（最终注入片段数）"); color: Theme.textFaint; font.pixelSize: Theme.fLabel }
                    MField {
                        width: parent.width
                        text: Library.topK
                        validator: IntValidator { bottom: 1; top: 30 }
                        onEditingFinished: Library.topK = parseInt(text)
                    }

                    Text { text: qsTr("每文件片段上限"); color: Theme.textFaint; font.pixelSize: Theme.fLabel }
                    MField {
                        width: parent.width
                        text: Library.perFileLimit
                        validator: IntValidator { bottom: 1; top: 10 }
                        onEditingFinished: Library.perFileLimit = parseInt(text)
                    }

                    Text { text: qsTr("注入 token 预算"); color: Theme.textFaint; font.pixelSize: Theme.fLabel }
                    MField {
                        width: parent.width
                        text: Library.tokenBudget
                        validator: IntValidator { bottom: 200; top: 20000 }
                        onEditingFinished: Library.tokenBudget = parseInt(text)
                    }

                    Text { text: qsTr("相似度阈值（0–1）"); color: Theme.textFaint; font.pixelSize: Theme.fLabel }
                    MField {
                        width: parent.width
                        text: Library.similarityThreshold
                        validator: DoubleValidator { bottom: 0; top: 1 }
                        onEditingFinished: Library.similarityThreshold = parseFloat(text)
                    }

                    Text { text: qsTr("邻居扩展半径（前后各 N 段）"); color: Theme.textFaint; font.pixelSize: Theme.fLabel }
                    MField {
                        width: parent.width
                        text: Library.neighborRadius
                        validator: IntValidator { bottom: 0; top: 3 }
                        onEditingFinished: Library.neighborRadius = parseInt(text)
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

                    Text { text: qsTr("切分尺寸（字符）"); color: Theme.textFaint; font.pixelSize: Theme.fLabel }
                    MField {
                        width: parent.width
                        text: Library.chunkSize
                        validator: IntValidator { bottom: 200; top: 4000 }
                        onEditingFinished: Library.chunkSize = parseInt(text)
                    }

                    Text { text: qsTr("片段重叠（字符）"); color: Theme.textFaint; font.pixelSize: Theme.fLabel }
                    MField {
                        width: parent.width
                        text: Library.chunkOverlap
                        validator: IntValidator { bottom: 0; top: 2000 }
                        onEditingFinished: Library.chunkOverlap = parseInt(text)
                    }

                    Rectangle { width: parent.width; height: 1; color: Theme.hairline }

                    MLabel { id: embeddingHeader; label: "内容向量化（embedding）" }

                    MCombo {
                        id: embedCombo
                        width: parent.width
                        model: page.providerNames(page.embedProviders)
                        placeholderText: qsTr("选择厂商")
                        currentIndex: page.indexOfProvider(page.embedProviders, Library.embeddingProviderId)
                        onActivated: {
                            const provider = page.embedProviders[currentIndex]
                            if (provider)
                                embedModel.text = Library.suggestedEmbeddingModel(provider.id)
                        }
                    }

                    MField {
                        id: embedModel
                        width: parent.width
                        placeholderText: qsTr("embedding 模型名")
                        text: Library.embeddingModelId
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
                        enabled: embedCombo.currentIndex >= 0
                        onClicked: {
                            const provider = page.embedProviders[embedCombo.currentIndex]
                            Library.saveEmbeddingConfig(provider.id, embedModel.text)
                        }
                    }

                    MField {
                        id: embedKey
                        width: parent.width
                        echoMode: TextInput.Password
                        placeholderText: Library.hasEmbeddingKey()
                                         ? qsTr("已保存（留空不修改）")
                                         : qsTr("单独填写 embedding API Key（可选）")
                    }

                    MButton {
                        width: parent.width
                        text: qsTr("保存 embedding Key")
                        variant: "quiet"
                        small: true
                        enabled: embedKey.text.trim().length > 0
                        onClicked: {
                            Library.saveEmbeddingKey(embedKey.text)
                            embedKey.text = ""
                        }
                    }

                    Rectangle { width: parent.width; height: 1; color: Theme.hairline }

                    MLabel { id: captionHeader; label: "图片描述（caption）" }

                    MCombo {
                        id: captionCombo
                        width: parent.width
                        model: page.providerNames(page.captionProviders)
                        placeholderText: qsTr("选择视觉模型厂商")
                        currentIndex: page.indexOfProvider(page.captionProviders, Library.captionProviderId)
                        onActivated: {
                            const provider = page.captionProviders[currentIndex]
                            if (provider)
                                captionModel.text = Library.suggestedCaptionModel(provider.id)
                        }
                    }

                    MField {
                        id: captionModel
                        width: parent.width
                        placeholderText: qsTr("视觉模型名")
                        text: Library.captionModelId
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
                        enabled: captionCombo.currentIndex >= 0
                        onClicked: {
                            const provider = page.captionProviders[captionCombo.currentIndex]
                            Library.saveCaptionConfig(provider.id, captionModel.text)
                        }
                    }

                    MField {
                        id: captionKey
                        width: parent.width
                        echoMode: TextInput.Password
                        placeholderText: Library.hasCaptionKey()
                                         ? qsTr("已保存（留空不修改）")
                                         : qsTr("单独填写视觉 API Key（可选）")
                    }

                    MButton {
                        width: parent.width
                        text: qsTr("保存图片描述 Key")
                        variant: "quiet"
                        small: true
                        enabled: captionKey.text.trim().length > 0
                        onClicked: {
                            Library.saveCaptionKey(captionKey.text)
                            captionKey.text = ""
                        }
                    }

                    Rectangle { width: parent.width; height: 1; color: Theme.hairline }

                    MLabel { label: "成本预估（参考）" }

                    Text {
                        width: parent.width
                        text: qsTr("文本 %1 token · 图片 %2 张")
                              .arg(Library.estimate.textTokens || 0)
                              .arg(Library.estimate.imageCount || 0)
                        color: Theme.textDim
                        font.pixelSize: Theme.fSmall
                    }
                    Text {
                        width: parent.width
                        text: qsTr("首次建向量：%1（%2 token）")
                              .arg(Library.estimate.embeddingCost || "—")
                              .arg(Library.estimate.embeddingTokens || 0)
                        color: Theme.textDim
                        font.pixelSize: Theme.fSmall
                    }
                    Text {
                        width: parent.width
                        visible: (Library.estimate.imageCount || 0) > 0
                        text: qsTr("图片描述：%1（约 %2 token）")
                              .arg(Library.estimate.visionCost || "—")
                              .arg(Library.estimate.visionTokens || 0)
                        color: Theme.textDim
                        font.pixelSize: Theme.fSmall
                    }
                    Text {
                        width: parent.width
                        text: qsTr("每次对话注入上限：%1 token ≈ %2")
                              .arg(Library.estimate.contextTokens || 0)
                              .arg(Library.estimate.contextCost || "—")
                        color: Theme.textDim
                        font.pixelSize: Theme.fSmall
                    }
                    Text {
                        width: parent.width
                        text: Library.estimate.note || ""
                        color: Theme.textGhost
                        font.pixelSize: Theme.fLabel
                        wrapMode: Text.Wrap
                    }

                    Rectangle { width: parent.width; height: 1; color: Theme.hairline }

                    MLabel { label: "库内搜索（本地关键词）" }

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

                    Item { width: 1; height: Theme.s4 }
                }
            }
        }
    }

    // -------------------------------------------------------------- 文件选择
    FileDialog {
        id: fileDialog
        title: qsTr("导入到资料库（会复制一份入库，原文件不动）")
        fileMode: FileDialog.OpenFiles
        nameFilters: [
            qsTr("支持的文档 (*.txt *.md *.csv *.json *.pdf *.docx *.xlsx *.pptx *.png *.jpg *.jpeg *.webp *.bmp)"),
            qsTr("图片 (*.png *.jpg *.jpeg *.webp *.bmp)"),
            qsTr("所有文件 (*)")
        ]
        onAccepted: Library.importFiles(page.toPathList(selectedFiles))
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("关联文件夹（只建索引引用，不复制）")
        onAccepted: Library.linkFolder(selectedFolder)
    }

    LibraryConsent {
        id: consentDialog
        anchors.fill: parent
        onAccepted: Library.grantConsent()
    }
}
