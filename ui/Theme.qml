pragma Singleton
import QtQuick

/**
 * 全局设计令牌（Nocturne / 琥珀猫眼）。
 *
 * 设计立意：深夜墨蓝的舞台 + 唯一一道暖琥珀强调色（猫眼反光）。
 * 大量发丝级描边、宽阔字距的大写微标签、柔光晕背景、克制的负空间——
 * 与「本地优先、私密、安静」的产品性格一致。
 *
 * 【阶段三 · 主题系统】这里的令牌全部是**可变属性**（非 readonly）：
 * core/ThemePresetManager 会用 setProperty 逐个写入所选主题预设的值，
 * 由于 QML 属性自带变更通知，界面绑定会即时刷新，实现零代码换肤。
 * 令牌名是**全局契约**：只允许新增，不允许改动已有名字；新增令牌必须同步
 * 补进 resources/themes 下的**所有**预设，否则该主题会漏色（会被 lastError 暴露）。
 *
 * 字体族刻意不在此处指定：字体由 core/ThemeManager 全局驱动，
 * 用户自定义字体必须优先（见功能 #11），层级只靠字号 / 字重 / 字距表达。
 */
QtObject {
    // ------------------------------------------------------------------ 色
    property color ink0: "#0B0D11"   // 舞台底色
    property color ink1: "#101319"   // 输入 / 凹槽
    property color ink2: "#151922"   // 卡片表面
    property color ink3: "#1C212C"   // 悬浮 / 抬升
    property color ink4: "#242A36"   // 高亮叠层

    property color hairline: "#14FFFFFF"        // 8% 发丝线
    property color hairlineStrong: "#26FFFFFF"  // 15%

    property color text: "#ECEEF3"
    property color textDim: "#A2A9B6"
    property color textFaint: "#6C7482"
    property color textGhost: "#454C59"

    property color accent: "#F2A63C"        // 琥珀（猫眼）
    property color accentHi: "#FFC46B"
    property color accentDeep: "#8A5A14"
    property color accentGhost: "#24F2A63C" // 14% 叠层
    property color accentHair: "#59F2A63C"  // 35% 描边
    property color accentInk: "#1A1005"     // 琥珀之上的深色文字

    property color ok: "#7FC8A0"
    property color warn: "#E8B86B"
    property color danger: "#E5605A"
    property color dangerGhost: "#24E5605A"

    property color glowWarm: "#38F2A63C"  // 背景暖光晕
    property color glowCool: "#2B5B7FB5"  // 背景冷光晕
    property color scrim: "#B00B0D11"     // 自定义背景之上的压暗层

    // --- 阶段三新增：把原先散落在各组件里的硬编码色收归为语义令牌 ---
    property color overlayFaint: "#0DFFFFFF"        // 导航项悬停
    property color overlaySubtle: "#0AFFFFFF"       // 助手气泡底
    property color overlayHover: "#14FFFFFF"        // 次级悬停 / 中性胶囊
    property color overlayStrong: "#26FFFFFF"       // 按压叠层
    property color shadow: "#A6000000"              // 卡片投影
    property color accentShadow: "#4DF2A63C"        // 主行动外发光
    property color dangerPress: "#33E5605A"         // 危险按压
    property color dangerHair: "#3DE5605A"          // 危险描边
    property color dangerHairStrong: "#66E5605A"    // 危险描边（悬停）
    property color okGhost: "#1F7FC8A0"
    property color okHair: "#3D7FC8A0"
    property color navRail: "#99080A0E"             // 左侧导航栏底
    property color stageTop: "#12161E"              // 舞台渐变
    property color stageMid: "#0E1118"
    property color stageBottom: "#08090D"
    property color splashScrimTop: "#8C08090D"      // 开屏上下压暗层
    property color splashScrimBottom: "#D908090D"

    // ---------------------------------------------------------------- 圆角
    property int rSm: 6
    property int rMd: 10
    property int rLg: 16
    property int rXl: 22
    property int rPill: 999

    // ---------------------------------------------------------------- 间距
    property int s1: 4
    property int s2: 8
    property int s3: 12
    property int s4: 16
    property int s5: 24
    property int s6: 32
    property int s7: 48

    // ---------------------------------------------------------------- 字号
    property int fDisplay: 34
    property int fH1: 22
    property int fH2: 17
    property int fBody: 14
    property int fSmall: 12
    property int fLabel: 11

    // ---------------------------------------------------------------- 动效
    property int dFast: 120
    property int dBase: 200
    property int dSlow: 420

    property int easeOut: Easing.OutQuint
    property int easeInOut: Easing.InOutCubic

    // ------------------------------------------------- 兼容旧令牌（勿删）
    // 已有 QML 可能仍引用这几个名字，统一指向新的语义令牌；
    // 它们**不进入**主题预设，靠绑定自动跟随令牌变化。
    readonly property color background: ink0
    readonly property color surface: ink2
    readonly property color foreground: text
    readonly property color muted: textDim
    readonly property int spacing: s3
}
