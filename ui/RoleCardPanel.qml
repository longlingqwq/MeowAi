import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

/**
 * 角色卡管理弹层（M18）。
 *
 * 职责边界：
 *  - 只做管理编排：新增 / 编辑（RoleCardDialog）、删除（两段确认）、导入（同名冲突
 *    必须显式选择覆盖或跳过，绝不静默覆盖）、导出（单个 JSON 数组）；
 *  - 数据一律走 App.roleCardStore；套用动作不在此处（侧栏点击卡片即套用）；
 *  - 删除「当前生效」的角色卡时发 activeCardRemoved，由侧栏清空系统提示词并提示一次。
 */
Popup {
    id: root

    /// 当前生效的角色卡 id（仅用于删除判断与回传，-1 = 未套用）
    property int activeCardId: -1
    property string statusText: ""
    property bool statusIsError: false
    /// 预检出的待导入冲突：非空时展示「覆盖 / 跳过」二选一
    property var pendingConflicts: []
    property string pendingImportPath: ""
    /// 删除当前生效卡的一次性提示（MToast 只承载这一条，其余反馈走状态行）
    property string noticeMessage: ""

    signal activeCardRemoved(int cardId)
    signal saved(int cardId)

    readonly property var store: App.roleCardStore
    /// 存储可用性：数据库不可用（或 store 未构造）时整块降级为只读提示
    readonly property bool storeReady: store !== null && store !== undefined && store.available
    property var cards: []

    function refresh() {
        cards = storeReady ? store.list() : []
    }

    function setStatus(text, isError) {
        statusText = text
        statusIsError = isError
    }

    Component.onCompleted: refresh()

    Connections {
        target: root.store
        function onChanged() { root.refresh() }
    }

    onOpened: {
        pendingConflicts = []
        pendingImportPath = ""
        setStatus("", false)
        refresh()
    }

    function beginImport(path) {
        if (!storeReady) {
            setStatus(qsTr("本地数据库不可用，暂不能导入。"), true)
            return
        }
        const preview = store.importFromFile(path, "preview")
        if (!preview.ok) {
            setStatus(preview.error || qsTr("导入失败"), true)
            return
        }
        const conflicts = preview.conflicts !== undefined ? preview.conflicts : []
        if (conflicts.length === 0) {
            finishImport(path, "skip")
            return
        }
        // 同名冲突：先问清覆盖还是跳过，未选择前不写任何数据
        pendingImportPath = path
        pendingConflicts = conflicts
        setStatus("", false)
    }

    function finishImport(path, mode) {
        const result = store.importFromFile(path, mode)
        pendingConflicts = []
        pendingImportPath = ""
        if (!result.ok) {
            setStatus(result.error || qsTr("导入失败"), true)
            return
        }
        let summary = qsTr("导入完成：新增 %1").arg(result.imported)
        if (result.skipped > 0)
            summary += qsTr(" · 跳过同名 %1").arg(result.skipped)
        if (result.overwritten > 0)
            summary += qsTr(" · 覆盖同名 %1").arg(result.overwritten)
        if (result.invalid > 0)
            summary += qsTr(" · 无效条目 %1").arg(result.invalid)
        setStatus(summary, false)
    }

    function exportTo(url) {
        // FileDialog 的 SaveFile 不强制后缀：缺失时补 .json，避免双击打不开
        let target = url.toString().replace(/^file:\/\/\//, "")
        if (!target.toLowerCase().endsWith(".json"))
            target += ".json"
        if (storeReady && store.exportToFile(target)) {
            setStatus(qsTr("已导出 %1 张角色卡 → %2").arg(cards.length).arg(target), false)
        } else {
            setStatus(store.lastError.length > 0 ? store.lastError : qsTr("导出失败"), true)
        }
    }

    function removeCard(card) {
        if (!storeReady || !store.remove(card.id)) {
            setStatus(store.lastError.length > 0 ? store.lastError : qsTr("删除失败"), true)
            return
        }
        setStatus(qsTr("已删除「%1」").arg(card.name), false)
        // 删除的是当前生效卡：状态由侧栏统一清空（保持绑定），这里只上报
        if (card.id === root.activeCardId)
            root.activeCardRemoved(card.id)
    }

    parent: Overlay.overlay
    width: Math.min(560, parent ? parent.width - 48 : 520)
    height: Math.min(Math.max(panelBody.implicitHeight + Theme.s5 * 2, 220),
                     parent ? parent.height - 48 : 680)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: Theme.s5
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        radius: Theme.rLg
        color: Theme.ink3
        border.width: 1
        border.color: Theme.hairlineStrong
    }

    contentItem: Column {
        id: panelBody
        spacing: Theme.s3

        // ------------------------------------------------ 标题与动作
        Item {
            width: parent.width
            height: Math.max(titleText.implicitHeight, actionsRow.height)

            MLabel {
                id: titleText
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                label: qsTr("角色卡管理")
            }

            Row {
                id: actionsRow
                anchors.right: parent.right
                spacing: Theme.s2

                MButton {
                    text: qsTr("新增")
                    variant: "primary"
                    small: true
                    enabled: root.storeReady
                    onClicked: editDialog.openNew()
                }

                MButton {
                    text: qsTr("导入")
                    small: true
                    enabled: root.storeReady
                    onClicked: importDialog.open()
                }

                MButton {
                    text: qsTr("导出")
                    small: true
                    enabled: root.storeReady && root.cards.length > 0
                    onClicked: exportDialog.open()
                }
            }
        }

        Text {
            width: parent.width
            visible: !root.storeReady
            text: qsTr("本地数据库不可用，角色卡暂不能读写。")
            color: Theme.danger
            font.pixelSize: Theme.fSmall
            wrapMode: Text.Wrap
        }

        // ------------------------------------------------ 冲突选择条（导入预检命中同名）
        Rectangle {
            width: parent.width
            height: conflictColumn.implicitHeight + Theme.s3 * 2
            visible: root.pendingConflicts.length > 0
            radius: Theme.rMd
            color: Theme.warn
            opacity: 0.12

            Column {
                id: conflictColumn
                anchors.fill: parent
                anchors.margins: Theme.s3
                spacing: Theme.s2

                Text {
                    width: parent.width
                    text: qsTr("导入内容与已有角色卡同名：%1。请选择处理方式（未选择前不会写入任何数据）。")
                          .arg(root.pendingConflicts.join("、"))
                    color: Theme.text
                    font.pixelSize: Theme.fSmall
                    wrapMode: Text.Wrap
                }

                Row {
                    spacing: Theme.s2
                    layoutDirection: Qt.RightToLeft

                    MButton {
                        text: qsTr("覆盖同名")
                        variant: "primary"
                        small: true
                        onClicked: root.finishImport(root.pendingImportPath, "overwrite")
                    }

                    MButton {
                        text: qsTr("跳过同名")
                        variant: "ghost"
                        small: true
                        onClicked: root.finishImport(root.pendingImportPath, "skip")
                    }

                    MButton {
                        text: qsTr("取消导入")
                        variant: "quiet"
                        small: true
                        onClicked: {
                            root.pendingConflicts = []
                            root.pendingImportPath = ""
                        }
                    }
                }
            }
        }

        // ------------------------------------------------ 卡片列表
        ListView {
            width: parent.width
            height: Math.min(contentHeight, root.cards.length > 0 ? 4 * 46 : 44)
            clip: true
            visible: root.cards.length > 0
            model: root.cards
            spacing: Theme.s1

            ScrollIndicator.vertical: ScrollIndicator { }

            delegate: Item {
                width: ListView.view.width
                height: 42

                required property var modelData

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.rMd
                    color: rowHover.hovered ? Theme.overlayFaint : "transparent"
                    border.width: 1
                    border.color: Theme.hairline

                    Behavior on color {
                        ColorAnimation { duration: Theme.dFast }
                    }
                }

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.s2
                    anchors.rightMargin: Theme.s2
                    spacing: Theme.s2

                    MAvatar {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 28
                        height: 28
                        source: modelData.avatarUrl || ""
                        fallbackText: (modelData.name || "?").charAt(0).toUpperCase()
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 28 - Theme.s2 * 3 - actionRow.width
                        spacing: 1
                        Text {
                            width: parent.width
                            text: modelData.name || ""
                            color: modelData.id === root.activeCardId ? Theme.accentHi : Theme.text
                            font.pixelSize: Theme.fSmall
                            font.weight: modelData.id === root.activeCardId
                                         ? Font.DemiBold : Font.Normal
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            // 默认卡（未锁定模型）展示其套用时会真正使用的当前模型
                            text: {
                                if (modelData.modelId && modelData.modelId.length > 0)
                                    return modelData.modelId
                                if (Chat.modelId && Chat.modelId.length > 0)
                                    return Chat.modelId
                                return qsTr("默认")
                            }
                            color: Theme.textFaint
                            font.pixelSize: Theme.fLabel
                            elide: Text.ElideRight
                        }
                    }

                    Item { height: 1; width: 0 }

                    Row {
                        id: actionRow
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.s1

                        MButton {
                            text: qsTr("编辑")
                            variant: "quiet"
                            small: true
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: editDialog.openEdit(modelData)
                        }

                        // 两段确认：先变红「删除」，再次点击才真正执行
                        MButton {
                            id: deleteButton
                            property bool confirming: false
                            text: confirming ? qsTr("确认删除") : qsTr("删除")
                            variant: confirming ? "danger" : "quiet"
                            small: true
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: {
                                if (confirming) {
                                    confirming = false
                                    root.removeCard(modelData)
                                } else {
                                    confirming = true
                                    confirmReset.restart()
                                }
                            }

                            Timer {
                                id: confirmReset
                                interval: 3000
                                onTriggered: deleteButton.confirming = false
                            }
                        }
                    }
                }

                HoverHandler { id: rowHover }
            }
        }

        Text {
            width: parent.width
            visible: root.cards.length === 0 && root.storeReady
            text: qsTr("还没有角色卡。点「新增」创建，或「导入」一份 JSON。")
            color: Theme.textGhost
            font.pixelSize: Theme.fSmall
            wrapMode: Text.Wrap
        }

        Text {
            width: parent.width
            visible: root.statusText.length > 0
            text: root.statusText
            color: root.statusIsError ? Theme.danger : Theme.textFaint
            font.pixelSize: Theme.fSmall
            wrapMode: Text.Wrap
        }
    }

    // ------------------------------------------------ 删除当前生效卡的一次性提示
    // 面板持有自己的单条 Toast（M15 组件自绘自管，不依赖 main.qml 宿主）；
    // 常驻高度为 0 的容器承载，避免 Toast 出现时把 Column 布局顶高
    Item {
        width: parent.width
        height: 0
        z: 10

        Loader {
            id: noticeLoader
            active: root.noticeMessage.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            y: -Theme.s5 - height

            sourceComponent: MToast {
                maxWidth: Math.min(420, root.width - Theme.s4 * 2)
                message: root.noticeMessage.length > 0
                         ? root.noticeMessage : qsTr("操作失败，请重试")
                isError: false
                onClosed: root.noticeMessage = ""
            }
        }
    }

    RoleCardDialog {
        id: editDialog
        onSaved: (cardId) => {
            root.refresh()
            root.saved(cardId)
        }
    }

    FileDialog {
        id: importDialog
        title: qsTr("导入角色卡（JSON 数组）")
        nameFilters: [qsTr("角色卡 (*.json)")]
        fileMode: FileDialog.OpenFile
        onAccepted: root.beginImport(selectedFile.toString().replace(/^file:\/\/\//, ""))
    }

    FileDialog {
        id: exportDialog
        title: qsTr("导出角色卡")
        nameFilters: [qsTr("角色卡 (*.json)")]
        fileMode: FileDialog.SaveFile
        onAccepted: root.exportTo(selectedFile.toString().replace(/^file:\/\/\//, ""))
    }
}
