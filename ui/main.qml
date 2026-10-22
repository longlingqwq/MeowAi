import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import MeowAi

/**
 * 应用外壳（Nocturne / 琥珀猫眼）。
 *
 * 只负责"舞台"与导航：背景氛围、自绘标题栏、左侧导航栏、顶栏、页面切换、资料库抽屉、开屏覆盖层。
 * 页面本身（ConversationPage / PersonalizationPage / LibraryPage）各自独立，互不引用。
 * 业务全部经 App / Chat / History / Personalize / ThemePreset / Library 转发，本文件不含业务逻辑。
 *
 * 外壳状态（同时是 M2/M3 的接口契约，不写入配置文件）：
 *   pageIndex            0 对话 / 1 个性化 / 2 资料库
 *   navCollapsed         左侧导航栏是否缩回为窄条
 *   conversationExpanded 对话导航项下方的会话列表是否展开
 *   libraryConfigOpen    资料库右侧配置抽屉是否打开
 *   conversationConfigOpen 对话页顶部内联接入配置面板是否展开（离开对话页即关闭）
 */
Window {
    id: root

    // 初始尺寸按可用屏幕收缩；x/y 再夹一次，避免窗口落在屏幕外导致"底部看不到"
    readonly property real screenW: Screen.desktopAvailableWidth
    readonly property real screenH: Screen.desktopAvailableHeight

    width: Math.min(1180, screenW - 80)
    height: Math.min(760, screenH - 80)
    x: Math.max(8, (screenW - width) / 2)
    y: Math.max(8, (screenH - height) / 2)
    minimumWidth: Math.min(820, Math.max(480, screenW - 40))
    minimumHeight: Math.min(560, Math.max(420, screenH - 40))
    visible: true
    title: App.appName + " · " + App.platformId
    // 透明底 + 内部遮罩裁剪出圆角；系统原生边框与投影已由无边框自绘标题栏放弃
    color: "transparent"

    /// 屏幕可用区域很小时直接最大化：由系统按真实工作区计算，避免底部超出屏幕
    Component.onCompleted: {
        if (screenW < 1200 || screenH < 820)
            visibility = Window.Maximized
    }

    // 切换显示器/远程桌面时把窗口收回可视区域
    Connections {
        target: root
        function onScreenChanged() { root.clampToScreen() }
    }

    /// 把窗口收回到当前屏幕内（远程桌面/投屏切换分辨率后也能自动恢复）
    function clampToScreen() {
        if (visibility === Window.Maximized || visibility === Window.FullScreen)
            return
        if (width > screenW - 16)
            width = Math.max(480, screenW - 80)
        if (height > screenH - 16)
            height = Math.max(420, screenH - 80)
        if (x + width > screenW - 8)
            x = Math.max(8, screenW - width - 8)
        if (y + height > screenH - 8)
            y = Math.max(8, screenH - height - 8)
    }

    /// 桌面端用无边框自绘标题栏；Android / Web 回退原生 chrome（见 PlatformAdapter）
    flags: App.customTitleBar ? (Qt.Window | Qt.FramelessWindowHint) : Qt.Window

    property int pageIndex: 0
    property bool navCollapsed: false
    property bool conversationExpanded: true
    property bool libraryConfigOpen: false
    /// 对话页内联接入配置面板：由次顶部 Dock 的「配置」按钮开合，切页时强制关闭
    property bool conversationConfigOpen: false
    /// 开屏是否已消费掉（避免改完设置后反复自动播放）
    property bool splashConsumed: false
    /// 是否为「手动预览」触发的一次播放
    property bool previewing: false
    /// 自绘标题栏高度：非桌面端为 0，此时内容整体上移
    readonly property int titleBarHeight: App.customTitleBar ? 38 : 0
    /// 窗口圆角：最大化 / 全屏时归零，避免与工作区边界冲突
    readonly property int stageRadius: (visibility === Window.Maximized
                                        || visibility === Window.FullScreen) ? 0 : Theme.rLg

    // 圆角裁剪容器：Rectangle 的 radius 不会裁剪子项，必须用 MultiEffect 遮罩，
    // 否则四角只会被背景色盖住、而不是真正透出桌面。
    Item {
        id: stageRoot
        anchors.fill: parent
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: stageMask
        }

        // ================================================================= 舞台
        // 1) 墨色渐变底
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.stageTop }
                GradientStop { position: 0.52; color: Theme.stageMid }
                GradientStop { position: 1.0; color: Theme.stageBottom }
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

            // ------------------------------------------------ 自绘标题栏（阶段三）
            TitleBar {
                id: titleBar
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: root.titleBarHeight
                visible: App.customTitleBar
                targetWindow: root
                pageTitle: root.pageIndex === 0 ? qsTr("对话")
                           : (root.pageIndex === 1 ? qsTr("个性化") : qsTr("资料库"))
            }

            // ------------------------------------------------ 左侧导航栏（可缩回）
            Sidebar {
                id: sidebar
                anchors.left: parent.left
                anchors.top: titleBar.bottom
                anchors.bottom: parent.bottom

                pageIndex: root.pageIndex
                navCollapsed: root.navCollapsed
                conversationExpanded: root.conversationExpanded

                onPageRequested: (index) => {
                    root.pageIndex = index
                    if (index !== 0)
                        root.conversationConfigOpen = false
                    if (index !== 2)
                        root.libraryConfigOpen = false
                }
                onNavCollapsedRequested: (collapsed) => root.navCollapsed = collapsed
                onConversationExpandedRequested: (expanded) => root.conversationExpanded = expanded
                onNewConversationRequested: History.newConversation()
            }

            // ------------------------------------------------ 右侧内容区
            Column {
                anchors.left: sidebar.right
                anchors.right: parent.right
                anchors.top: titleBar.bottom
                anchors.bottom: parent.bottom
                spacing: 0

                // 次顶部 Dock
                Item {
                    id: topBar
                    width: parent.width
                    height: 68

                    // 标题行：对话页的模型条紧随标题右侧，不再贴窗口右边缘
                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.s5
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.s4

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2

                            Text {
                                text: root.pageIndex === 0 ? qsTr("对话")
                                      : (root.pageIndex === 1 ? qsTr("个性化") : qsTr("资料库"))
                                color: Theme.text
                                font.pixelSize: Theme.fH1
                                font.weight: Font.DemiBold
                            }

                            // 对话页不再显示副标题；其余页面保留一行说明
                            Text {
                                visible: root.pageIndex !== 0
                                text: root.pageIndex === 1
                                      ? qsTr("主题 · 字体 · 背景 · 头像 · 开屏")
                                      : qsTr("小库 · 关联文件夹 · 检索与成本")
                                color: Theme.textFaint
                                font.pixelSize: Theme.fSmall
                            }
                        }

                        // 对话页专属：模型名胶囊 + 配置按钮（内部直接绑定 Chat）
                        ModelStrip {
                            visible: root.pageIndex === 0
                            anchors.verticalCenter: parent.verticalCenter
                            onConfigureRequested: root.conversationConfigOpen = !root.conversationConfigOpen
                        }
                    }

                    Row {
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.s5
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.s3

                        // 资料库页专属：打开右侧配置抽屉
                        MButton {
                            visible: root.pageIndex === 2
                            text: qsTr("配置")
                            variant: "quiet"
                            small: true
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: root.libraryConfigOpen = !root.libraryConfigOpen
                        }

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
                            // 顶部不留外边距：消息流可一直滚到 Dock 下沿，滚动时不出空白带
                            anchors.leftMargin: Theme.s5
                            anchors.rightMargin: Theme.s5
                            anchors.bottomMargin: Theme.s5
                            anchors.topMargin: 0
                            configOpen: root.conversationConfigOpen
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

                    Item {
                        id: libraryHost

                        LibraryPage {
                            anchors.fill: parent
                        }
                    }
                }
            }

            // ------------------------------------------------ 资料库配置抽屉（右侧）
            // 只在内容区（标题栏之下、侧栏之右）展开，不遮挡标题栏。
            Item {
                id: drawerLayer
                anchors.left: sidebar.right
                anchors.right: parent.right
                anchors.top: titleBar.bottom
                anchors.bottom: parent.bottom
                clip: true
                z: 600

                Rectangle {
                    id: drawerBackdrop
                    anchors.fill: parent
                    color: Theme.scrim
                    opacity: root.libraryConfigOpen ? 1.0 : 0.0

                    Behavior on opacity {
                        NumberAnimation { duration: Theme.dBase }
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: root.libraryConfigOpen
                        onClicked: root.libraryConfigOpen = false
                    }
                }

                Rectangle {
                    id: configDrawer
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: Math.min(440, drawerLayer.width - Theme.s7)
                    x: root.libraryConfigOpen ? drawerLayer.width - width : drawerLayer.width
                    color: Theme.ink2
                    border.width: 1
                    border.color: Theme.hairlineStrong
                    // 贴满内容区，只圆左侧上下两角；右侧与舞台边缘齐平
                    topLeftRadius: Theme.rLg
                    bottomLeftRadius: Theme.rLg

                    Behavior on x {
                        NumberAnimation {
                            duration: Theme.dBase
                            easing.type: Theme.easeInOut
                        }
                    }

                    // 吞掉抽屉内空白点击，避免穿透到遮罩把抽屉误关
                    MouseArea {
                        anchors.fill: parent
                    }

                    Item {
                        id: drawerHeader
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        height: 52

                        MLabel {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.s4
                            anchors.verticalCenter: parent.verticalCenter
                            label: qsTr("资料库配置")
                        }

                        MButton {
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.s3
                            anchors.verticalCenter: parent.verticalCenter
                            width: 30
                            height: 30
                            text: "\u2715"
                            variant: "quiet"
                            small: true
                            onClicked: root.libraryConfigOpen = false
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            color: Theme.hairline
                        }
                    }

                    // M3 的配置面板：占位阶段已具备 open / closeRequested() 契约
                    LibraryConfigPanel {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: drawerHeader.bottom
                        anchors.bottom: parent.bottom
                        anchors.margins: Theme.s4
                        open: root.libraryConfigOpen
                        onCloseRequested: root.libraryConfigOpen = false
                    }
                }
            }

            // Esc 关闭资料库抽屉（仅在抽屉打开时生效）
            Shortcut {
                sequence: "Esc"
                enabled: root.libraryConfigOpen
                onActivated: root.libraryConfigOpen = false
            }

            // ------------------------------------------------ 无边框窗口缩放边框
            // Windows 由 platform 层原生助手接管 WM_NCHITTEST；
            // 其它桌面平台在此用 QWindow::startSystemResize（槽，QML 可直接调用）兜底。
            Item {
                id: resizeFrame
                anchors.fill: parent
                visible: App.customTitleBar
                z: 1000

                MouseArea {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 6
                    cursorShape: Qt.SizeHorCursor
                    onPressed: root.startSystemResize(Qt.LeftEdge)
                }
                MouseArea {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 6
                    cursorShape: Qt.SizeHorCursor
                    onPressed: root.startSystemResize(Qt.RightEdge)
                }
                MouseArea {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 6
                    cursorShape: Qt.SizeVerCursor
                    onPressed: root.startSystemResize(Qt.TopEdge)
                }
                MouseArea {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 6
                    cursorShape: Qt.SizeVerCursor
                    onPressed: root.startSystemResize(Qt.BottomEdge)
                }
                // 四角热区向内收 8px：圆角处的像素是透明的，贴边放置会点不到
                MouseArea {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: 8
                    anchors.topMargin: 8
                    width: 6
                    height: 6
                    cursorShape: Qt.SizeFDiagCursor
                    onPressed: root.startSystemResize(Qt.TopEdge | Qt.LeftEdge)
                }
                MouseArea {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.rightMargin: 8
                    anchors.topMargin: 8
                    width: 6
                    height: 6
                    cursorShape: Qt.SizeBDiagCursor
                    onPressed: root.startSystemResize(Qt.TopEdge | Qt.RightEdge)
                }
                MouseArea {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 8
                    anchors.bottomMargin: 8
                    width: 6
                    height: 6
                    cursorShape: Qt.SizeBDiagCursor
                    onPressed: root.startSystemResize(Qt.BottomEdge | Qt.LeftEdge)
                }
                MouseArea {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.rightMargin: 8
                    anchors.bottomMargin: 8
                    width: 6
                    height: 6
                    cursorShape: Qt.SizeFDiagCursor
                    onPressed: root.startSystemResize(Qt.BottomEdge | Qt.RightEdge)
                }
            }
        }

        // ================================================================= 开屏
        Loader {
            id: splashLoader
            anchors.fill: parent
            // 用户设置过自定义开屏视频走视频开屏；否则按当前主题播放内置开屏
            source: Personalize.splashSource !== "" ? "SplashVideo.qml" : "SplashIntro.qml"
            visible: active
            active: root.previewing
                    || (!root.splashConsumed && Personalize.splashEnabled)
        }
    }

    // 圆角遮罩：MultiEffect 只读取它的 alpha 通道；色值用令牌只为保证不透明，
    // 它被整个舞台盖住，不会出现在界面上。
    Rectangle {
        id: stageMask
        anchors.fill: stageRoot
        radius: root.stageRadius
        color: Theme.ink0
        z: -1
        layer.enabled: true
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
