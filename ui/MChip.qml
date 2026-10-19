import QtQuick

/**
 * 胶囊状态标签：极细描边 + 低饱和底色 + 可选状态点。
 * tone: "neutral" | "accent" | "ok" | "danger"
 */
Rectangle {
    id: root

    property string text: ""
    property string tone: "neutral"
    property bool dot: false

    implicitWidth: row.implicitWidth + Theme.s3
    implicitHeight: 22
    radius: Theme.rPill
    color: {
        switch (root.tone) {
        case "accent": return Theme.accentGhost
        case "ok":     return Theme.okGhost
        case "danger": return Theme.dangerGhost
        default:       return Theme.overlayHover
        }
    }
    border.width: 1
    border.color: {
        switch (root.tone) {
        case "accent": return Theme.accentHair
        case "ok":     return Theme.okHair
        case "danger": return Theme.dangerHair
        default:       return Theme.hairline
        }
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6

        Rectangle {
            visible: root.dot
            anchors.verticalCenter: parent.verticalCenter
            width: 5
            height: 5
            radius: 3
            color: {
                switch (root.tone) {
                case "ok":     return Theme.ok
                case "danger": return Theme.danger
                case "accent": return Theme.accent
                default:       return Theme.textFaint
                }
            }
        }

        Text {
            text: root.text
            color: {
                switch (root.tone) {
                case "accent": return Theme.accentHi
                case "ok":     return Theme.ok
                case "danger": return Theme.danger
                default:       return Theme.textDim
                }
            }
            font.pixelSize: Theme.fLabel
            font.weight: Font.Medium
            font.letterSpacing: 0.6
            verticalAlignment: Text.AlignVCenter
        }
    }
}
