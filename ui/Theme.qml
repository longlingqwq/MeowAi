pragma Singleton
import QtQuick

/**
 * 全局主题令牌（功能 #11 的 QML 侧接入点）。
 * 阶段一只提供只读默认值，后续由 core/ThemeManager 驱动为动态值。
 */
QtObject {
    readonly property color background: "#1e1f26"
    readonly property color surface: "#262832"
    readonly property color foreground: "#f5f5f7"
    readonly property color muted: "#8a8f9c"
    readonly property string fontFamily: ""
    readonly property int spacing: 12
}
