import QtQuick

/**
 * 品牌标识：按当前主题选用官方 logo。
 *
 * theme1 Nocturne → 琥珀版；theme2 Daylight → 浅蓝版；
 * 未知预设按 Theme.ink0 亮度兜底：偏暗用琥珀版，偏亮用浅蓝版。
 * 与内置开屏（SplashIntro）使用同一套映射规则，避免两处判断漂移。
 *
 * markOnly 用于标题栏、品牌行等紧凑位置：只画猫形与星点，不含 MeowAi 文字。
 * 尺寸由调用方给出，图片按 PreserveAspectFit 等比缩放。
 */
Item {
    id: root

    property bool markOnly: false

    readonly property bool amber: {
        const id = ThemePreset.currentPresetId;
        if (id === "theme1")
            return true;
        if (id === "theme2")
            return false;
        return (Theme.ink0.r * 0.2126 + Theme.ink0.g * 0.7152 + Theme.ink0.b * 0.0722) < 0.5;
    }

    // 注意：qrc 路径必须带 qrc: 前缀。以 ":/" 开头的字符串在 QML 里会被当作
    // 相对 URL 拼到组件所在目录后面（qrc:/qt/qml/MeowAi/ui/:/branding/...），加载失败。
    Image {
        anchors.fill: parent
        source: root.markOnly ? (root.amber ? "qrc:/branding/mark-amber.svg" : "qrc:/branding/mark-blue.svg")
                              : (root.amber ? "qrc:/branding/logo-amber.svg" : "qrc:/branding/logo-blue.svg")
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
    }
}
