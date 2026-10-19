import QtQuick
import QtQuick.Effects
import QtQuick.Shapes

/**
 * 品牌标志：猫眼 —— 杏仁状轮廓 + 一道竖瞳高光。
 *
 * 纯矢量绘制，随父项尺寸自适应；颜色可注入。
 * 形状在 100x100 的设计坐标系里画，再整体缩放，避免逐点换算。
 */
Item {
    id: root

    property color ringColor: Theme.accent
    property color pupilColor: Theme.accentHi
    /// 轮廓粗细（相对设计坐标系）
    property real strokeWeight: 6

    implicitWidth: 30
    implicitHeight: 30

    readonly property real unit: Math.min(width, height) / 100

    Shape {
        id: eyeOutline
        width: 100
        height: 100
        anchors.centerIn: parent
        scale: root.unit
        transformOrigin: Item.Center
        antialiasing: true

        ShapePath {
            strokeColor: root.ringColor
            strokeWidth: root.strokeWeight
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            startX: 8
            startY: 50
            PathQuad { x: 92; y: 50; controlX: 50; controlY: 10 }
            PathQuad { x: 8; y: 50; controlX: 50; controlY: 90 }
        }
    }

    // 竖瞳：猫眼的反光
    Rectangle {
        anchors.centerIn: parent
        width: Math.max(2, root.unit * 9)
        height: Math.max(4, root.unit * 28)
        radius: width / 2
        color: root.pupilColor

        layer.enabled: true
        layer.effect: MultiEffect {
            blurEnabled: true
            blur: 0.35
            blurMax: 24
        }
    }
}
