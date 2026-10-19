import QtQuick
import QtQuick.Effects

/**
 * 圆形头像：有图时取用户头像并裁成圆形；无图时退化为琥珀首字母徽记。
 */
Item {
    id: root

    property string source: ""
    property string fallbackText: ""

    implicitWidth: 42
    implicitHeight: 42

    readonly property bool hasImage: source !== "" && img.status === Image.Ready

    // 圆形遮罩（供 MultiEffect.maskSource 使用，自身不可见）
    Rectangle {
        id: maskShape
        anchors.fill: parent
        radius: width / 2
        color: "white"
        visible: false
        layer.enabled: true
    }

    Image {
        id: img
        anchors.fill: parent
        source: root.source
        sourceSize.width: Math.round(root.width * 2)
        sourceSize.height: Math.round(root.height * 2)
        fillMode: Image.PreserveAspectCrop
        visible: root.hasImage
        asynchronous: true
        layer.enabled: root.hasImage
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: maskShape
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        visible: !root.hasImage
        color: Theme.accentGhost
        border.width: 1
        border.color: Theme.accentHair

        Text {
            anchors.centerIn: parent
            text: root.fallbackText
            color: Theme.accentHi
            font.pixelSize: Math.round(root.height * 0.4)
            font.weight: Font.DemiBold
        }
    }

    // 统一描边环
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: "transparent"
        border.width: 1
        border.color: Theme.hairlineStrong
    }
}
