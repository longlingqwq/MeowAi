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
 */
Item {
    id: bar

    /// 目标窗口（由 main.qml 注入 root）
    property var targetWindow: null
    /// 左侧副标题（当前页名）
    property string pageTitle: ""

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
        onPressed: if (bar.targetWindow) bar.targetWindow.startSystemMove()
        onDoubleClicked: bar.toggleMaximize()
    }

    // ------------------------------------------------------------------ 品牌
    Row {
        anchors.left: parent.left
        anchors.leftMargin: Theme.s4
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.s2

        EyeMark {
            width: 18
            height: 18
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
            glyph: "\u2014" // —
            onActivated: if (bar.targetWindow) bar.targetWindow.showMinimized()
        }

        CapButton {
            glyph: bar.targetWindow && bar.targetWindow.visibility === Window.Maximized
                   ? "\u2750" // ❐ 还原
                   : "\u25A2" // ▢ 最大化
            onActivated: bar.toggleMaximize()
        }

        CapButton {
            glyph: "\u2715" // ✕
            danger: true
            onActivated: if (bar.targetWindow) bar.targetWindow.close()
        }
    }

    /// 最大化 / 还原切换
    function toggleMaximize() {
        if (!bar.targetWindow)
            return
        if (bar.targetWindow.visibility === Window.Maximized)
            bar.targetWindow.showNormal()
        else
            bar.targetWindow.showMaximized()
    }

    component CapButton: Item {
        id: cap

        property string glyph: ""
        property bool danger: false
        signal activated()

        implicitWidth: 44
        implicitHeight: 38

        Rectangle {
            anchors.fill: parent
            color: capHover.hovered
                   ? (cap.danger ? Theme.dangerGhost : Theme.overlayHover)
                   : "transparent"

            Behavior on color {
                ColorAnimation { duration: Theme.dFast }
            }
        }

        Text {
            anchors.centerIn: parent
            text: cap.glyph
            color: cap.danger ? Theme.danger
                              : (capHover.hovered ? Theme.text : Theme.textDim)
            font.pixelSize: Theme.fSmall

            Behavior on color {
                ColorAnimation { duration: Theme.dFast }
            }
        }

        HoverHandler { id: capHover }
        TapHandler { onTapped: cap.activated() }
    }
}
