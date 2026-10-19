import QtQuick

/**
 * 自绘开关：轨道 + 圆钮，点亮为琥珀。
 *
 * `checked` 是自持状态：点击时自行翻转并发出 toggled(checked)。
 * 使用方若需要跟随外部源变化，用 Connections 显式回同步即可。
 */
Item {
    id: root

    property bool checked: false
    property bool enabled: true

    signal toggled(bool checked)

    implicitWidth: 44
    implicitHeight: 26
    opacity: enabled ? 1.0 : 0.4

    Rectangle {
        id: track
        anchors.fill: parent
        radius: height / 2
        color: root.checked ? Theme.accent : Theme.ink1
        border.width: 1
        border.color: root.checked ? Theme.accent : Theme.hairlineStrong

        Behavior on color {
            ColorAnimation { duration: Theme.dBase }
        }
        Behavior on border.color {
            ColorAnimation { duration: Theme.dBase }
        }
    }

    Rectangle {
        id: knob
        width: 18
        height: 18
        radius: 9
        y: (root.height - height) / 2
        x: root.checked ? root.width - width - 4 : 4
        color: root.checked ? Theme.accentInk : Theme.textFaint

        Behavior on x {
            NumberAnimation { duration: Theme.dBase; easing.type: Theme.easeInOut }
        }
        Behavior on color {
            ColorAnimation { duration: Theme.dBase }
        }
    }

    TapHandler {
        enabled: root.enabled
        onTapped: {
            root.checked = !root.checked
            root.toggled(root.checked)
        }
    }
}
