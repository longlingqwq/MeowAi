import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import MeowAi

/**
 * 应用外壳（Nocturne / 琥珀猫眼）。
 *
 * 只负责"舞台"与导航：背景氛围、左侧导航栏、顶栏、页面切换、开屏覆盖层。
 * 页面本身（ConversationPage / PersonalizationPage）各自独立，互不引用。
 * 业务全部经 App / Chat / Personalize 转发，本文件不含任何业务逻辑。
 */
Window {
    id: root

    width: 1180
    height: 760
    minimumWidth: 900
    minimumHeight: 620
    visible: true
    title: App.appName + " · " + App.platformId
    color: Theme.ink0

    property int pageIndex: 0
    /// 开屏是否已消费掉（避免改完设置后反复自动播放）
    property bool splashConsumed: false
    /// 是否为「手动预览」触发的一次播放
    property bool previewing: false

    // ================================================================= 舞台
    // 1) 墨色渐变底
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#12161E" }
            GradientStop { position: 0.52; color: "#0E1118" }
            GradientStop { position: 1.0; color: "#08090D" }
        }
    }

    // 2) 双色柔光晕：暖（琥珀）在右上，冷在左下，制造纵深
    MGlow {
        width: 640
        height: 640
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: -220
        anchors.topMargin: -280
        color: Theme.glowWarm
        blurAmount: 1.0
    }

    MGlow {
        width: 560
        height: 560
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: -200
        anchors.bottomMargin: -240
        color: Theme.glowCool
        blurAmount: 1.0
    }

    // 3) 用户自定义背景（功能 #11）：覆盖在舞台之上
    Image {
        anchors.fill: parent
        source: Personalize.backgroundSource
        fillMode: Image.PreserveAspectCrop
        visible: source !== ""
        asynchronous: true
    }

    // 4) 自定义背景之上的压暗层：保证前景文字始终可读
    Rectangle {
        anchors.fill: parent
        color: Theme.scrim
        visible: Personalize.backgroundSource !== ""
    }

    // ================================================================= 前景
    Item {
        anchors.fill: parent

        // ------------------------------------------------ 左侧导航栏
        Rectangle {
            id: navRail
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 228
            color: "#99080A0E"

            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: Theme.hairline
            }

            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.s5
                spacing: Theme.s5

                // 品牌
                Column {
                    width: parent.width
                    spacing: Theme.s3

                    Row {
                        spacing: Theme.s2

                        EyeMark {
                            width: 32
                            height: 32
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: "MeowAi"
                            color: Theme.text
                            font.pixelSize: Theme.fH1
                            font.weight: Font.DemiBold
                            font.letterSpacing: 2.2
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MLabel {
                        label: qsTr("本地优先的 AI 助理")
                        tone: Theme.textGhost
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 1
                    color: Theme.hairline
                }

                // 导航项
                Column {
                    width: parent.width
                    spacing: Theme.s1

                    Item {
                        width: parent.width
                        height: 40

                        readonly property bool current: root.pageIndex === 0

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.rMd
                            color: parent.current
                                   ? Theme.accentGhost
                                   : (navChatHover.hovered ? "#0DFFFFFF" : "transparent")

                            Behavior on color {
                                ColorAnimation { duration: Theme.dFast }
                            }
                        }

                        Rectangle {
                            visible: parent.current
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 2
                            height: 18
                            radius: 1
                            color: Theme.accent
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.s3 + 4
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("对话")
                            color: parent.current ? Theme.text : Theme.textDim
                            font.pixelSize: Theme.fBody
                            font.weight: parent.current ? Font.DemiBold : Font.Normal
                        }

                        HoverHandler { id: navChatHover }
                        TapHandler { onTapped: root.pageIndex = 0 }
                    }

                    Item {
                        width: parent.width
                        height: 40

                        readonly property bool current: root.pageIndex === 1

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.rMd
                            color: parent.current
                                   ? Theme.accentGhost
                                   : (navPersoHover.hovered ? "#0DFFFFFF" : "transparent")

                            Behavior on color {
                                ColorAnimation { duration: Theme.dFast }
                            }
                        }

                        Rectangle {
                            visible: parent.current
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 2
                            height: 18
                            radius: 1
                            color: Theme.accent
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.s3 + 4
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("个性化")
                            color: parent.current ? Theme.text : Theme.textDim
                            font.pixelSize: Theme.fBody
                            font.weight: parent.current ? Font.DemiBold : Font.Normal
                        }

                        HoverHandler { id: navPersoHover }
                        TapHandler { onTapped: root.pageIndex = 1 }
                    }
                }
            }

            // 底部：运行环境元信息
            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Theme.s5
                spacing: Theme.s2

                Rectangle {
                    width: parent.width
                    height: 1
                    color: Theme.hairline
                }

                MLabel {
                    label: qsTr("运行平台")
                    tone: Theme.textGhost
                }

                Text {
                    width: parent.width
                    text: App.platformId.toUpperCase()
                    color: Theme.textDim
                    font.pixelSize: Theme.fSmall
                    font.letterSpacing: 1.2
                }

                MLabel {
                    label: qsTr("运行状态")
                    tone: Theme.textGhost
                    anchors.topMargin: Theme.s2
                }

                Text {
                    width: parent.width
                    text: App.statusMessage
                    color: Theme.textFaint
                    font.pixelSize: Theme.fLabel
                    lineHeight: 1.6
                    wrapMode: Text.Wrap
                }
            }
        }

        // ------------------------------------------------ 右侧内容区
        Column {
            anchors.left: navRail.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            spacing: 0

            // 顶栏
            Item {
                id: topBar
                width: parent.width
                height: 68

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.s5
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Text {
                        text: root.pageIndex === 0 ? qsTr("对话") : qsTr("个性化")
                        color: Theme.text
                        font.pixelSize: Theme.fH1
                        font.weight: Font.DemiBold
                    }

                    Text {
                        text: root.pageIndex === 0
                              ? qsTr("与本地助理交谈")
                              : qsTr("字体 · 背景 · 头像 · 开屏")
                        color: Theme.textFaint
                        font.pixelSize: Theme.fSmall
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.s5
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.s3

                    MChip {
                        text: App.platformId
                        tone: "neutral"
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    MAvatar {
                        width: 36
                        height: 36
                        source: Personalize.avatarSource
                        fallbackText: qsTr("猫")
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.hairline
                }
            }

            // 页面栈
            StackLayout {
                id: pageStack
                width: parent.width
                height: parent.height - topBar.height
                currentIndex: root.pageIndex

                Item {
                    id: conversationHost

                    ConversationPage {
                        anchors.fill: parent
                        anchors.margins: Theme.s5
                    }
                }

                Item {
                    id: personalizationHost

                    PersonalizationPage {
                        anchors.fill: parent
                        anchors.margins: Theme.s5
                        onPreviewSplash: {
                            root.splashConsumed = false
                            root.previewing = true
                        }
                    }
                }
            }
        }
    }

    // ================================================================= 开屏
    Loader {
        id: splashLoader
        anchors.fill: parent
        source: "SplashVideo.qml"
        visible: active
        active: root.previewing
                || (!root.splashConsumed && Personalize.splashEnabled && Personalize.splashSource !== "")
    }

    Connections {
        target: splashLoader.item
        ignoreUnknownSignals: true

        function onFinished() {
            root.previewing = false
            root.splashConsumed = true
        }
    }
}
