import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

/**
 * 角色卡新建 / 编辑弹层（M18）。
 *
 * 职责边界：
 *  - 只做表单：头像（复用 AvatarCrop，512×512，外部保存模式）、名称（必填去重）、
 *    厂商 / 模型 / 思考档位（数据全部来自 Chat 的 QML 视图接口）、系统提示词（可空）；
 *  - 保存一律走 App.roleCardStore（storage 层），本组件不直连数据库、不拼业务路径；
 *  - 新建时预填当前生效的厂商 / 模型 / 档位：角色卡默认是「当前对话设置的快照」。
 */
Popup {
    id: root

    /// 编辑中的角色卡 id；-1 表示新建
    property int cardId: -1
    /// 头像相对路径（已托管在 assets/rolecards/）；空串 = 无头像
    property string avatarPath: ""
    property string errorText: ""
    /// 无头像时头像位显示的首字：由名称输入框驱动（显式赋值，避免依赖前向 id 绑定）
    property string nameInitial: "?"

    /// 保存成功（携带卡 id；侧栏据此对「当前生效卡被编辑」自动重套用）
    signal saved(int cardId)

    readonly property var store: App.roleCardStore
    /// 下拉数据与 id 的平行映射（MCombo 只吃字符串模型）
    property var providerIds: []
    property var modelIds: []
    property var levelValues: []

    parent: Overlay.overlay
    width: Math.min(520, parent ? parent.width - 48 : 480)
    height: Math.min(body.implicitHeight + Theme.s5 * 2, parent ? parent.height - 48 : 720)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: Theme.s5
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function currentProviderId() {
        return (providerBox.currentIndex >= 0
                && providerBox.currentIndex < root.providerIds.length)
               ? root.providerIds[providerBox.currentIndex] : ""
    }

    function currentModelId() {
        return (modelBox.currentIndex >= 0 && modelBox.currentIndex < root.modelIds.length)
               ? root.modelIds[modelBox.currentIndex] : ""
    }

    function currentLevelValue() {
        return (levelBox.currentIndex >= 0 && levelBox.currentIndex < root.levelValues.length)
               ? root.levelValues[levelBox.currentIndex] : ""
    }

    /// 在平行 id 表中定位并选中；不存在时回落首项（无项则 -1）
    function selectById(box, ids, id) {
        const index = ids.indexOf(id)
        box.currentIndex = index >= 0 ? index : (ids.length > 0 ? 0 : -1)
    }

    function rebuildProviders(preferredId) {
        const entries = Chat.providerOptions()
        // 首项「默认」= 不指定厂商：套用时跟随当前正在使用的模型设置
        const names = [qsTr("默认")]
        const ids = [""]
        for (let i = 0; i < entries.length; ++i) {
            names.push(entries[i].displayName || entries[i].id)
            ids.push(entries[i].id)
        }
        root.providerIds = ids
        providerBox.model = names
        selectById(providerBox, ids, preferredId)
        rebuildModels("")
    }

    function rebuildModels(preferredId) {
        const providerId = currentProviderId()
        const entries = providerId.length > 0 ? Chat.modelOptions(providerId) : []
        // 首项「默认」= 不指定模型：套用即调用当前正在使用的模型，用户改选后才锁定
        const names = [qsTr("默认")]
        const ids = [""]
        for (let i = 0; i < entries.length; ++i) {
            names.push(entries[i].displayName || entries[i].id)
            ids.push(entries[i].id)
        }
        root.modelIds = ids
        modelBox.model = names
        selectById(modelBox, ids, preferredId)
        rebuildLevels(null)
    }

    function rebuildLevels(preferred) {
        const providerId = currentProviderId()
        const modelId = currentModelId()
        let levels = []
        if (providerId.length > 0 && modelId.length > 0) {
            const info = Chat.modelInfo(providerId, modelId)
            if (info && info.levels !== undefined)
                levels = info.levels
        }
        // 首项「默认」= 不指定档位（套用时保持当前模型的默认档位）
        const names = [qsTr("默认")]
        const values = [""]
        for (let i = 0; i < levels.length; ++i) {
            names.push(levels[i])
            values.push(levels[i])
        }
        root.levelValues = values
        levelBox.model = names
        let index = values.indexOf(preferred)
        levelBox.currentIndex = index >= 0 ? index : 0
    }

    /// 名称首字（无头像时作为头像位徽记）；空名称回落 "?"
    function initialOf(value) {
        const trimmed = value.trim()
        return trimmed.length > 0 ? trimmed.charAt(0).toUpperCase() : "?"
    }

    function openNew() {
        root.cardId = -1
        nameField.text = ""
        promptArea.text = ""
        root.avatarPath = ""
        root.errorText = ""
        // 默认三项全为「默认」：套用时跟随当前正在使用的厂商 / 模型 / 档位；
        // 只有用户主动改选，卡片才锁定为具体模型
        rebuildProviders("")
        selectById(modelBox, root.modelIds, "")
        rebuildLevels("")
        root.open()
        nameField.forceActiveFocus()
    }

    function openEdit(card) {
        root.cardId = card.id
        nameField.text = card.name || ""
        promptArea.text = card.systemPrompt || ""
        root.avatarPath = card.avatarPath || ""
        root.errorText = ""
        rebuildProviders(card.providerId || "")
        selectById(modelBox, root.modelIds, card.modelId || "")
        rebuildLevels(card.thinkingLevel || "")
        root.open()
        nameField.forceActiveFocus()
    }

    function save() {
        const name = nameField.text.trim()
        if (name.length === 0) {
            root.errorText = qsTr("名称不能为空")
            return
        }
        if (store.nameExists(name, root.cardId)) {
            root.errorText = qsTr("已存在同名角色卡：%1").arg(name)
            return
        }
        const providerId = currentProviderId()
        const modelId = currentModelId()
        const level = currentLevelValue()
        const editingId = root.cardId
        let resultId = -2
        if (editingId < 0) {
            resultId = store.create(name, promptArea.text, providerId, modelId, level,
                                    root.avatarPath)
        } else if (store.update(editingId, name, promptArea.text, providerId, modelId, level,
                                root.avatarPath)) {
            resultId = editingId
        }
        if (resultId < 0) {
            root.errorText = store.lastError.length > 0
                         ? store.lastError : qsTr("保存失败，请重试。")
            return
        }
        root.close()
        root.saved(resultId)
    }

    background: Rectangle {
        radius: Theme.rLg
        color: Theme.ink3
        border.width: 1
        border.color: Theme.hairlineStrong
    }

    contentItem: Column {
        id: body
        spacing: Theme.s3

        Column {
            width: parent.width
            spacing: Theme.s1

            MLabel {
                label: root.cardId < 0 ? qsTr("新建角色卡") : qsTr("编辑角色卡")
            }

            Text {
                width: parent.width
                text: qsTr("厂商 / 模型 / 档位默认为「默认」：套用时直接使用当前正在使用的模型；改选后卡片才锁定为你指定的模型。套用会同时生效系统提示词；头像仅作展示。")
                color: Theme.textFaint
                font.pixelSize: Theme.fSmall
                wrapMode: Text.Wrap
            }
        }

        // ------------------------------------------------ 头像 + 名称
        Row {
            width: parent.width
            spacing: Theme.s4

            Column {
                id: avatarColumn
                width: 72
                spacing: Theme.s2

                MAvatar {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 64
                    height: 64
                    source: root.avatarPath.length > 0 ? store.avatarUrl(root.avatarPath) : ""
                    fallbackText: root.nameInitial
                }

                MButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.avatarPath.length > 0 ? qsTr("清除") : qsTr("头像…")
                    variant: "quiet"
                    small: true
                    onClicked: {
                        if (root.avatarPath.length > 0)
                            root.avatarPath = ""
                        else
                            avatarDialog.open()
                    }
                }
            }

            MField {
                id: nameField
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - avatarColumn.width - Theme.s4
                placeholderText: qsTr("角色名称（必填，不可重复）")
                // 头像位的首字跟随名称实时变化（无头像时才可见）
                onTextChanged: root.nameInitial = root.initialOf(text)
                onAccepted: root.save()
            }
        }

        // ------------------------------------------------ 厂商 / 模型 / 档位
        Row {
            width: parent.width
            spacing: Theme.s2

            MCombo {
                id: providerBox
                width: (parent.width - Theme.s2 * 2) / 3
                placeholderText: qsTr("厂商")
                onActivated: root.rebuildModels("")
            }

            MCombo {
                id: modelBox
                width: (parent.width - Theme.s2 * 2) / 3
                placeholderText: qsTr("模型")
                onActivated: root.rebuildLevels(null)
            }

            MCombo {
                id: levelBox
                width: (parent.width - Theme.s2 * 2) / 3
                placeholderText: qsTr("档位")
            }
        }

        // ------------------------------------------------ 系统提示词
        Column {
            width: parent.width
            spacing: Theme.s1

            MLabel { label: qsTr("系统提示词") }

            TextArea {
                id: promptArea
                width: parent.width
                height: 120
                wrapMode: TextArea.Wrap
                color: Theme.text
                placeholderTextColor: Theme.textGhost
                placeholderText: qsTr("为空则套用后不下发任何 system 消息")
                selectionColor: Theme.accent
                selectedTextColor: Theme.accentInk
                font.pixelSize: Theme.fBody
                background: Rectangle {
                    radius: Theme.rMd
                    color: Theme.ink1
                    border.width: 1
                    border.color: promptArea.activeFocus
                                  ? Theme.accentHair
                                  : (promptArea.hovered ? Theme.hairlineStrong : Theme.hairline)
                }
            }
        }

        Text {
            width: parent.width
            visible: root.errorText.length > 0
            text: root.errorText
            color: Theme.danger
            font.pixelSize: Theme.fSmall
            wrapMode: Text.Wrap
        }

        // ------------------------------------------------ 操作
        Row {
            width: parent.width
            spacing: Theme.s2
            layoutDirection: Qt.RightToLeft

            MButton {
                text: qsTr("保存")
                variant: "primary"
                small: true
                onClicked: root.save()
            }

            MButton {
                text: qsTr("取消")
                variant: "ghost"
                small: true
                onClicked: root.close()
            }
        }
    }

    FileDialog {
        id: avatarDialog
        title: qsTr("选择头像图片")
        nameFilters: [qsTr("图片 (*.png *.jpg *.jpeg *.webp *.bmp)")]
        onAccepted: cropPopup.start(selectedFile.toString())
    }

    AvatarCrop {
        id: cropPopup
        externalSave: true
        onCropped: function (tempUrl) {
            const relative = store.saveAvatar(tempUrl)
            if (relative.length === 0) {
                root.errorText = store.lastError.length > 0
                                 ? store.lastError : qsTr("头像保存失败。")
                return
            }
            root.avatarPath = relative
            root.errorText = ""
        }
    }
}
