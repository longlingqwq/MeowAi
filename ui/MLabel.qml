import QtQuick

/**
 * 大写微标签：宽字距、冷灰、极小字号。
 *
 * 是整个界面版式的节奏基底 —— 分区标题、元信息、字段名统一用它。
 * 不指定 font.family：字体由 core/ThemeManager 全局驱动，用户自定义字体优先。
 */
Text {
    id: root

    /// 原文（自动转大写显示，避免各处手工 toUpperCase）
    property string label: ""

    property color tone: Theme.textFaint

    text: label
    color: tone
    font.pixelSize: Theme.fLabel
    font.weight: Font.DemiBold
    font.capitalization: Font.AllUppercase
    font.letterSpacing: 1.8
    verticalAlignment: Text.AlignVCenter
    renderType: Text.NativeRendering
}
