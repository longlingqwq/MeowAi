import QtQuick
import QtQuick.Controls
import QtQuick.Effects

/**
 * 联网搜索（第三级弹层）：**默认不显眼**——从「模型与档位」二级菜单里的「配置…」进入。
 *
 * 纯展示与绑定：所有状态都来自 `Chat`（ChatController），本文件不解析 providers.json、
 * 不碰网络、不碰密钥明文（密钥输入与配置卡里同一条规则：只写 SecureStore）。
 *
 * 两级来源：
 *  - 内置源（本厂商原生能力）：**零配置**，直接复用该厂商 API Key，无需用户填写任何东西；
 *  - 自定义源：仅在厂商无内置搜索时作为后备，需填端点与密钥。
 */
Popup {
    id: panel

    /// 在触发器下方弹出，并尽量不出右边界（与二级菜单同一套定位约定）
    function openAt(anchorItem, hostItem) {
        const point = anchorItem.mapToItem(hostItem, 0, anchorItem.height + 6)
        x = Math.max(8, Math.min(point.x, hostItem.width - width - 8))
        y = Math.max(8, point.y)
        open()
    }

    implicitWidth: 486
    implicitHeight: body.implicitHeight + Theme.s4 * 2
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

    Column {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: Theme.s4
        anchors.leftMargin: Theme.s4
        anchors.rightMargin: Theme.s4
        spacing: Theme.s3

        // ------------------------------------------------------------ 头部
        Item {
            width: parent.width
            height: Math.max(30, headRow.height)

            Row {
                id: headRow
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.s2

                MLabel {
                    label: qsTr("联网搜索")
                    anchors.verticalCenter: parent.verticalCenter
                }

                MChip {
                    tone: "accent"
                    text: Chat.providerId
                    anchors.verticalCenter: parent.verticalCenter
                }

                MChip {
                    dot: true
                    tone: !Chat.webSearchReady ? "danger"
                                               : (Chat.webSearchSearching ? "accent" : "ok")
                    text: Chat.webSearchSearching ? qsTr("正在搜索…")
                                                  : (Chat.webSearchReady ? qsTr("已就绪")
                                                                         : qsTr("未就绪"))
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            MButton {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("关闭")
                variant: "quiet"
                small: true
                onClicked: panel.close()
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.hairline }

        // ------------------------------------------------------------ 总开关
        Row {
            width: parent.width
            spacing: Theme.s2

            MLabel {
                label: qsTr("智能联网")
                anchors.verticalCenter: parent.verticalCenter
            }

            MSwitch {
                id: webSwitch
                checked: Chat.webSearchEnabled
                onToggled: (value) => Chat.webSearchEnabled = value
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: Chat.webSearchEnabled
                      ? qsTr("开启后：模型按需自行联网，需要时才搜索")
                      : qsTr("关闭后：与普通对话完全一致")
                color: Chat.webSearchEnabled ? Theme.accentHi : Theme.textGhost
                font.pixelSize: Theme.fSmall
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Text {
            width: parent.width
            text: qsTr("开启后，联网搜索会作为「工具」交给模型：模型自己判断是否需要上网，需要时自动搜索并把网页资料用于作答；不需要时就不会联网。")
            color: Theme.textGhost
            font.pixelSize: Theme.fLabel
            wrapMode: Text.Wrap
        }

        Rectangle { width: parent.width; height: 1; color: Theme.hairline }

        // ------------------------------------------------------------ 搜索源说明
        Row {
            width: parent.width
            spacing: Theme.s2

            MLabel {
                label: qsTr("搜索源")
                anchors.verticalCenter: parent.verticalCenter
            }

            MChip {
                tone: Chat.webSearchNative ? "ok" : "neutral"
                text: Chat.webSearchNative ? qsTr("本厂商内置搜索") : qsTr("自定义搜索源")
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: Chat.webSearchNative ? qsTr("零配置：复用该厂商 API Key")
                                           : qsTr("需填写下方端点与密钥")
                color: Theme.textGhost
                font.pixelSize: Theme.fLabel
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Text {
            width: parent.width
            visible: Chat.webSearchNative
            text: qsTr("该厂商内置了联网搜索能力，且鉴权复用你已配置的厂商 API Key——无需任何额外配置。")
            color: Theme.textGhost
            font.pixelSize: Theme.fLabel
            wrapMode: Text.Wrap
        }

        // ------------------------------------------------------------ 自定义源（后备）
        Text {
            width: parent.width
            visible: !Chat.webSearchNative
            text: qsTr("当前厂商没有内置搜索能力。作为后备，可填写一个返回 JSON 的搜索端点；不填则联网功能不可用。")
            color: Theme.textGhost
            font.pixelSize: Theme.fLabel
            wrapMode: Text.Wrap
        }

        Row {
            width: parent.width
            spacing: Theme.s3

            MLabel {
                label: qsTr("端点")
                anchors.verticalCenter: parent.verticalCenter
            }

            MField {
                id: webEndpointField
                width: 360
                text: Chat.webSearchEndpoint
                placeholderText: qsTr("https://搜索端点?q={query}（须返回 JSON 结果）")
                onEditingFinished: Chat.webSearchEndpoint = text
            }
        }

        Row {
            width: parent.width
            spacing: Theme.s3

            MLabel {
                label: qsTr("密钥")
                anchors.verticalCenter: parent.verticalCenter
            }

            MField {
                id: webKeyField
                width: 220
                echoMode: TextInput.Password
                placeholderText: Chat.dataStoreAvailable
                                 ? qsTr("仅加密存入本机保险库")
                                 : qsTr("本平台不支持加密存储")
                enabled: Chat.dataStoreAvailable
            }

            MButton {
                text: qsTr("保存")
                variant: "primary"
                small: true
                enabled: Chat.dataStoreAvailable && webKeyField.text.length > 0
                anchors.verticalCenter: parent.verticalCenter
                onClicked: {
                    if (Chat.saveWebSearchKey(webKeyField.text))
                        webKeyField.text = ""
                }
            }

            MButton {
                text: qsTr("清除")
                variant: "quiet"
                small: true
                enabled: Chat.dataStoreAvailable && Chat.hasWebSearchKey
                anchors.verticalCenter: parent.verticalCenter
                onClicked: Chat.eraseWebSearchKey()
            }

            MChip {
                visible: Chat.hasWebSearchKey
                tone: "ok"
                text: qsTr("已存密钥")
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Text {
            width: parent.width
            visible: Chat.webSearchMessage.length > 0
            text: Chat.webSearchMessage
            color: Theme.warn
            font.pixelSize: Theme.fSmall
            wrapMode: Text.Wrap
        }

        Text {
            width: parent.width
            text: qsTr("提示：开关打开但当前设置不可用时，提问会**自动降级为普通对话**，并在错误横幅里说明原因。")
            color: Theme.textGhost
            font.pixelSize: Theme.fLabel
            wrapMode: Text.Wrap
        }
    }

    // 状态回同步（避免用户拖动开关后绑定被打断）
    Connections {
        target: Chat

        function onWebSearchChanged() {
            webSwitch.checked = Chat.webSearchEnabled
            webEndpointField.text = Chat.webSearchEndpoint
        }
    }
}
