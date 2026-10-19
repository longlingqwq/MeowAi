import QtQuick
import QtQuick.Controls
import QtQuick.Effects

/**
 * 按钮：四种变体 + 两种尺寸。
 *
 *   primary  琥珀实心（一屏只应出现一个主行动）
 *   ghost    发丝描边透明底（次级行动）
 *   quiet    无底无边（三级 / 收起 / 图标旁）
 *   danger   危险语义
 *
 * 继承 Button 以复用其 clicked / enabled / focus 语义，只替换外观与内容。
 */
Button {
    id: root

    property string variant: "ghost"
    property bool small: false

    implicitHeight: small ? 30 : 38
    padding: 0
    leftPadding: small ? 12 : 18
    rightPadding: small ? 12 : 18
    topPadding: small ? 5 : 8
    bottomPadding: small ? 5 : 8

    scale: down ? 0.97 : 1.0

    Behavior on scale {
        NumberAnimation { duration: Theme.dFast; easing.type: Theme.easeOut }
    }

    background: Rectangle {
        implicitWidth: 56
        implicitHeight: root.small ? 30 : 38
        radius: Theme.rMd

        color: {
            if (root.variant === "primary")
                return root.down ? Theme.accentDeep : (root.hovered ? Theme.accentHi : Theme.accent)
            if (root.variant === "danger")
                return root.down ? "#33E5605A" : (root.hovered ? Theme.dangerGhost : "transparent")
            if (root.variant === "quiet")
                return root.hovered ? "#14FFFFFF" : "transparent"
            return root.down ? "#26FFFFFF" : (root.hovered ? "#14FFFFFF" : "transparent")
        }
        border.width: (root.variant === "ghost" || root.variant === "danger") ? 1 : 0
        border.color: {
            if (root.variant === "danger")
                return root.hovered ? "#66E5605A" : "#3DE5605A"
            if (root.variant === "ghost")
                return root.hovered ? Theme.hairlineStrong : Theme.hairline
            return "transparent"
        }
        opacity: root.enabled ? 1.0 : 0.45

        Behavior on color {
            ColorAnimation { duration: Theme.dFast }
        }
        Behavior on border.color {
            ColorAnimation { duration: Theme.dFast }
        }

        // 主行动：琥珀外发光，让"唯一一道光"落在它身上
        layer.enabled: root.variant === "primary" && root.enabled
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: "#4DF2A63C"
            shadowBlur: 0.6
            shadowVerticalOffset: 2
        }
    }

    contentItem: Text {
        text: root.text
        color: {
            if (root.variant === "primary")
                return Theme.accentInk
            if (root.variant === "danger")
                return Theme.danger
            return root.enabled ? Theme.text : Theme.textGhost
        }
        font.pixelSize: root.small ? Theme.fSmall : Theme.fBody
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
