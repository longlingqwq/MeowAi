pragma Singleton
import QtQuick

/**
 * 全局设计令牌（Nocturne / 琥珀猫眼）。
 *
 * 设计立意：深夜墨蓝的舞台 + 唯一一道暖琥珀强调色（猫眼反光）。
 * 大量发丝级描边、宽阔字距的大写微标签、柔光晕背景、克制的负空间——
 * 与「本地优先、私密、安静」的产品性格一致。
 *
 * 字体族刻意不在此处指定：字体由 core/ThemeManager 全局驱动，
 * 用户自定义字体必须优先（见功能 #11），层级只靠字号 / 字重 / 字距表达。
 */
QtObject {
    // ------------------------------------------------------------------ 色
    readonly property color ink0: "#0B0D11"   // 舞台底色
    readonly property color ink1: "#101319"   // 输入 / 凹槽
    readonly property color ink2: "#151922"   // 卡片表面
    readonly property color ink3: "#1C212C"   // 悬浮 / 抬升
    readonly property color ink4: "#242A36"   // 高亮叠层

    readonly property color hairline: "#14FFFFFF"        // 8% 发丝线
    readonly property color hairlineStrong: "#26FFFFFF"  // 15%

    readonly property color text: "#ECEEF3"
    readonly property color textDim: "#A2A9B6"
    readonly property color textFaint: "#6C7482"
    readonly property color textGhost: "#454C59"

    readonly property color accent: "#F2A63C"        // 琥珀（猫眼）
    readonly property color accentHi: "#FFC46B"
    readonly property color accentDeep: "#8A5A14"
    readonly property color accentGhost: "#24F2A63C" // 14% 叠层
    readonly property color accentHair: "#59F2A63C"  // 35% 描边
    readonly property color accentInk: "#1A1005"     // 琥珀之上的深色文字

    readonly property color ok: "#7FC8A0"
    readonly property color warn: "#E8B86B"
    readonly property color danger: "#E5605A"
    readonly property color dangerGhost: "#24E5605A"

    readonly property color glowWarm: "#38F2A63C"  // 背景暖光晕
    readonly property color glowCool: "#2B5B7FB5"  // 背景冷光晕
    readonly property color scrim: "#B00B0D11"     // 自定义背景之上的压暗层

    // ---------------------------------------------------------------- 圆角
    readonly property int rSm: 6
    readonly property int rMd: 10
    readonly property int rLg: 16
    readonly property int rXl: 22
    readonly property int rPill: 999

    // ---------------------------------------------------------------- 间距
    readonly property int s1: 4
    readonly property int s2: 8
    readonly property int s3: 12
    readonly property int s4: 16
    readonly property int s5: 24
    readonly property int s6: 32
    readonly property int s7: 48

    // ---------------------------------------------------------------- 字号
    readonly property int fDisplay: 34
    readonly property int fH1: 22
    readonly property int fH2: 17
    readonly property int fBody: 14
    readonly property int fSmall: 12
    readonly property int fLabel: 11

    // ---------------------------------------------------------------- 动效
    readonly property int dFast: 120
    readonly property int dBase: 200
    readonly property int dSlow: 420

    readonly property int easeOut: Easing.OutQuint
    readonly property int easeInOut: Easing.InOutCubic

    // ------------------------------------------------- 兼容旧令牌（勿删）
    // 已有 QML 可能仍引用这几个名字，统一指向新的语义令牌。
    readonly property color background: ink0
    readonly property color surface: ink2
    readonly property color foreground: text
    readonly property color muted: textDim
    readonly property int spacing: s3
}
