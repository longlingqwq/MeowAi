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
    // 必须保持透明：窗口颜色会填满整块矩形，圆角遮罩之外的四角会露出这层底色，
    // 开屏底色由 stageRoot 内部的底板负责（会被圆角遮罩一起裁剪）。
    color: "transparent"

    /// 屏幕可用区域很小时直接最大化：由系统按真实工作区计算，避免底部超出屏幕
    Component.onCompleted: {
        if (screenW < 1200 || screenH < 820)
            visibility = Window.Maximized
        // 不开开屏时首帧即显示主内容
        if (!root.splashLayerVisible) {
            root.mainOpacity = 1
            root.mainRevealed = true
        }
    }

    // 切换显示器/远程桌面时把窗口收回可视区域
    Connections {
        target: root
        function onScreenChanged() { root.clampToScreen() }
    }

    /// 离开某页时同步收起该页专属面板：配置抽屉只属于资料库页
    onPageIndexChanged: {
        if (pageIndex !== 0)
            conversationConfigOpen = false
        if (pageIndex !== 2)
            libraryConfigOpen = false
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
    /// 启动阶段是否必须先播放开屏（预览，或首次启动且设置开启）
    readonly property bool startupSplashActive: !previewing && !splashConsumed && Personalize.splashEnabled
    /// 开屏层是否可见：预览与启动开屏共用同一个覆盖层
    readonly property bool splashLayerVisible: previewing || startupSplashActive
    /// 主内容层透明度：开屏期间为 0，交叉淡化结束后置 1
    property real mainOpacity: 0
    /// 主内容层是否已开放交互：开屏结束前为 false
    property bool mainRevealed: false
    /// 开屏配色：与 SplashIntro 的版本映射保持一致（未知主题按亮度兜底）
    readonly property bool splashAmber: {
        const id = ThemePreset.currentPresetId;
        if (id === "theme1")
            return true;
        if (id === "theme2")
            return false;
        return (Theme.ink0.r * 0.2126 + Theme.ink0.g * 0.7152 + Theme.ink0.b * 0.0722) < 0.5;
    }
    /// 构建期窗口底色：内置开屏用品牌底色，自定义视频开屏用黑底
    readonly property color splashBackdropColor: Personalize.splashSource !== ""
                                                  ? "#000000"
                                                  : (splashAmber ? "#0B0D12" : "#FFFFFF")
    /// 自绘标题栏高度：非桌面端为 0，此时内容整体上移
    readonly property int titleBarHeight: App.customTitleBar ? 38 : 0
    /// 窗口圆角：最大化 / 全屏时归零，避免与工作区边界冲突
    readonly property int stageRadius: (visibility === Window.Maximized
                                        || visibility === Window.FullScreen) ? 0 : Theme.rLg

    /// 开屏收尾：跳过与自然结束共用同一条路径——350ms 同曲线交叉淡化
    function crossfadeOutSplash() {
        if (splashCrossfade.running)
            return
        splashLoader.opacity = 1
        splashCrossfade.start()
    }

    /// 交叉淡化结束后才卸载开屏层并开放主内容交互
    function finishSplash() {
        previewing = false
        splashConsumed = true
        mainOpacity = 1
        mainRevealed = true
        splashLoader.opacity = 1
    }

    ParallelAnimation {
        id: splashCrossfade

        NumberAnimation {
            target: splashLoader
            property: "opacity"
            to: 0
            duration: 350
            easing.type: Theme.easeInOut
        }

        NumberAnimation {
            target: root
            property: "mainOpacity"
            to: 1
            duration: 350
            easing.type: Theme.easeInOut
        }

        onFinished: root.finishSplash()
    }

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
        // 主内容层：开屏期间整层透明且不可交互，数据绑定与模型加载照常进行
        Item {
            id: mainLayer
            anchors.fill: parent
            opacity: root.mainOpacity
            enabled: root.mainRevealed

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
                // 新建对话直接切回对话页；先切页再装载，保证停在新会话上
                onNewConversationRequested: {
                    root.pageIndex = 0
                    History.newConversation()
                }
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
                            onToastRequested: (message, isError) => toastLayer.showToast(message, isError)
                        }
                    }

                    Item {
                        id: libraryHost

                        LibraryPage {
                            anchors.fill: parent
                            onToastRequested: (message, isError) => toastLayer.showToast(message, isError)
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
                    // 关闭态不渲染：侧栏宽度动画会让 x 目标随 drawerLayer.width 变化，
                    // 仅靠“移到视口右侧”会被动画滞后扯回视口，露出一条抽屉边缘。
                    visible: root.libraryConfigOpen
                    color: Theme.ink2
                    border.width: 1
                    border.color: Theme.hairlineStrong
                    // 贴满内容区，只圆左侧上下两角；右侧与舞台边缘齐平
                    topLeftRadius: Theme.rLg
                    bottomLeftRadius: Theme.rLg

                    Behavior on x {
                        // 只在打开时做滑入；关闭态侧栏宽度变化直接落到视口外，不产生动画滞后
                        enabled: root.libraryConfigOpen
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

            // ------------------------------------------------ Toast 宿主（M15）
            // 唯一实例：页面只发信号，不各自持有 Toast。位置固定在内容区顶部，
            // 不压标题栏与次顶部 Dock；资料库抽屉打开时把可用宽度让给抽屉左侧。
            Item {
                id: toastLayer
                anchors.left: sidebar.right
                anchors.right: parent.right
                anchors.rightMargin: root.libraryConfigOpen
                                    ? Math.min(440, drawerLayer.width - Theme.s7) : 0
                anchors.top: titleBar.bottom
                anchors.topMargin: topBar.height + Theme.s4
                height: toastColumn.height
                z: 650

                /// 抽屉打开或窗口变窄时同步收窄，避免 Toast 压到抽屉上
                readonly property int toastMaxWidth: Math.max(120,
                    Math.min(560, width - Theme.s4 * 2))

                property string lastToastText: ""
                property double lastToastAt: 0

                /**
                 * 页面统一入口：空文案兜底为「操作失败，请重试」；
                 * 相同文案 2 秒内不新增，只重置计时并轻闪；
                 * 同时最多 3 条，新条插到最上方，超出时挤掉最旧的一条。
                 */
                function showToast(message, isError) {
                    var text = (message === undefined || message === null)
                             ? "" : String(message).trim()
                    if (text.length === 0)
                        text = qsTr("操作失败，请重试")

                    const now = Date.now()
                    if (text === lastToastText && now - lastToastAt <= 2000) {
                        for (let i = 0; i < toastModel.count; ++i) {
                            if (toastModel.get(i).toastText !== text)
                                continue
                            const delegate = toastRepeater.itemAt(i)
                            if (delegate && !delegate.closing) {
                                delegate.restart(true)
                                lastToastAt = now
                                return
                            }
                        }
                    }

                    if (toastModel.count >= 3)
                        toastModel.remove(toastModel.count - 1)
                    toastModel.insert(0, { "toastText": text, "toastError": !!isError })
                    lastToastText = text
                    lastToastAt = now
                }

                ListModel { id: toastModel }

                Column {
                    id: toastColumn
                    anchors.top: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Theme.s2

                    Repeater {
                        id: toastRepeater
                        model: toastModel
                        delegate: MToast {
                            required property int index
                            required property string toastText
                            required property bool toastError

                            maxWidth: toastLayer.toastMaxWidth
                            message: toastText
                            isError: toastError
                            onClosed: toastModel.remove(index)
                        }
                    }
                }

                // 切页即清空：上一页的文件错误不在新页面上残留
                Connections {
                    target: root
                    function onPageIndexChanged() { toastModel.clear() }
                }
            }

            // ------------------------------------------------ 会话激活接线（任务 1）
            // Sidebar 内部的会话列表直接调用 History.openConversation()，外壳拿不到该点击信号；
            // ConversationChanged 在 loadRows() 末尾发出，此时消息已装载完毕，再切回对话页，
            // 因此最终一定停在对话页且消息可见。切页只改视图，不触碰 Chat，不打断流式回答。
            Connections {
                target: History
                function onConversationChanged() { root.pageIndex = 0 }
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
        // 开屏底板：把舞台背景一起盖住，构建期与播放期都保持品牌底色（视频开屏为黑底），
        // 交叉淡化时与开屏层同步淡出，不会出现硬切。
        Rectangle {
            anchors.fill: parent
            color: root.splashBackdropColor
            visible: root.splashLayerVisible
            opacity: splashLoader.opacity
        }

        Loader {
            id: splashLoader
            anchors.fill: parent
            // 用户设置过自定义开屏视频走视频开屏；否则按当前主题播放内置开屏
            source: Personalize.splashSource !== "" ? "SplashVideo.qml" : "SplashIntro.qml"
            // 同步创建：窗口首帧就只有开屏层，不会先露出主界面再叠上来
            asynchronous: false
            visible: active
            active: root.splashLayerVisible
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

        // 自然结束与点击跳过都只发起交叉淡化，收尾统一在 finishSplash 完成
        function onFinished() { root.crossfadeOutSplash() }
    }
}
