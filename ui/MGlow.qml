import QtQuick
import QtQuick.Effects

/**
 * 背景柔光晕：圆形色块经高斯模糊后作为舞台氛围层。
 *
 * 纯装饰、不接收任何输入（enabled: false），开销只发生在首次成层时。
 */
Rectangle {
    id: root

    /// 0..1，越大越糊
    property real blurAmount: 1.0
    /// 0..1 透明度
    property real intensity: 1.0

    radius: width / 2
    opacity: intensity
    enabled: false

    layer.enabled: true
    layer.effect: MultiEffect {
        blurEnabled: true
        blur: root.blurAmount
        blurMax: 96
    }
}
