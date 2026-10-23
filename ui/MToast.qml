import QtQuick
import QtQuick.Controls

/**
 * 单条 Toast（M15）：只负责视觉、出现 / 消失动画与计时，不管理堆叠。
 *
 * 约定：
 *  - 视觉沿用设计令牌：浮层色 Theme.ink3、直角 Theme.rMd、1px Theme.hairlineStrong；
 *    错误态文字与图标用 Theme.danger，普通提示用 Theme.text；
 *  - 宽度随文案自适应，上限由宿主的 maxWidth 传入（抽屉打开时会收窄）；
 *    最多两行，超出省略，悬停 ToolTip 看完整文案；
 *  - 出现 200ms 淡入 + 自上向下位移 8px；消失 160ms 淡出；
 *  - 默认 3.2 秒自动消失，指针悬停时暂停，移开后按剩余时间继续；
 *  - 点击本体立即开始关闭；不设置 focus，不改变页面布局与滚动位置。
 *
 * 堆叠、去重、最多三条与页面切换清理由 main.qml 的宿主负责。
 */
Item {
    id: root

    /// 展示文案（空串由宿主兜底成「操作失败，请重试」）
    property string message: ""
    /// 是否错误态：影响文字、图标与闪烁叠层
    property bool isError: false
    /// 最大宽度（抽屉打开或窗口变窄时由宿主收窄）
    property int maxWidth: 560

    /// 淡出结束、可以移出宿主列表
    signal closed()

    /// 是否正在关闭（宿主据此判断重复文案能否复用本条）
    readonly property bool closing: closeState.closing

    readonly property int padH: Theme.s4
    readonly property int padV: Theme.s2
    readonly property int iconSize: 18
    readonly property int defaultLife: 3200

    width: maxWidth
    height: surface.height

    // 采用单独的 QtObject 承载关闭状态，避免 readonly 属性被外部赋值
    QtObject {
        id: closeState
        property bool closing: false
    }

    /// 剩余自动关闭时间（悬停暂停时保存，移开后继续）
    property int remainingMs: defaultLife
    property double startedAt: 0

    /// 重新计时；flash 为 true 时轻微闪一下（相同文案 2 秒内重复触发）
    function restart(flash) {
        if (closeState.closing)
            return
        remainingMs = defaultLife
        startedAt = Date.now()
        if (hoverArea.hovered)
            lifeTimer.stop()
        else
            lifeTimer.restart()
        if (flash)
            flashAnim.restart()
    }

    /// 点击本体或计时到达：开始 160ms 淡出
    function dismiss() {
        if (closeState.closing)
            return
        closeState.closing = true
        lifeTimer.stop()
        outro.start()
    }

    Rectangle {
        id: surface
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(root.maxWidth,
                        Math.max(160, textMetrics.width + root.iconSize
                                       + Theme.s2 + root.padH * 2))
        height: Math.max(root.iconSize, body.height) + root.padV * 2
        radius: Theme.rMd
        color: Theme.ink3
        border.width: 1
        border.color: Theme.hairlineStrong
        // 从上方向下位移 8px 落位
        y: -8
        opacity: 0

        // 重复触发的轻微闪动：一次很短的叠层呼吸，不改变文字与布局
        Rectangle {
            id: flashRect
            anchors.fill: parent
            radius: parent.radius
            color: root.isError ? Theme.dangerGhost : Theme.overlayHover
            opacity: 0
        }

        HoverHandler {
            id: hoverArea

            onHoveredChanged: {
                if (hoverArea.hovered) {
                    if (lifeTimer.running) {
                        root.remainingMs = Math.max(200,
                                                   lifeTimer.interval
                                                   - (Date.now() - root.startedAt))
                        lifeTimer.stop()
                    }
                } else if (!closeState.closing) {
                    lifeTimer.interval = root.remainingMs
                    root.startedAt = Date.now()
                    lifeTimer.start()
                }
            }
        }

        // 点击本体立即关闭；不设置 focus，键盘焦点不受影响
        TapHandler {
            onTapped: root.dismiss()
        }

        ToolTip.visible: hoverArea.hovered && messageText.truncated
        ToolTip.text: root.message
        ToolTip.delay: 300

        // 图文区：图标 + 最多两行文案
        Item {
            id: body
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: root.padH
            anchors.rightMargin: root.padH
            anchors.verticalCenter: parent.verticalCenter
            height: Math.max(root.iconSize, messageText.implicitHeight)

            Rectangle {
                id: iconBadge
                width: root.iconSize
                height: root.iconSize
                radius: width / 2
                color: "transparent"
                border.width: 1
                border.color: root.isError ? Theme.danger : Theme.hairlineStrong
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    anchors.centerIn: parent
                    text: root.isError ? "!" : "i"
                    color: root.isError ? Theme.danger : Theme.text
                    font.pixelSize: Theme.fLabel
                    font.weight: Font.Bold
                }
            }

            Text {
                id: messageText
                anchors.left: iconBadge.right
                anchors.leftMargin: Theme.s2
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: root.message
                color: root.isError ? Theme.danger : Theme.text
                font.pixelSize: Theme.fBody
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                lineHeight: 1.35
            }
        }
    }

    /// 文案自然宽度：避免用 messageText.implicitWidth 造成宽高互相依赖
    TextMetrics {
        id: textMetrics
        font: messageText.font
        text: root.message
    }

    Timer {
        id: lifeTimer
        interval: root.defaultLife
        repeat: false
        onTriggered: root.dismiss()
    }

    SequentialAnimation {
        id: intro
        ParallelAnimation {
            NumberAnimation {
                target: surface
                property: "opacity"
                to: 1
                duration: 200
                easing.type: Theme.easeOut
            }
            NumberAnimation {
                target: surface
                property: "y"
                to: 0
                duration: 200
                easing.type: Theme.easeOut
            }
        }
    }

    SequentialAnimation {
        id: outro
        NumberAnimation {
            target: surface
            property: "opacity"
            to: 0
            duration: 160
            easing.type: Easing.OutCubic
        }
        ScriptAction { script: root.closed() }
    }

    SequentialAnimation {
        id: flashAnim
        NumberAnimation {
            target: flashRect
            property: "opacity"
            to: 1
            duration: 70
        }
        NumberAnimation {
            target: flashRect
            property: "opacity"
            to: 0
            duration: 110
        }
    }

    Component.onCompleted: {
        intro.start()
        restart(false)
    }
}
