import QtQuick
import QtQuick.Controls
import QtQuick.Effects

/**
 * 模型二级菜单（Popup）：**模型清单 + 思考档位 + 高级原生参数透传**。
 *
 * 纯展示与绑定：所有数据都来自 `Chat`（ChatController），本文件不解析 providers.json、
 * 不碰网络与密钥。三个入口（悬停 1s / 单击 / 长按）由调用方在触发器上实现，统一调用 openAt()。
 *
 * 交互约定：
 *  - 点模型行＝切换模型（菜单保持打开，便于继续调档位）；
 *  - 点档位胶囊＝切到该模型并应用该档位，然后关闭菜单；
 *  - 高级参数存在时**覆盖**档位映射，点任一档位会解除覆盖（语义唯一，避免歧义）。
 */
Popup {
    id: menu

    /// 用户点击「联网搜索 · 配置…」时发出，由调用方负责关闭本菜单并打开第三级弹层
    signal configureRequested()

    /// 由调用方在 open() 之前调用：把 Chat 的数据快照到本地数组，避免在绑定里反复求值
    function refresh() {
        const options = Chat.modelOptions(Chat.providerId)
        const names = []
        const ids = []
        const levels = []
        const vision = []
        for (let i = 0; i < options.length; ++i) {
            const entry = options[i]
            names.push(entry.displayName)
            ids.push(entry.id)
            levels.push(entry.levels ? entry.levels : [])
            vision.push(entry.supportsVision === true)
        }
        modelNames = names
        modelIds = ids
        modelLevels = levels
        modelVision = vision
        advancedEdit.text = JSON.stringify(Chat.advancedParams())
        advancedError.text = ""
        modelList.scrollToCurrent()
    }

    /// 在触发器下方弹出；下方放不下时自动翻到上方，并整体夹在窗口内
    function openAt(anchorItem, hostItem) {
        refresh()
        menu.hostItem = hostItem
        const point = anchorItem.mapToItem(hostItem, 0, anchorItem.height + 6)
        const popupWidth = Math.min(menu.implicitWidth, hostItem.width - 16)
        const popupHeight = Math.min(menu.implicitHeight, hostItem.height - 16)
        x = Math.max(8, Math.min(point.x, hostItem.width - popupWidth - 8))
        let top = point.y
        if (top + popupHeight > hostItem.height - 8)
            top = point.y - anchorItem.height - 12 - popupHeight
        y = Math.max(8, top)
        open()
    }

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

    property var modelNames: []
    property var modelIds: []
    property var modelLevels: []
    property var modelVision: []
    /// 弹层宿主（窗口根 Item）：用于把弹层尺寸与位置夹在可视范围内
    property var hostItem: null

    implicitWidth: 470
    implicitHeight: body.implicitHeight + Theme.s4 * 2
    // 窗口小/矮时整体收缩，超出的内容交给内部滚动，避免弹层底部被屏幕裁掉
    width: hostItem ? Math.min(implicitWidth, hostItem.width - 16) : implicitWidth
    height: hostItem ? Math.min(implicitHeight, hostItem.height - 16) : implicitHeight
    padding: 0
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        radius: Theme.rLg
        color: Theme.ink3
        border.width: 1
        border.color: Theme.hairlineStrong

        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Theme.shadow
            shadowBlur: 0.6
            shadowVerticalOffset: 10
        }
    }

    // 内容区可滚动：窗口较矮时不会被裁掉下半部分（模型清单、高级参数都能滚到）
    Flickable {
        id: flick
        anchors.fill: parent
        anchors.margins: Theme.s4
        clip: true
        contentWidth: width
        contentHeight: body.implicitHeight
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
            width: 6
        }

        Column {
            id: body
            width: flick.width
            spacing: Theme.s3

        // ------------------------------------------------------------ 头部
        Item {
            width: parent.width
            height: Math.max(30, headLeft.height)

            Row {
                id: headLeft
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.s2

                MLabel {
                    label: qsTr("模型与档位")
                    anchors.verticalCenter: parent.verticalCenter
                }

                MChip {
                    tone: "accent"
                    text: Chat.providerId
                    maxWidth: 120
                    anchors.verticalCenter: parent.verticalCenter
                }

                MChip {
                    tone: "neutral"
                    text: Chat.modelId.length > 0 ? Chat.modelId : qsTr("未选择模型")
                    maxWidth: 160
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.s1

                MButton {
                    text: qsTr("拉取清单")
                    variant: "quiet"
                    small: true
                    visible: Chat.modelListFetchable
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: Chat.fetchModelList()
                }

                MButton {
                    text: qsTr("关闭")
                    variant: "quiet"
                    small: true
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: menu.close()
                }
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.hairline }

        Text {
            width: parent.width
            visible: Chat.modelListMessage.length > 0
            text: Chat.modelListMessage
            color: Chat.modelListMessage.indexOf(qsTr("失败")) >= 0 ? Theme.warn : Theme.textFaint
            font.pixelSize: Theme.fSmall
            wrapMode: Text.Wrap
        }

        // ------------------------------------------------------------ 模型清单
        ListView {
            id: modelList
            width: parent.width
            height: Math.min(264, contentHeight)
            clip: true
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            model: menu.modelNames

            /// 打开时把当前模型滚进视野，避免长清单里找不到选中项
            function scrollToCurrent() {
                const index = menu.modelIds.indexOf(Chat.modelId)
                if (index >= 0)
                    positionViewAtIndex(index, ListView.Contain)
            }

            delegate: Item {
                id: rowItem
                required property int index
                required property string modelData

                readonly property string id: menu.modelIds[rowItem.index]
                readonly property var levels: menu.modelLevels[rowItem.index]
                readonly property bool current: Chat.modelId === rowItem.id

                width: ListView.view ? ListView.view.width : 0
                height: Math.max(38, levelRow.height + 10)

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.rMd
                    color: rowItem.current ? Theme.accentGhost
                                           : (rowHover.hovered ? Theme.overlayFaint : "transparent")
                    border.width: 1
                    border.color: rowItem.current ? Theme.accentHair : "transparent"

                    Behavior on color {
                        ColorAnimation { duration: Theme.dFast }
                    }
                }

                Text {
                    id: nameText
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.s3
                    anchors.top: parent.top
                    anchors.topMargin: 6
                    width: Math.max(60, parent.width - levelRow.width - Theme.s3 * 2 - 90)
                    text: rowItem.modelData
                    color: rowItem.current ? Theme.accentHi : Theme.text
                    font.pixelSize: Theme.fSmall
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }

                MChip {
                    anchors.left: nameText.right
                    anchors.leftMargin: 6
                    anchors.top: parent.top
                    anchors.topMargin: 4
                    visible: menu.modelVision[rowItem.index]
                    tone: "neutral"
                    text: qsTr("视觉")
                }

                // 档位胶囊：点即「切到该模型 + 应用该档位」
                Row {
                    id: levelRow
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.s2
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

                    Repeater {
                        model: rowItem.levels

                        Rectangle {
                            id: levelChip
                            required property string modelData

                            readonly property bool active: rowItem.current
                                                            && Chat.thinkingLevel === levelChip.modelData

                            width: levelText.implicitWidth + 14
                            height: 22
                            radius: Theme.rPill
                            color: levelChip.active ? Theme.accent : Theme.overlayHover
                            border.width: 1
                            border.color: levelChip.active ? Theme.accent : Theme.hairline

                            Text {
                                id: levelText
                                anchors.centerIn: parent
                                text: menu.levelLabel(levelChip.modelData)
                                color: levelChip.active ? Theme.accentInk : Theme.textDim
                                font.pixelSize: Theme.fLabel
                                font.weight: Font.Medium
                            }

                            HoverHandler { id: levelHover }

                            TapHandler {
                                onTapped: {
                                    Chat.setModelId(rowItem.id)
                                    Chat.setThinkingLevel(levelChip.modelData)
                                    menu.close()
                                }
                            }
                        }
                    }
                }

                HoverHandler { id: rowHover }

                TapHandler {
                    onTapped: Chat.setModelId(rowItem.id)
                }
            }

            Text {
                anchors.centerIn: parent
                visible: modelList.count === 0
                text: menu.modelNames.length === 0 ? qsTr("该厂商无内置模型，可在下方手填模型名")
                                                   : qsTr("暂无模型")
                color: Theme.textGhost
                font.pixelSize: Theme.fSmall
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.hairline }

        // ------------------------------------------------------------ 联网搜索（入口，第三级弹层）
        Row {
            width: parent.width
            spacing: Theme.s2

            MLabel {
                label: qsTr("联网搜索")
                anchors.verticalCenter: parent.verticalCenter
            }

            MChip {
                dot: true
                tone: !Chat.webSearchEnabled ? "neutral"
                                             : (Chat.webSearchReady ? "ok" : "accent")
                text: Chat.webSearchSearching ? qsTr("正在联网搜索…")
                      : (!Chat.webSearchEnabled ? qsTr("已关闭")
                                                : (Chat.webSearchReady ? qsTr("已就绪")
                                                                       : qsTr("未就绪")))
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: Chat.webSearchNative ? qsTr("内置源 · 零配置") : qsTr("自定义源")
                color: Theme.textGhost
                font.pixelSize: Theme.fLabel
                anchors.verticalCenter: parent.verticalCenter
            }

            MButton {
                text: qsTr("配置…")
                variant: "quiet"
                small: true
                anchors.verticalCenter: parent.verticalCenter
                onClicked: menu.configureRequested()
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.hairline }

        // ------------------------------------------------------------ 高级原生参数
        Row {
            width: parent.width
            spacing: Theme.s2

            MLabel {
                label: qsTr("高级原生参数")
                anchors.verticalCenter: parent.verticalCenter
            }

            MChip {
                tone: "neutral"
                text: qsTr("当前生效：%1").arg(JSON.stringify(Chat.effectiveParams()))
                maxWidth: Math.max(90, body.width * 0.45)
                anchors.verticalCenter: parent.verticalCenter
            }

            MButton {
                text: qsTr("重置")
                variant: "quiet"
                small: true
                anchors.verticalCenter: parent.verticalCenter
                onClicked: {
                    advancedEdit.text = "{}"
                    advancedError.text = ""
                    Chat.clearAdvancedParams()
                }
            }

            MButton {
                text: qsTr("应用")
                variant: "ghost"
                small: true
                anchors.verticalCenter: parent.verticalCenter
                onClicked: {
                    const raw = advancedEdit.text.trim()
                    if (raw.length === 0 || raw === "{}") {
                        advancedError.text = ""
                        Chat.clearAdvancedParams()
                        return
                    }
                    try {
                        const parsed = JSON.parse(raw)
                        if (parsed === null || typeof parsed !== "object" || Array.isArray(parsed))
                            throw new Error(qsTr("需要 JSON 对象"))
                        Chat.setAdvancedParams(parsed)
                        advancedError.text = ""
                    } catch (error) {
                        advancedError.text = qsTr("JSON 解析失败：%1").arg(error.message)
                    }
                }
            }
        }

        TextArea {
            id: advancedEdit
            width: parent.width
            height: 96
            wrapMode: TextEdit.WrapAnywhere
            color: Theme.text
            placeholderTextColor: Theme.textGhost
            placeholderText: qsTr('例如 {"reasoning_effort":"high"}；留空或 {} 表示回到档位映射')
            font.pixelSize: Theme.fSmall
            selectByMouse: true
            leftPadding: 10
            rightPadding: 10
            topPadding: 8
            bottomPadding: 8

            background: Rectangle {
                radius: Theme.rMd
                color: Theme.ink1
                border.width: 1
                border.color: advancedEdit.activeFocus ? Theme.accentHair : Theme.hairline
            }
        }

        Text {
            id: advancedError
            width: parent.width
            color: Theme.danger
            font.pixelSize: Theme.fSmall
            wrapMode: Text.Wrap
            visible: text.length > 0
        }

        Text {
            width: parent.width
            text: qsTr("档位来自厂商官方文档的能力声明；高级参数会直接透传厂商原生字段并覆盖档位映射。")
            color: Theme.textGhost
            font.pixelSize: Theme.fLabel
            wrapMode: Text.Wrap
        }
        }
    }
}
