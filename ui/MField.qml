import QtQuick
import QtQuick.Controls

/**
 * 输入框：凹槽墨底 + 发丝描边，聚焦时描边点亮为琥珀。
 * 不指定 font.family，跟随全局字体。
 */
TextField {
    id: root

    implicitHeight: 38
    leftPadding: 12
    rightPadding: 12
    topPadding: 8
    bottomPadding: 8

    color: Theme.text
    placeholderTextColor: Theme.textGhost
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentInk
    font.pixelSize: Theme.fBody

    background: Rectangle {
        radius: Theme.rMd
        color: Theme.ink1
        border.width: 1
        border.color: root.activeFocus
                      ? Theme.accentHair
                      : (root.hovered ? Theme.hairlineStrong : Theme.hairline)

        Behavior on border.color {
            ColorAnimation { duration: Theme.dFast }
        }
    }
}
