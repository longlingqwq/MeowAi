import QtQuick
import QtQuick.Controls

/**
 * 对话页：连接设置（可折叠）+ 消息流 + 输入条。
 *
 * 纯展示与绑定，业务全部经 Chat（ChatController）转发，本文件不碰网络与密钥。
 *
 * 【临时方案】core 层目前只暴露单个流式的 replyText，没有历史消息模型，
 * 因此这里用 QML 侧 ListModel 维护"展示用 transcript"（进程内有效，重启即清空）。
 * 待 core 提供真实会话历史后替换，已记入 docs/DEVLOG.md。
 */
Item {
    id: page

    ListModel { id: transcript }

    /// 气泡最大宽度：任何一行都不该横贯整个窗口
    readonly property real bubbleMaxWidth: Math.min(680, width * 0.8)

    /// 组装一次发送：先落用户气泡，再按是否带图调用对应接口
    function submit() {
        const text = composerField.text.trim()
        if (text.length === 0 || Chat.busy)
            return

        const imagePath = imageField.text.trim()
        transcript.append({
                              "role": "user",
                              "text": imagePath.length > 0 ? text + "\n[附图] " + imagePath : text
                          })

        if (imagePath.length > 0)
            Chat.sendWithImage(text, imagePath)
        else
            Chat.send(text)

        composerField.text = ""
        transcriptView.positionViewAtEnd()
    }

    Connections {
        target: Chat

        // 一轮结束：把完整回复固化进 transcript
        function onBusyChanged() {
            if (!Chat.busy && Chat.replyText.length > 0) {
                transcript.append({ "role": "assistant", "text": Chat.replyText })
                transcriptView.positionViewAtEnd()
            }
        }

        // 流式增量：保持视图贴底
        function onReplyTextChanged() {
            if (Chat.busy)
                transcriptView.positionViewAtEnd()
        }
    }

    // ---------------------------------------------------------- 连接设置卡
    MSurface {
        id: connCard
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: connCard.expanded ? 190 : 56
        clip: true

        property bool expanded: false

        Behavior on height {
            NumberAnimation { duration: Theme.dBase; easing.type: Theme.easeInOut }
        }

        // ---- 头部（始终可见）----
        Item {
            id: connHead
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 56

            Row {
                anchors.left: parent.left
                anchors.leftMargin: Theme.s4
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.s2

                MLabel {
                    label: qsTr("接入")
                    anchors.verticalCenter: parent.verticalCenter
                }

                MChip {
                    tone: "accent"
                    text: Chat.providerId
                    anchors.verticalCenter: parent.verticalCenter
                }

                MChip {
                    dot: true
                    tone: Chat.hasApiKey ? "ok" : "danger"
                    text: Chat.hasApiKey ? qsTr("密钥已就绪") : qsTr("未配置密钥")
                    anchors.verticalCenter: parent.verticalCenter
                }

                MChip {
                    visible: !Chat.dataStoreAvailable
                    tone: "danger"
                    text: qsTr("本平台不支持加密存储")
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            MButton {
                anchors.right: parent.right
                anchors.rightMargin: Theme.s4
                anchors.verticalCenter: parent.verticalCenter
                text: connCard.expanded ? qsTr("收起") : qsTr("配置")
                variant: "quiet"
                small: true
                onClicked: connCard.expanded = !connCard.expanded
            }
        }

        // ---- 展开体 ----
        Column {
            id: connBody
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: connHead.bottom
            anchors.leftMargin: Theme.s4
            anchors.rightMargin: Theme.s4
            spacing: Theme.s3
            opacity: connCard.expanded ? 1 : 0

            Behavior on opacity {
                NumberAnimation { duration: Theme.dBase }
            }

            Row {
                spacing: Theme.s3

                MLabel {
                    label: qsTr("厂商")
                    anchors.verticalCenter: parent.verticalCenter
                }

                MCombo {
                    id: providerBox
                    width: 150
                    model: ["openai", "anthropic"]
                    currentIndex: Chat.providerId === "anthropic" ? 1 : 0
                    onActivated: (index) => Chat.setProvider(index === 1 ? "anthropic" : "openai")
                }

                MLabel {
                    label: qsTr("模型")
                    anchors.verticalCenter: parent.verticalCenter
                }

                MField {
                    id: modelField
                    width: 260
                    text: Chat.model
                    placeholderText: qsTr("模型名")
                    onEditingFinished: Chat.model = text
                }
            }

            Row {
                spacing: Theme.s3

                MLabel {
                    label: qsTr("密钥")
                    anchors.verticalCenter: parent.verticalCenter
                }

                MField {
                    id: keyField
                    width: 320
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
            }

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
        }
    }

    // 外部源变化时回同步（避免用户操作打断绑定后状态漂移）
    Connections {
        target: Chat

        function onProviderChanged() {
            providerBox.currentIndex = Chat.providerId === "anthropic" ? 1 : 0
        }
        function onModelChanged() {
            if (modelField.text !== Chat.model)
                modelField.text = Chat.model
        }
        function onImagesConfirmedChanged() {
            imageConfirmSwitch.checked = Chat.imagesConfirmed
        }
        function onApiKeyStateChanged() {
            if (!Chat.dataStoreAvailable)
                keyField.text = ""
        }
    }

    // ---------------------------------------------------------- 底部区
    Column {
        id: bottomArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: Theme.s3

        // 错误横幅：只在有错误时占位
        MSurface {
            id: errorBanner
            width: parent.width
            height: 42
            visible: Chat.lastError.length > 0
            radius: Theme.rMd
            fill: Theme.dangerGhost
            stroke: "#3DE5605A"

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
                    text: Chat.lastError
                    color: Theme.danger
                    font.pixelSize: Theme.fSmall
                    elide: Text.ElideRight
                    maximumLineCount: 2
                    wrapMode: Text.Wrap
                    anchors.verticalCenter: parent.verticalCenter
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

                    MField {
                        id: composerField
                        width: parent.width - sendButton.width
                               - (cancelButton.visible ? cancelButton.width + parent.spacing : 0)
                               - parent.spacing
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
                        placeholderText: qsTr("图片路径（可选）")
                        enabled: !Chat.busy
                    }

                    Text {
                        text: Chat.imagesConfirmed
                              ? qsTr("已确认：该图片会被上传")
                              : qsTr("未确认：该图片不会被上传")
                        color: Chat.imagesConfirmed ? Theme.warn : Theme.textGhost
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
        anchors.topMargin: Theme.s4
        anchors.bottomMargin: Theme.s4
        clip: true
        spacing: 0
        boundsBehavior: Flickable.StopAtBounds
        model: transcript

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
            required property string text

            width: ListView.view.width
            height: bubble.height + Theme.s4

            readonly property bool isUser: wrapper.role === "user"
            readonly property real maxWidth: Math.min(680, wrapper.width * 0.8)

            // 用于测量单行自然宽度（自身不受宽度约束）
            Text {
                id: measureText
                visible: false
                text: wrapper.text
                font.pixelSize: Theme.fBody
            }

            Rectangle {
                id: bubble
                readonly property int hPad: 16
                readonly property int vPad: 12
                readonly property int railWidth: wrapper.isUser ? 0 : 10

                width: Math.min(wrapper.maxWidth,
                                Math.ceil(measureText.implicitWidth) + bubble.hPad * 2 + bubble.railWidth)
                height: roleTag.implicitHeight + 6 + bodyText.implicitHeight + bubble.vPad * 2
                anchors.top: parent.top
                anchors.left: wrapper.isUser ? undefined : parent.left
                anchors.right: wrapper.isUser ? parent.right : undefined
                radius: Theme.rLg
                color: wrapper.isUser ? Theme.ink3 : "#0AFFFFFF"
                border.width: wrapper.isUser ? 1 : 0
                border.color: Theme.hairline

                // 助手侧：一道琥珀竖线，作为"AI 的声音"的视觉锚
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
                }

                MLabel {
                    id: roleTag
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: bubble.hPad + bubble.railWidth
                    anchors.topMargin: bubble.vPad
                    label: wrapper.isUser ? qsTr("你") : qsTr("MeowAi")
                    tone: wrapper.isUser ? Theme.textGhost : Theme.accent
                }

                Text {
                    id: bodyText
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: roleTag.bottom
                    anchors.leftMargin: bubble.hPad + bubble.railWidth
                    anchors.rightMargin: bubble.hPad
                    anchors.topMargin: 6
                    text: wrapper.text
                    color: Theme.text
                    font.pixelSize: Theme.fBody
                    lineHeight: 1.55
                    wrapMode: Text.Wrap
                }
            }
        }

        // 流式气泡：本轮生成期间贴在末尾
        footer: Item {
            id: streamFooter
            width: transcriptView.width
            height: streamBubble.height + Theme.s4
            visible: Chat.busy

            property real pulse: 1.0

            SequentialAnimation on pulse {
                running: Chat.busy
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 0.3; duration: 620; easing.type: Easing.InOutQuad }
                NumberAnimation { from: 0.3; to: 1.0; duration: 620; easing.type: Easing.InOutQuad }
            }

            Rectangle {
                id: streamBubble
                readonly property int hPad: 16
                readonly property int vPad: 12
                readonly property int railWidth: 10

                width: Math.min(page.bubbleMaxWidth,
                                Math.ceil(streamMeasure.implicitWidth) + streamBubble.hPad * 2
                                + streamBubble.railWidth)
                height: streamTag.implicitHeight + 6 + streamBody.implicitHeight + streamBubble.vPad * 2
                anchors.top: parent.top
                anchors.left: parent.left
                radius: Theme.rLg
                color: "#0AFFFFFF"

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.topMargin: 6
                    anchors.bottomMargin: 6
                    width: 2
                    radius: 1
                    color: Theme.accent
                    opacity: streamFooter.pulse
                }

                MLabel {
                    id: streamTag
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: streamBubble.hPad + streamBubble.railWidth
                    anchors.topMargin: streamBubble.vPad
                    label: qsTr("MeowAi")
                    tone: Theme.accent
                }

                Text {
                    id: streamMeasure
                    visible: false
                    text: streamBody.text
                    font.pixelSize: Theme.fBody
                }

                Text {
                    id: streamBody
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: streamTag.bottom
                    anchors.leftMargin: streamBubble.hPad + streamBubble.railWidth
                    anchors.rightMargin: streamBubble.hPad
                    anchors.topMargin: 6
                    text: Chat.replyText.length > 0 ? Chat.replyText : qsTr("正在思考…")
                    color: Chat.replyText.length > 0 ? Theme.text : Theme.textFaint
                    font.pixelSize: Theme.fBody
                    lineHeight: 1.55
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    // 空态
    Item {
        anchors.fill: transcriptView
        visible: transcript.count === 0 && !Chat.busy
        enabled: false

        EyeMark {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: -48
            width: 56
            height: 56
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
}
