import QtQuick
import QtQuick.Controls

/**
 * 对话页：**左侧会话列表**（多会话 / 新建 / 切换 / 删除）+ 连接设置（可折叠）+ 消息流 + 输入条。
 *
 * 纯展示与绑定：
 *  - 消息流直接绑定 core 的会话历史模型 `History`（ChatHistoryModel），
 *    持久化、上下文裁剪、流式节流都在 core 层，本文件不碰数据库；
 *  - 发送经 `Chat`（ChatController）转发，本文件不碰网络与密钥。
 *
 * 布局全部在本文件内自由编排（阶段三要求：UI 组件位置可在 ui 模块内自定义）。
 */
Item {
    id: page

    /// 气泡最大宽度：任何一行都不该横贯整个窗口
    readonly property real bubbleMaxWidth: Math.min(620, Math.max(320, page.width - 260) * 0.78)
    /// 统一展示的错误：优先网络 / 密钥错误，其次会话历史错误
    readonly property string errorText: Chat.lastError.length > 0 ? Chat.lastError : History.lastError

    /// 组装一次发送：用户消息由 core 落库入模型，这里只负责转发与清空输入
    function submit() {
        const text = composerField.text.trim()
        if (text.length === 0 || Chat.busy)
            return

        const imagePath = imageField.text.trim()
        if (imagePath.length > 0)
            Chat.sendWithImage(text, imagePath)
        else
            Chat.send(text)

        composerField.text = ""
        transcriptView.positionViewAtEnd()
    }

    // ================================================================= 会话列表
    MSurface {
        id: convPanel
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 208

        MButton {
            id: newConvBtn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: Theme.s3
            anchors.rightMargin: Theme.s3
            anchors.topMargin: Theme.s3
            text: qsTr("＋ 新对话")
            variant: "primary"
            small: true
            onClicked: History.newConversation()
        }

        MLabel {
            id: convLabel
            anchors.left: parent.left
            anchors.top: newConvBtn.bottom
            anchors.leftMargin: Theme.s3
            anchors.topMargin: Theme.s4
            label: qsTr("会话")
            tone: Theme.textGhost
        }

        ListView {
            id: convList
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: convLabel.bottom
            anchors.bottom: parent.bottom
            anchors.leftMargin: Theme.s1
            anchors.rightMargin: Theme.s1
            anchors.topMargin: Theme.s2
            anchors.bottomMargin: Theme.s3
            clip: true
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            model: History.conversations

            ScrollBar.vertical: ScrollBar {
                id: convScroll
                policy: ScrollBar.AsNeeded
                width: 6
                background: Item { }

                contentItem: Rectangle {
                    implicitWidth: 3
                    radius: 2
                    color: convScroll.pressed ? Theme.accent : Theme.hairlineStrong
                    opacity: convScroll.active ? 0.9 : 0.35
                }
            }

            delegate: Item {
                id: convItem
                required property var modelData

                width: ListView.view ? ListView.view.width : 0
                height: 38

                readonly property bool current: History.conversationId === convItem.modelData.id

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.rMd
                    color: convItem.current
                           ? Theme.accentGhost
                           : (convHover.hovered ? Theme.overlayFaint : "transparent")

                    Behavior on color {
                        ColorAnimation { duration: Theme.dFast }
                    }
                }

                Rectangle {
                    visible: convItem.current
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 2
                    height: 16
                    radius: 1
                    color: Theme.accent
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.s2 + 4
                    anchors.right: delBtn.left
                    anchors.rightMargin: Theme.s1
                    anchors.verticalCenter: parent.verticalCenter
                    text: convItem.modelData.title.length > 0
                          ? convItem.modelData.title
                          : qsTr("新对话")
                    color: convItem.current ? Theme.text : Theme.textDim
                    font.pixelSize: Theme.fSmall
                    elide: Text.ElideRight
                }

                Item {
                    id: delBtn
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.s1
                    anchors.verticalCenter: parent.verticalCenter
                    width: 22
                    height: 22
                    visible: convHover.hovered

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.rSm
                        color: delHover.hovered ? Theme.dangerGhost : "transparent"
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "\u2715"
                        color: delHover.hovered ? Theme.danger : Theme.textFaint
                        font.pixelSize: Theme.fSmall
                    }

                    HoverHandler { id: delHover }
                    TapHandler { onTapped: History.removeConversation(convItem.modelData.id) }
                }

                HoverHandler { id: convHover }
                TapHandler { onTapped: History.openConversation(convItem.modelData.id) }
            }

            Text {
                anchors.centerIn: parent
                visible: convList.count === 0
                text: qsTr("暂无会话")
                color: Theme.textGhost
                font.pixelSize: Theme.fSmall
            }
        }
    }

    // ============================================================ 连接设置卡
    MSurface {
        id: connCard
        anchors.left: convPanel.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Theme.s4
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

                MChip {
                    visible: !History.available
                    tone: "danger"
                    text: qsTr("会话历史不可用")
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
        anchors.left: convPanel.right
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.s4
        spacing: Theme.s3

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
        anchors.left: convPanel.right
        anchors.right: parent.right
        anchors.leftMargin: Theme.s4
        anchors.top: connCard.bottom
        anchors.bottom: bottomArea.top
        anchors.topMargin: Theme.s4
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

            width: ListView.view ? ListView.view.width : 0
            height: bubble.height + Theme.s4

            readonly property bool isUser: wrapper.role === "user"
            readonly property bool thinking: wrapper.status === "streaming" && wrapper.content.length === 0
            readonly property real maxWidth: page.bubbleMaxWidth

            /// 流式时竖线呼吸
            property real pulse: 1.0

            SequentialAnimation on pulse {
                running: wrapper.status === "streaming"
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 0.3; duration: 620; easing.type: Easing.InOutQuad }
                NumberAnimation { from: 0.3; to: 1.0; duration: 620; easing.type: Easing.InOutQuad }
            }

            // 用于测量单行自然宽度（自身不受宽度约束）
            Text {
                id: measureText
                visible: false
                text: wrapper.thinking ? qsTr("正在思考…") : wrapper.content
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
                    text: wrapper.thinking ? qsTr("正在思考…") : wrapper.content
                    color: wrapper.thinking ? Theme.textFaint
                                            : (wrapper.status === "error" ? Theme.danger : Theme.text)
                    font.pixelSize: Theme.fBody
                    lineHeight: 1.55
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    // 一轮结束：把视图贴到最新（用户消息由 core 落库入模型，无需在此追加）
    Connections {
        target: Chat

        function onBusyChanged() {
            transcriptView.positionViewAtEnd()
        }

        // 流式增量：保持视图贴底
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
