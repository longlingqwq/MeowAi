import QtQuick
import QtQuick.Controls

/**
 * 会话列表（从 ConversationPage 抽出的独立组件）。
 *
 * 只负责展示与交互：数据来自 core 的 `History.conversations`；
 * 切换经 `conversationActivated()` 交给外壳（Sidebar 接线到 `History.openConversation()`），
 * 删除直接转发 `History.removeConversation()`；本文件不碰数据库。
 *
 * 新建对话入口由外壳（M1）在侧边栏统一放置，本组件不再内置“新建”按钮；
 * 若外壳需要自行处理新建，可连接 `newConversationRequested()`。
 */
Item {
    id: root

    /// 当前会话 id（默认跟随 History；外部可覆盖以驱动高亮）。
    /// 用 var 承载 qint64，避免 QML int 的 32 位截断。
    property var currentConversationId: History.conversationId

    signal conversationActivated(var conversationId)
    signal newConversationRequested()

    implicitWidth: 208
    implicitHeight: 240

    ListView {
        id: convList
        anchors.fill: parent
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

            readonly property bool current: root.currentConversationId === convItem.modelData.id

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
                // 明显缩进于「对话」一级导航之下；高亮底与选中条仍按委托满宽
                anchors.leftMargin: Theme.s5
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
            TapHandler {
                onTapped: root.conversationActivated(convItem.modelData.id)
            }
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
