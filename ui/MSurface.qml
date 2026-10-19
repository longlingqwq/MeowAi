import QtQuick
import QtQuick.Effects

/**
 * 基础表面容器：墨色卡片 + 发丝描边 + 可选柔光投影。
 *
 * 刻意继承 Rectangle 而非 Control —— 不接管 children 的默认属性，
 * 可以像普通矩形一样自由摆放子项，也便于用 anchors 做局部布局。
 */
Rectangle {
    id: root

    /// 是否施加柔光投影（抬升感）
    property bool elevated: false
    /// 悬停时描边是否增亮
    property bool interactive: false

    property color fill: Theme.ink2
    property color stroke: Theme.hairline

    color: fill
    border.width: 1
    border.color: interactive && hoverHandler.hovered ? Theme.hairlineStrong : stroke
    radius: Theme.rLg

    Behavior on color {
        ColorAnimation { duration: Theme.dBase }
    }
    Behavior on border.color {
        ColorAnimation { duration: Theme.dFast }
    }

    layer.enabled: elevated
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowColor: "#A6000000"
        shadowBlur: 0.55
        shadowVerticalOffset: 8
        shadowHorizontalOffset: 0
    }

    HoverHandler {
        id: hoverHandler
    }
}
