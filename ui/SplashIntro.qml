import QtQuick
import QtQuick.Shapes
import QtQuick.Effects
import QtMultimedia

/**
 * 内置开屏动画（功能 #11）：猫形描边 + 星点 + 品牌文字。
 *
 * 由 assets/splash-src 的两版 HTML 设计稿重写为 QML：
 *  - 两版除配色外结构、时序完全一致（已逐行比对），因此共用本组件，只切调色；
 *  - SVG 路径长度由离线脚本采样计算后固化为字面量（QML 无 getTotalLength）；
 *  - 音效由离线脚本按 Web Audio 语义合成，产物 :/splash/audio/intro.wav，两版共用；
 *  - 2.0s 填充阶段用官方 logo（qrc:/splash/{amber|blue}/cat.svg）淡入覆盖描边，
 *    末帧画面与官方 logo 一致；星点仍按原稿在左上角自绘并保留 2.8s 闪烁。
 *
 * 纯展示组件：版本选择只读 ThemePreset.currentPresetId，不做业务判断；
 * 播放结束或用户跳过时发出 finished，由上层决定隐藏。
 */
Item {
    id: root

    /// 是否播放音效（关闭时静音，动画继续）
    property bool soundEnabled: Personalize.splashSoundEnabled

    /// 总时间轴 3.5s：结束即 finished
    readonly property real timelineMs: 3500

    /// 由单一线性时间轴驱动，所有动效按 HTML 的 CSS 时间表换算
    property real elapsedMs: 0
    property bool done: false

    signal finished

    // ---------------------------------------------------------------- 版本选择
    // theme1 Nocturne 琥珀 → 琥珀深色版；theme2 Daylight 浅蓝 → 浅蓝白色版。
    // 未知预设按 ink0 亮度兜底：偏暗用琥珀版，偏亮用浅蓝版。
    readonly property bool amber: {
        const id = ThemePreset.currentPresetId;
        if (id === "theme1")
            return true;
        if (id === "theme2")
            return false;
        return (Theme.ink0.r * 0.2126 + Theme.ink0.g * 0.7152 + Theme.ink0.b * 0.0722) < 0.5;
    }

    // 品牌固有色（两版设计稿原值），文字色沿用 Theme 令牌以保证可读性
    readonly property color primaryColor: amber ? "#F5A623" : "#65BDE6"
    readonly property color backgroundColor: amber ? "#0B0D12" : "#FFFFFF"

    // ---------------------------------------------------------------- 缓动与时间表
    function clamp01(v) {
        return v < 0 ? 0 : (v > 1 ? 1 : v);
    }

    /// 三次贝塞尔单轴分量（起点 0、终点 1 固定）
    function bezierCoord(u, p1, p2) {
        const mu = 1 - u;
        return 3 * mu * mu * u * p1 + 3 * mu * u * u * p2 + u * u * u;
    }

    /// CSS cubic-bezier(x1,y1,x2,y2)：先按 x 反解参数，再取 y
    function cubicBezier(t, x1, y1, x2, y2) {
        if (t <= 0)
            return 0;
        if (t >= 1)
            return 1;
        let lo = 0;
        let hi = 1;
        let u = t;
        for (let i = 0; i < 20; ++i) {
            const x = bezierCoord(u, x1, x2);
            if (Math.abs(x - t) < 0.00001)
                break;
            if (x < t)
                lo = u;
            else
                hi = u;
            u = (lo + hi) * 0.5;
        }
        return bezierCoord(u, y1, y2);
    }

    /// 背景淡入 0.5s ease-out
    function backgroundOpacity(now) {
        return cubicBezier(clamp01(now / 500), 0.0, 0.0, 0.58, 1.0);
    }

    /// 猫形描边：0.5s + index * 8ms 起，1.38s cubic-bezier(.55,.05,.35,1)
    function drawOffset(index, length, now) {
        const t = (now - (500 + index * 8)) / 1380;
        if (t <= 0)
            return length;
        if (t >= 1)
            return 0;
        return length * (1 - cubicBezier(t, 0.55, 0.05, 0.35, 1.0));
    }

    /// 填充：2.0s 起 0.3s ease
    readonly property real catFill: cubicBezier(clamp01((root.elapsedMs - 2000) / 300), 0.25, 0.10, 0.25, 1.0)

    /// 星点弹出 0.72s keyframes：0% scale 0 → 68% scale 1.2 → 100% scale 1
    function starPopScale(now) {
        const t = clamp01(now / 720);
        if (t < 0.68)
            return 1.2 * cubicBezier(t / 0.68, 0.18, 0.89, 0.32, 1.35);
        return 1.2 - 0.2 * cubicBezier((t - 0.68) / 0.32, 0.18, 0.89, 0.32, 1.35);
    }

    function starPopOpacity(now) {
        const t = clamp01(now / 720);
        if (t < 0.68)
            return clamp01(cubicBezier(t / 0.68, 0.18, 0.89, 0.32, 1.35));
        return 1;
    }

    /// 闪烁：2.8s 起一次 0.7s，四个关键帧之间用 ease-in-out
    function twinkleAt(t) {
        const stops = [[0.0, 1.0, 1.0], [0.24, 0.48, 0.92], [0.47, 1.0, 1.08], [0.72, 0.58, 0.96], [1.0, 1.0, 1.0]];
        const clamped = clamp01(t);
        for (let i = 0; i < stops.length - 1; ++i) {
            const a = stops[i];
            const b = stops[i + 1];
            if (clamped <= b[0]) {
                const u = (clamped - a[0]) / (b[0] - a[0]);
                const e = cubicBezier(u, 0.42, 0.0, 0.58, 1.0);
                return {
                    opacity: a[1] + (b[1] - a[1]) * e,
                    scale: a[2] + (b[2] - a[2]) * e
                };
            }
        }
        return {
            opacity: 1.0,
            scale: 1.0
        };
    }

    function starScale(now) {
        if (now < 720)
            return starPopScale(now);
        if (now < 2800)
            return 1.0;
        return twinkleAt((now - 2800) / 700).scale;
    }

    function starOpacity(now) {
        if (now < 720)
            return starPopOpacity(now);
        if (now < 2800)
            return 1.0;
        return twinkleAt((now - 2800) / 700).opacity;
    }

    /// 文字：2.3s 起 0.5s cubic-bezier(.22,1.35,.45,1)，translateY 20 → 0
    function wordRise(now) {
        return cubicBezier(clamp01((now - 2300) / 500), 0.22, 1.35, 0.45, 1.0);
    }

    // ---------------------------------------------------------------- 生命周期
    function finish() {
        if (done)
            return;
        done = true;
        timeline.stop();
        introAudio.stop();
        finished();
    }

    opacity: 0

    Component.onCompleted: {
        fadeIn.start();
        timeline.start();
        introAudio.play();
    }

    onVisibleChanged: if (!visible)
        introAudio.stop()

    NumberAnimation {
        id: fadeIn
        target: root
        property: "opacity"
        to: 1
        duration: 760
        easing.type: Easing.OutCubic
    }

    NumberAnimation {
        id: timeline
        target: root
        property: "elapsedMs"
        from: 0
        to: root.timelineMs
        duration: root.timelineMs
        easing.type: Easing.Linear
        onFinished: root.finish()
    }

    MediaPlayer {
        id: introAudio
        source: "qrc:/splash/audio/intro.wav"
        audioOutput: AudioOutput {
            muted: !root.soundEnabled
        }
        // 音频失败只静默跳过：开屏不因设备 / 解码问题被阻断
        onErrorOccurred: {}
    }

    // ---------------------------------------------------------------- 画面
    Rectangle {
        anchors.fill: parent
        color: root.backgroundColor
        opacity: root.backgroundOpacity(root.elapsedMs)
    }

    // 设计基准 1920×1080，整体等比缩放；所有动画尺寸都用设计坐标
    Item {
        id: designStage
        width: 1920
        height: 1080
        anchors.centerIn: parent
        scale: Math.min(root.width / width, root.height / height)
        transformOrigin: Item.Center

        Item {
            id: logoWrap
            width: 350
            height: 400
            // HTML: width min(82vw, 390px)，整组上移 1.5vh
            scale: 390 / 350
            transformOrigin: Item.Center
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -16.2

            // 猫形：描边 + 2.0s 填充，发光用 MultiEffect 阴影（替代 CSS drop-shadow）
            Item {
                id: catLayer
                anchors.fill: parent
                // 2.0s 起与官方 logo 交叉淡化：描边动完（约 1.97s）后才淡出
                opacity: 1 - root.catFill
                layer.enabled: true
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: Qt.rgba(root.primaryColor.r, root.primaryColor.g, root.primaryColor.b, 0.55)
                    shadowBlur: 0.85
                    shadowHorizontalOffset: 0
                    shadowVerticalOffset: 0
                    autoPaddingEnabled: true
                }

                Shape {
                    anchors.fill: parent
                    antialiasing: true

                    ShapePath {
                        id: catPath0
                        strokeColor: root.primaryColor
                        strokeStyle: ShapePath.DashLine
                        strokeWidth: 1.45
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin
                        fillColor: Qt.rgba(root.primaryColor.r, root.primaryColor.g, root.primaryColor.b, root.catFill)
                        dashPattern: [951.58, 1903.16]
                        dashOffset: root.drawOffset(0, 951.58, root.elapsedMs)
                        PathSvg {
                            path: "M 84 113 C 85.28 113.19 86.56 113.37 87.88 113.56 C 92.51 115.7 95.48 118.55 98 123 C 99.13 127.97 98.6 131.73 96.44 136.31 C 91.09 142.2 85.01 145.05 77.75 147.88 C 59.87 155.15 47.61 165.03 39.64 183.07 C 33.44 199.51 34.46 217.99 41.55 233.94 C 42.03 234.95 42.51 235.96 43 237 C 43.45 237.96 43.89 238.92 44.36 239.9 C 51.49 253.57 63.81 264.31 77 272 C 76.55 271.29 76.1 270.59 75.63 269.86 C 67.99 256.46 67.45 240.71 71 226 C 79.28 200.78 99.16 184.86 122 173 C 123.66 172.19 125.32 171.39 126.99 170.59 C 136.58 165.99 144.66 161.86 152 154 C 153.29 152.63 154.62 151.28 156 150 C 156.66 150 157.32 150 158 150 C 158.63 161.46 154.58 168.65 147.17 177.08 C 141.87 182.7 135.91 187.24 129.67 191.75 C 126.62 193.97 123.76 196.13 121.14 198.86 C 120.43 199.57 119.73 200.27 119 201 C 118.34 201 117.68 201 117 201 C 116.74 201.59 116.47 202.18 116.2 202.79 C 114.96 205.08 113.63 206.53 111.81 208.38 C 100.69 220.78 98.07 235.61 98.81 251.67 C 99.81 263.9 108.1 273.09 117.03 280.8 C 133.04 292.65 152.72 296.2 172 299 C 172 299.66 172 300.32 172 301 C 133.76 309.67 90.04 298.76 57.19 278.56 C 32.91 262.51 12.62 238.1 6.59 209.15 C 6.3 207.59 6.3 207.59 6 206 C 5.83 205.37 5.65 204.73 5.48 204.08 C 1.16 185.22 6.65 164.36 16 148 C 18.09 144.71 20.35 141.85 23 139 C 23.72 138.19 24.43 137.39 25.17 136.56 C 40.26 120.44 61.69 109.67 84 113 Z"
                        }
                    }
                    ShapePath {
                        id: catPath1
                        strokeColor: root.primaryColor
                        strokeStyle: ShapePath.DashLine
                        strokeWidth: 1.45
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin
                        fillColor: Qt.rgba(root.primaryColor.r, root.primaryColor.g, root.primaryColor.b, root.catFill)
                        dashPattern: [431.17, 862.34]
                        dashOffset: root.drawOffset(1, 431.17, root.elapsedMs)
                        PathSvg {
                            path: "M 269 147 C 272 148 272 148 273.25 150.3 C 273.9 151.79 273.9 151.79 274.56 153.31 C 277.42 159.3 281.02 163.82 285.34 168.81 C 301.49 187.8 306.37 206.28 305 231 C 303.01 249.39 287.32 266.41 273.5 277.56 C 254.45 292.33 226.23 308.15 202 311 C 203.21 307.38 204.02 307.11 207.23 305.25 C 208.11 304.73 208.99 304.21 209.9 303.67 C 210.84 303.12 211.78 302.57 212.75 302 C 221.61 296.67 229.38 290.95 237 284 C 238.29 282.92 239.58 281.83 240.88 280.75 C 242.26 279.51 243.63 278.26 245 277 C 245.53 276.52 246.06 276.03 246.6 275.53 C 259.57 263.53 270.77 249.75 277 233 C 277.29 232.24 277.58 231.47 277.88 230.69 C 285.73 208.74 280.6 188.68 271.27 168.09 C 269.18 163 268.88 158.67 268.94 153.19 C 268.95 152.03 268.96 150.86 268.96 149.67 C 268.98 148.79 268.99 147.91 269 147 Z"
                        }
                    }
                    ShapePath {
                        id: catPath2
                        strokeColor: root.primaryColor
                        strokeStyle: ShapePath.DashLine
                        strokeWidth: 1.45
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin
                        fillColor: Qt.rgba(root.primaryColor.r, root.primaryColor.g, root.primaryColor.b, root.catFill)
                        dashPattern: [326.96, 653.92]
                        dashOffset: root.drawOffset(2, 326.96, root.elapsedMs)
                        PathSvg {
                            path: "M 169.7656 50.7031 C 172.8156 51.9231 175.8756 52.9931 178.9956 54.0031 C 178.9956 54.6631 178.9956 55.3231 178.9956 56.0031 C 176.1356 58.3431 173.6756 59.4831 169.9956 60.0031 C 165.5456 58.9331 161.4456 56.8031 157.3056 54.9031 C 149.2556 51.2131 140.8356 49.4831 131.9956 50.0031 C 129.3556 54.1331 129.5356 58.2731 130.3456 63.0231 C 130.8456 65.2631 131.4056 67.4731 131.9956 69.6831 C 132.3256 70.9031 132.3256 70.9031 132.6556 72.1431 C 135.5356 82.4931 139.0056 94.0031 144.9956 103.0031 C 145.4356 106.1731 145.3356 109.3631 145.3156 112.5631 C 145.3056 113.4731 145.3056 114.3731 145.2956 115.3131 C 145.0956 126.9431 141.5856 137.0031 133.9956 146.0031 C 133.3356 146.0031 132.6756 146.0031 131.9956 146.0031 C 132.0356 144.9931 132.0656 143.9931 132.1056 142.9531 C 132.4656 128.4131 128.8756 116.4931 123.9856 102.8831 C 118.9656 88.5931 115.8256 73.6831 115.8156 58.5031 C 115.7956 57.7931 115.7856 57.0831 115.7756 56.3531 C 115.7556 50.9431 115.8256 44.5931 119.4556 40.2731 C 133.9256 33.0431 156.0756 45.0431 169.7656 50.7031 Z"
                        }
                    }
                    ShapePath {
                        id: catPath3
                        strokeColor: root.primaryColor
                        strokeStyle: ShapePath.DashLine
                        strokeWidth: 1.45
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin
                        fillColor: Qt.rgba(root.primaryColor.r, root.primaryColor.g, root.primaryColor.b, root.catFill)
                        dashPattern: [224.12, 448.24]
                        dashOffset: root.drawOffset(3, 224.12, root.elapsedMs)
                        PathSvg {
                            path: "M 239 5 C 246.52 10.16 248.88 16.82 252 25 C 252.23 25.6 252.46 26.2 252.7 26.81 C 262 50.88 262 50.88 262 56 C 258.67 55.43 257.14 54.6 255 52 C 254.18 50.05 253.44 48.06 252.75 46.06 C 248.37 34.25 248.37 34.25 240.3 24.89 C 239.54 24.27 238.78 23.64 238 23 C 237.69 19.25 237.69 19.25 238 16 C 232.76 18.39 231.16 21.71 228.81 26.69 C 228.3 27.71 228.3 27.71 227.78 28.76 C 225.51 33.42 224.11 37.93 223 43 C 221.87 43.13 220.73 43.27 219.56 43.4 C 218.04 43.58 216.52 43.76 215 43.94 C 213.88 44.07 213.88 44.07 212.75 44.2 C 207.83 44.78 202.91 45.38 198 46 C 204.05 33.36 223.28 2.84 239 5 Z"
                        }
                    }
                    ShapePath {
                        id: catPath4
                        strokeColor: root.primaryColor
                        strokeStyle: ShapePath.DashLine
                        strokeWidth: 1.45
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin
                        fillColor: Qt.rgba(root.primaryColor.r, root.primaryColor.g, root.primaryColor.b, root.catFill)
                        dashPattern: [87.97, 175.94]
                        dashOffset: root.drawOffset(4, 87.97, root.elapsedMs)
                        PathSvg {
                            path: "M 140 58 C 148.21 58.36 155.79 62.3 163 66 C 160.93 70.53 158.05 74.57 155.28 78.69 C 152.87 82.36 151.2 85.73 150 90 C 149.01 89.67 148.02 89.34 147 89 C 145.83 86.49 144.85 84.1 143.94 81.5 C 143.67 80.77 143.41 80.05 143.14 79.3 C 138.36 65.82 138.36 65.82 139 59 C 139.33 58.67 139.66 58.34 140 58 Z"
                        }
                    }
                    ShapePath {
                        id: catPath5
                        strokeColor: root.primaryColor
                        strokeStyle: ShapePath.DashLine
                        strokeWidth: 1.45
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin
                        fillColor: Qt.rgba(root.primaryColor.r, root.primaryColor.g, root.primaryColor.b, root.catFill)
                        dashPattern: [100.25, 200.5]
                        dashOffset: root.drawOffset(5, 100.25, root.elapsedMs)
                        PathSvg {
                            path: "M 222.6836 84.9844 C 226.3736 87.1744 228.2636 89.0344 230.0036 93.0044 C 227.0036 94.0044 227.0036 94.0044 224.0036 93.0044 C 211.7836 92.0444 211.7836 92.0444 201.0036 97.0044 C 199.1636 98.8444 197.3836 100.7344 195.6136 102.6444 C 194.0036 104.0044 194.0036 104.0044 191.0636 103.9344 C 190.3836 103.6244 189.7036 103.3144 189.0036 103.0044 C 189.5836 96.8444 191.8436 92.9844 196.3736 88.8144 C 204.2536 83.3744 213.4436 81.5944 222.6836 84.9844 Z"
                        }
                    }
                    ShapePath {
                        id: catPath6
                        strokeColor: root.primaryColor
                        strokeStyle: ShapePath.DashLine
                        strokeWidth: 1.45
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin
                        fillColor: Qt.rgba(root.primaryColor.r, root.primaryColor.g, root.primaryColor.b, root.catFill)
                        dashPattern: [94.08, 188.16]
                        dashOffset: root.drawOffset(6, 94.08, root.elapsedMs)
                        PathSvg {
                            path: "M 206 121 C 204.87 124.39 204.27 124.75 201.44 126.69 C 193.8 132.25 188.41 138.39 183.54 146.48 C 182.14 148.77 180.64 150.89 179 153 C 177.68 152.67 176.36 152.34 175 152 C 173.98 145.51 177.38 141.05 181 136 C 185.7 130.18 191.47 126.53 198 123 C 198.68 122.61 199.35 122.23 200.05 121.83 C 202 121 202 121 206 121 Z"
                        }
                    }
                    ShapePath {
                        id: catPath7
                        strokeColor: root.primaryColor
                        strokeStyle: ShapePath.DashLine
                        strokeWidth: 1.45
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin
                        fillColor: Qt.rgba(root.primaryColor.r, root.primaryColor.g, root.primaryColor.b, root.catFill)
                        dashPattern: [97.41, 194.82]
                        dashOffset: root.drawOffset(7, 97.41, root.elapsedMs)
                        PathSvg {
                            path: "M 202 113 C 201 116 201 116 199.21 117.06 C 198.44 117.37 197.67 117.69 196.88 118.02 C 195.61 118.56 195.61 118.56 194.31 119.11 C 193.4 119.49 192.5 119.86 191.56 120.25 C 184.07 123.48 177.42 126.9 171 132 C 165.65 136.07 165.65 136.07 162.62 135.75 C 162.09 135.5 161.55 135.25 161 135 C 164.87 125.43 173.6 120.76 182.7 116.84 C 189.13 114.34 195.07 113 202 113 Z"
                        }
                    }
                    ShapePath {
                        id: catPath8
                        strokeColor: root.primaryColor
                        strokeStyle: ShapePath.DashLine
                        strokeWidth: 1.45
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin
                        fillColor: Qt.rgba(root.primaryColor.r, root.primaryColor.g, root.primaryColor.b, root.catFill)
                        dashPattern: [86.16, 172.32]
                        dashOffset: root.drawOffset(8, 86.16, root.elapsedMs)
                        PathSvg {
                            path: "M 296.625 95.6875 C 297.695 95.6975 297.695 95.6975 298.785 95.7075 C 307.745 95.9075 314.375 98.0475 321.605 103.3975 C 323.005 104.9975 323.005 104.9975 323.005 108.9975 C 319.485 109.4375 317.305 108.7975 314.245 107.1875 C 304.825 102.2475 295.455 101.0875 285.005 99.9975 C 285.005 99.3375 285.005 98.6775 285.005 97.9975 C 288.705 95.6175 292.365 95.6575 296.625 95.6875 Z"
                        }
                    }
                    ShapePath {
                        id: catPath9
                        strokeColor: root.primaryColor
                        strokeStyle: ShapePath.DashLine
                        strokeWidth: 1.45
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin
                        fillColor: Qt.rgba(root.primaryColor.r, root.primaryColor.g, root.primaryColor.b, root.catFill)
                        dashPattern: [66.69, 133.38]
                        dashOffset: root.drawOffset(9, 66.69, root.elapsedMs)
                        PathSvg {
                            path: "M 275 70 C 277.72 71.66 280.05 73.47 282 76 C 282 76.99 282 77.98 282 79 C 281.02 78.75 281.02 78.75 280.02 78.49 C 272.86 76.79 272.86 76.79 266 79 C 264.66 80.66 263.32 82.32 262 84 C 260.68 84 259.36 84 258 84 C 257.54 78.31 258.32 75.4 262 71 C 266.34 68.29 270.2 68.6 275 70 Z"
                        }
                    }
                    ShapePath {
                        id: catPath10
                        strokeColor: root.primaryColor
                        strokeStyle: ShapePath.DashLine
                        strokeWidth: 1.45
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin
                        fillColor: Qt.rgba(root.primaryColor.r, root.primaryColor.g, root.primaryColor.b, root.catFill)
                        dashPattern: [82.32, 164.64]
                        dashOffset: root.drawOffset(10, 82.32, root.elapsedMs)
                        PathSvg {
                            path: "M 321 86 C 321 86.66 321 87.32 321 88 C 319.89 88.04 318.77 88.07 317.62 88.11 C 308.29 88.49 299.4 89.45 290.26 91.38 C 287 92 287 92 283 92 C 284 89 284 89 286.19 87.68 C 297.57 82.64 309.56 78.89 321 86 Z"
                        }
                    }
                    ShapePath {
                        id: catPath11
                        strokeColor: root.primaryColor
                        strokeStyle: ShapePath.DashLine
                        strokeWidth: 1.45
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin
                        fillColor: Qt.rgba(root.primaryColor.r, root.primaryColor.g, root.primaryColor.b, root.catFill)
                        dashPattern: [47.83, 95.66]
                        dashOffset: root.drawOffset(11, 47.83, root.elapsedMs)
                        PathSvg {
                            path: "M 264 90 C 265.21 93.64 264.67 94.33 263.06 97.69 C 262.49 98.9 262.49 98.9 261.91 100.14 C 261.61 100.75 261.31 101.37 261 102 C 255.65 101.47 251.64 99.53 247 97 C 248.39 93.74 248.86 93.08 252.12 91.25 C 256.16 89.68 259.71 89.77 264 90 Z"
                        }
                    }
                }
            }

            // 官方 logo 猫形（已去掉星点与文字）：2.0s 起 0.3s 淡入，与描边交叉淡化。
            // 星点仍由下方 starLayer 自绘，保持原稿的左上角位置与 2.8s 闪烁。
            Item {
                anchors.fill: parent
                opacity: root.catFill
                layer.enabled: true
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: Qt.rgba(root.primaryColor.r, root.primaryColor.g, root.primaryColor.b, 0.55)
                    shadowBlur: 0.85
                    shadowHorizontalOffset: 0
                    shadowVerticalOffset: 0
                    autoPaddingEnabled: true
                }

                Image {
                    anchors.fill: parent
                    source: root.amber ? "qrc:/splash/amber/cat.svg" : "qrc:/splash/blue/cat.svg"
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                }
            }

            // 星点：0s 弹出，2.8s 闪烁一次，按 fill-box 中心缩放。
            // 原稿 .star-piece 的 CSS transform 覆盖了 SVG 的 translate(232,184)，
            // 浏览器实际按路径自身坐标绘制，落在 logo 左上角（中心 1.5,21.5）。
            // 这里用独立图层承载负坐标区域，避免被图层纹理裁切。
            Item {
                id: starLayer
                x: -19
                y: 0
                width: 41
                height: 43
                opacity: root.starOpacity(root.elapsedMs)
                transform: Scale {
                    origin.x: 20.5
                    origin.y: 21.5
                    xScale: root.starScale(root.elapsedMs)
                    yScale: root.starScale(root.elapsedMs)
                }
                layer.enabled: true
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: Qt.rgba(root.primaryColor.r, root.primaryColor.g, root.primaryColor.b, 0.65)
                    shadowBlur: 1.0
                    shadowHorizontalOffset: 0
                    shadowVerticalOffset: 0
                    autoPaddingEnabled: true
                }

                // 路径坐标保持在 SVG 原坐标系（x 含负值），用 Shape 的偏移把它挪进图层
                Shape {
                    x: 19
                    y: 0
                    width: 41
                    height: 43
                    antialiasing: true

                    ShapePath {
                        fillColor: root.primaryColor
                        PathSvg {
                            path: "M 0 0 C 0.99 0.33 1.98 0.66 3 1 C 3.99 3.33 3.99 3.33 4.88 6.25 C 6.52 11 7.8 13.99 12 17 C 15.27 18.52 18.59 19.82 22 21 C 21.5 22.49 21.5 22.49 21 24 C 18.89 24.96 18.89 24.96 16.25 25.81 C 11.36 27.67 8.83 29.45 6 34 C 4.81 36.96 3.84 39.93 3 43 C 2.01 43 1.02 43 0 43 C -0.21 42.35 -0.42 41.7 -0.63 41.02 C -3.6 32.58 -7.55 28.64 -15.44 24.62 C -16.61 24.09 -17.79 23.55 -19 23 C -19 22.01 -19 21.02 -19 20 C -18.34 19.78 -17.67 19.56 -16.99 19.34 C -11.11 17.22 -7.44 15.46 -4 10 C -2.51 6.72 -1.2 3.4 0 0 Z"
                        }
                    }
                }
            }

            // 品牌文字：2.3s 上浮 20 → 0
            Item {
                id: wordLayer
                x: 0
                y: 20 * (1 - root.wordRise(root.elapsedMs))
                width: logoWrap.width
                height: logoWrap.height
                opacity: root.clamp01(root.wordRise(root.elapsedMs))
                layer.enabled: true
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: Qt.rgba(root.primaryColor.r, root.primaryColor.g, root.primaryColor.b, 0.45)
                    shadowBlur: 0.7
                    shadowHorizontalOffset: 0
                    shadowVerticalOffset: 0
                    autoPaddingEnabled: true
                }

                Image {
                    anchors.fill: parent
                    source: root.amber ? "qrc:/splash/amber/word.svg" : "qrc:/splash/blue/word.svg"
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                }
            }
        }
    }

    // ---------------------------------------------------------------- 壳体
    // 上下压暗：让品牌文字与「跳过」在任何画面上都可读
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: parent.height * 0.24
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: Theme.splashScrimTop
            }
            GradientStop {
                position: 1.0
                color: "transparent"
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: parent.height * 0.34
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: "transparent"
            }
            GradientStop {
                position: 1.0
                color: Theme.splashScrimBottom
            }
        }
    }

    // 跳过
    Rectangle {
        id: skipPill
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.s5
        width: skipRow.implicitWidth + Theme.s5
        height: 34
        radius: Theme.rPill
        color: skipHover.hovered ? Theme.overlayStrong : Theme.overlayHover
        border.width: 1
        border.color: skipHover.hovered ? Theme.hairlineStrong : Theme.hairline

        Behavior on color {
            ColorAnimation {
                duration: Theme.dFast
            }
        }
        Behavior on border.color {
            ColorAnimation {
                duration: Theme.dFast
            }
        }

        Row {
            id: skipRow
            anchors.centerIn: parent
            spacing: Theme.s2

            MLabel {
                label: qsTr("跳过")
                tone: Theme.text
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: "››"
                color: Theme.textDim
                font.pixelSize: Theme.fSmall
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        HoverHandler {
            id: skipHover
        }
        TapHandler {
            onTapped: root.finish()
        }
    }

    // 底部：品牌标识（保留小 logo 与品牌名，不再显示状态小字与进度条）
    Item {
        id: bottomBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.s6
        anchors.rightMargin: Theme.s6
        anchors.bottomMargin: Theme.s5
        height: 52

        Row {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.s3
            spacing: Theme.s2

            BrandLogo {
                width: 22
                height: 22
                markOnly: true
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: "MeowAi"
                color: Theme.text
                font.pixelSize: Theme.fSmall
                font.weight: Font.DemiBold
                font.letterSpacing: 2.6
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}
