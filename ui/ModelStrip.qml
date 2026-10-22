import QtQuick
import QtQuick.Controls
import QtQuick.Window

/**
 * 模型条（次顶部 Dock 内的独立组件）：
 * 模型名触发器 + 「配置」按钮 + 二级菜单 `ModelMenu` + 三级弹层 `WebSearchPanel`。
 *
 * 由外壳（M1）实例化在次顶部 Dock 中，**不依赖 ConversationPage 的内部 id**：
 * 弹层宿主固定取窗口 contentItem，因此不会被 Dock / 父容器的 clip 裁剪。
 *
 * 纯展示与绑定：厂商与模型数据全部来自 `Chat`，本文件不解析 providers.json、
 * 不碰网络与密钥。常态只显示当前模型名；厂商名只出现在展开的接入配置面板里。
 *
 * 「配置」按钮只抛信号，展开 / 收起由外壳决定（`conversationConfigOpen`）。
 */
Item {
    id: strip

    /// 用户点击「配置」时发出，由外壳决定展开还是收起接入配置面板
    signal configureRequested()

    /// 弹层宿主：窗口根 Item；拿不到窗口时退化为自身
    readonly property var hostItem: strip.Window.window ? strip.Window.window.contentItem : strip

    implicitWidth: layout.implicitWidth
    implicitHeight: layout.implicitHeight
    width: implicitWidth
    height: implicitHeight

    function openModelMenu() {
        modelMenu.openAt(pill, strip.hostItem)
    }

    Row {
        id: layout
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.s2

        Rectangle {
            id: pill
            anchors.verticalCenter: parent.verticalCenter
            width: pillText.width + Theme.s3 * 2
            height: 26
            radius: Theme.rPill
            color: (pillHover.hovered || modelMenu.visible) ? Theme.accentGhost
                                                            : Theme.overlayHover
            border.width: 1
            border.color: (pillHover.hovered || modelMenu.visible) ? Theme.accentHair
                                                                   : Theme.hairline

            Behavior on color {
                ColorAnimation { duration: Theme.dFast }
            }

            Text {
                id: pillText
                anchors.centerIn: parent
                // 模型名过长时按 220 截断，胶囊不再随名字无限拉宽
                width: Math.min(implicitWidth, 220)
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                text: Chat.modelId.length > 0 ? Chat.modelId : qsTr("未选择模型")
                color: (pillHover.hovered || modelMenu.visible) ? Theme.accentHi : Theme.textDim
                font.pixelSize: Theme.fLabel
                font.weight: Font.Medium
            }

            HoverHandler {
                id: pillHover
                onHoveredChanged: {
                    if (hovered)
                        hoverTimer.restart()
                    else
                        hoverTimer.stop()
                }
            }

            TapHandler {
                onTapped: strip.openModelMenu()
                onLongPressed: strip.openModelMenu()
            }
        }

        // 「配置」与模型名同款胶囊：常态带底色与描边，不再是一段裸文字
        Rectangle {
            id: configButton
            anchors.verticalCenter: parent.verticalCenter
            width: configText.width + Theme.s3 * 2
            height: 26
            radius: Theme.rPill
            color: configHover.hovered ? Theme.accentGhost : Theme.overlayFaint
            border.width: 1
            border.color: configHover.hovered ? Theme.accentHair : Theme.hairline

            Behavior on color {
                ColorAnimation { duration: Theme.dFast }
            }
            Behavior on border.color {
                ColorAnimation { duration: Theme.dFast }
            }

            Text {
                id: configText
                anchors.centerIn: parent
                text: qsTr("配置")
                color: configHover.hovered ? Theme.accentHi : Theme.textDim
                font.pixelSize: Theme.fLabel
                font.weight: Font.Medium
            }

            HoverHandler { id: configHover }

            TapHandler {
                cursorShape: Qt.PointingHandCursor
                onTapped: strip.configureRequested()
            }
        }
    }

    Timer {
        id: hoverTimer
        interval: 1000
        onTriggered: strip.openModelMenu()
    }

    ModelMenu {
        id: modelMenu
        onConfigureRequested: {
            modelMenu.close()
            webPanel.openAt(pill, strip.hostItem)
        }
    }

    WebSearchPanel {
        id: webPanel
    }
}
