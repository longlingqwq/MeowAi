import QtQuick
import QtQuick.Window

/**
 * 无边框自绘标题栏（阶段三）。
 *
 * 只负责窗口 chrome：拖拽、最小化 / 最大化 / 关闭，纯展示与交互、不含业务逻辑。
 * 窗口引用由外层 main.qml 经 targetWindow 注入；非桌面端不占用高度（App.customTitleBar 为 false）。
 *
 * 拖拽用 QWindow::startSystemMove()（QWindow 的槽，QML 可直接调用），
 * 因此即使在无边框窗口上也不会出现「无法拖动」；关闭按钮直连 close()，永远可用。
 *
 * 三枚窗口按钮共用 46×38 命中区与 12×12 图标画布，图标一律用矩形绘制，
 * 不依赖字体字形：最小化 / 最大化 / 还原 / 关闭的视觉重量与居中完全一致，
 * 最大化与还原切换时画布尺寸不变，因此不会产生布局位移。
 */
Item {
    id: bar

    /// 目标窗口（由 main.qml 注入 root）
    property var targetWindow: null
    /// 左侧副标题（当前页名）
    property string pageTitle: ""
    /// 最近一次「最大化 / 全屏态按下即还原」的时间戳，用于抑制紧随其后的双击翻转
    property double lastRestoreTime: 0

    implicitHeight: 38

    Rectangle {
        anchors.fill: parent
        color: Theme.navRail

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.hairline
        }
    }

    // ---------------------------------------------------------------- 拖拽区
    // 覆盖到控制按钮左侧；控制按钮声明在后 → 叠在其上，点击不会被吞掉。
    MouseArea {
        id: dragArea
        anchors.left: parent.left
        anchors.right: controls.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        acceptedButtons: Qt.LeftButton
        onPressed: (mouse) => bar.beginWindowDrag(mouse.x)
        // 最大化 / 全屏态按下即已还原，此时的双击不再翻转状态，避免“还原后又被最大化”
        onDoubleClicked: {
            if (Date.now() - bar.lastRestoreTime < 600)
                return
            bar.toggleMaximize()
        }
    }

    // ------------------------------------------------------------------ 品牌
    Row {
        anchors.left: parent.left
        anchors.leftMargin: Theme.s4
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.s2

        BrandLogo {
            width: 18
            height: 18
            markOnly: true
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            text: "MeowAi"
            color: Theme.text
            font.pixelSize: Theme.fSmall
            font.weight: Font.DemiBold
            font.letterSpacing: 1.6
            anchors.verticalCenter: parent.verticalCenter
        }

        Rectangle {
            visible: bar.pageTitle.length > 0
            width: 1
            height: 14
            color: Theme.hairline
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            visible: bar.pageTitle.length > 0
            text: bar.pageTitle
            color: Theme.textFaint
            font.pixelSize: Theme.fSmall
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    // ------------------------------------------------------------ 窗口控制
    Row {
        id: controls
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom

        CapButton {
            iconKind: "minimize"
            onActivated: if (bar.targetWindow) bar.targetWindow.showMinimized()
        }

        CapButton {
            iconKind: "maximize"
            maximized: bar.targetWindow
                       && (bar.targetWindow.visibility === Window.Maximized
                           || bar.targetWindow.visibility === Window.FullScreen)
            onActivated: bar.toggleMaximize()
        }

        CapButton {
            iconKind: "close"
            danger: true
            onActivated: if (bar.targetWindow) bar.targetWindow.close()
        }
    }

    /**
     * 标题栏按下：普通态直接进入系统拖动；
     * 最大化 / 全屏态系统会忽略拖动，因此先把窗口还原成普通尺寸，
     * 再按鼠标在标题栏内的横向比例反算 x，使光标落在还原后标题栏的同一相对位置，
     * 最后才进入系统拖动。y 不参与换算，由 showNormal() 恢复到普通态位置。
     */
    function beginWindowDrag(mouseX) {
        if (!bar.targetWindow)
            return

        var win = bar.targetWindow
        var vis = win.visibility
        if (vis !== Window.Maximized && vis !== Window.FullScreen) {
            win.startSystemMove()
            return
        }

        // 按下的横向比例（restore 后按它反推窗口 x，使鼠标仍指向同一位置）
        var ratio = bar.width > 0 ? Math.max(0, Math.min(1, mouseX / bar.width)) : 0.5
        // 最大化 / 全屏态下 root.x 即工作区左边界，全局鼠标 x 约为 root.x + mouseX
        var globalMouseX = win.x + mouseX

        win.showNormal()
        bar.lastRestoreTime = Date.now()

        // showNormal() 之后 width 才回到普通尺寸，换算与拖动放到下一轮事件循环
        Qt.callLater(function () {
            if (!bar.targetWindow)
                return

            var normalWidth = win.width
            var clampLeft = 8
            var clampRight = Math.max(clampLeft, win.screenW - normalWidth - 8)
            win.x = Math.max(clampLeft, Math.min(globalMouseX - ratio * normalWidth, clampRight))
            // 触底 / 触右时按 main.qml 的 clampToScreen 规则再夹一次
            if (typeof win.clampToScreen === "function")
                win.clampToScreen()
            win.startSystemMove()
        })
    }

    /// 最大化 / 还原切换（全屏态先回到普通窗口，与「还原」图标语义一致）
    function toggleMaximize() {
        if (!bar.targetWindow)
            return
        if (bar.targetWindow.visibility === Window.Maximized
                || bar.targetWindow.visibility === Window.FullScreen)
            bar.targetWindow.showNormal()
        else
            bar.targetWindow.showMaximized()
    }

    component CapButton: Item {
        id: cap

        /// "minimize" | "maximize" | "close"
        property string iconKind: "minimize"
        property bool danger: false
        /// 仅 maximize 使用：true 时画「还原」双层方块
        property bool maximized: false

        signal activated()

        implicitWidth: 46
        implicitHeight: 38

        readonly property color iconColor: cap.danger
                                           ? Theme.danger
                                           : (capHover.hovered ? Theme.text : Theme.textDim)

        Rectangle {
            anchors.fill: parent
            color: capHover.hovered
                   ? (cap.danger ? Theme.dangerGhost : Theme.overlayHover)
                   : "transparent"

            Behavior on color {
                ColorAnimation { duration: Theme.dFast }
            }
        }

        // 统一图标画布：三种按钮共用 12×12 中心区域，字形不参与布局计算
        Item {
            anchors.centerIn: parent
            width: 12
            height: 12

            // 最小化：一条横线
            Rectangle {
                visible: cap.iconKind === "minimize"
                anchors.centerIn: parent
                width: 10
                height: 1
                color: cap.iconColor
            }

            // 最大化：单层方框
            Rectangle {
                visible: cap.iconKind === "maximize" && !cap.maximized
                anchors.centerIn: parent
                width: 10
                height: 10
                color: "transparent"
                border.width: 1
                border.color: cap.iconColor
            }

            // 还原：两个 8×8 方框叠放，整体仍收在 12×12 画布内
            Rectangle {
                visible: cap.iconKind === "maximize" && cap.maximized
                x: 4
                y: 0
                width: 8
                height: 8
                color: "transparent"
                border.width: 1
                border.color: cap.iconColor
            }

            Rectangle {
                visible: cap.iconKind === "maximize" && cap.maximized
                x: 0
                y: 4
                width: 8
                height: 8
                color: "transparent"
                border.width: 1
                border.color: cap.iconColor
            }

            // 关闭：两条对角线
            Rectangle {
                visible: cap.iconKind === "close"
                anchors.centerIn: parent
                width: 11
                height: 1
                color: cap.iconColor
                rotation: 45
            }

            Rectangle {
                visible: cap.iconKind === "close"
                anchors.centerIn: parent
                width: 11
                height: 1
                color: cap.iconColor
                rotation: -45
            }
        }

        HoverHandler { id: capHover }
        TapHandler { onTapped: cap.activated() }
    }
}
