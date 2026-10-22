import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MeowAi

/**
 * 资料库页（v0.2.6 · M3）。
 *
 * 两栏：小库 → 文件与关联文件夹。服务配置、检索参数、隐私、成本与库内搜索
 * 全部在右侧「资料库配置」抽屉里（LibraryConfigPanel），入口是顶栏的「配置」按钮，
 * 本页不再用一栏固定宽度承载配置。
 * 所有业务都经 `Library`（LibraryController）转发，本文件不直连数据库、不发网络请求。
 * 删除类操作一律二次确认；文案反复强调「用户原文件不会被删除」。
 */
Item {
    id: page

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

    Connections {
        target: Library
        function onConsentRequired() { consentDialog.open = true }
    }

    // ---------------------------------------------------------------- 两栏
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

                MLabel { label: qsTr("小库") }

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
                        // 行高跟随名称实际行数：长名称换行到第二行时不再被固定 48 裁掉
                        height: Math.max(48, nameLabel.implicitHeight + Theme.s2 * 2)

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
                                id: nameLabel
                                anchors.left: parent.left
                                anchors.leftMargin: Theme.s2
                                anchors.right: actionsRow.left
                                anchors.rightMargin: Theme.s1
                                anchors.verticalCenter: parent.verticalCenter
                                text: collectionEntry.modelData.name
                                color: Theme.text
                                font.pixelSize: Theme.fSmall
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }

                            Row {
                                id: actionsRow
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
                               : qsTr("文件（请先选择小库）")
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

                // 隐私状态常驻可见：配置移入抽屉后，这里保留一句状态与入口指引
                Text {
                    Layout.fillWidth: true
                    text: Library.consentGiven
                          ? qsTr("已同意内容外发。服务、检索参数与隐私设置可在顶栏「配置」抽屉中调整。")
                          : qsTr("尚未同意内容外发：文件可以本地入库，但不会建立向量。请在顶栏「配置」抽屉中同意并启用。")
                    color: Library.consentGiven ? Theme.ok : Theme.warn
                    font.pixelSize: Theme.fLabel
                    wrapMode: Text.Wrap
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("提示：也可以直接把文件拖到本区域导入（会复制一份入库副本）。")
                    color: Theme.textGhost
                    font.pixelSize: Theme.fLabel
                    wrapMode: Text.Wrap
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
                Text {
                    Layout.fillWidth: true
                    visible: page.pendingImageCount > 0 && !Library.captionReady
                    text: qsTr("%1 张图片已入库，但缺少视觉模型，暂时无法生成描述与索引。请在顶栏「配置」抽屉中设置图片描述模型。")
                          .arg(page.pendingImageCount)
                    color: Theme.warn
                    font.pixelSize: Theme.fLabel
                    wrapMode: Text.Wrap
                }

                Text {
                    Layout.fillWidth: true
                    visible: page.pendingEmbeddingCount > 0 && !Library.embeddingReady
                    text: qsTr("%1 个文件已切分成片段，等待向量化。请在顶栏「配置」抽屉中设置内容向量化服务。")
                          .arg(page.pendingEmbeddingCount)
                    color: Theme.warn
                    font.pixelSize: Theme.fLabel
                    wrapMode: Text.Wrap
                }

                // 关联文件夹快照：手动重建，绝不自动监视用户目录
                Column {
                    Layout.fillWidth: true
                    spacing: Theme.s1
                    visible: Library.folderLinks.length > 0

                    Repeater {
                        model: Library.folderLinks
                        delegate: RowLayout {
                            required property var modelData
                            width: parent.width
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
                        // 行高跟随内容：长文件名 / 长错误信息换行时不会挤压或裁切
                        height: Math.max(62, entryColumn.implicitHeight + Theme.s3)
                        radius: Theme.rMd
                        color: hover.hovered ? Theme.overlayFaint : "transparent"

                        HoverHandler { id: hover }

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

                        ColumnLayout {
                            id: entryColumn
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.s2
                            anchors.right: actions.left
                            anchors.rightMargin: Theme.s2
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.s1

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.s2

                                Text {
                                    Layout.fillWidth: true
                                    text: fileEntry.modelData.displayName
                                    color: Theme.text
                                    font.pixelSize: Theme.fSmall
                                    elide: Text.ElideRight
                                }

                                MChip {
                                    Layout.alignment: Qt.AlignVCenter
                                    // 状态胶囊限宽：名称再长也不会把胶囊顶到操作按钮上
                                    maxWidth: Math.min(120, Math.max(48, fileEntry.width * 0.28))
                                    text: fileEntry.modelData.statusLabel
                                    dot: true
                                    tone: fileEntry.modelData.status === "indexed" ? "ok"
                                          : (fileEntry.modelData.status === "failed" ? "danger" : "neutral")
                                }
                            }

                            Text {
                                Layout.fillWidth: true
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
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
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
