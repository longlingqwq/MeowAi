import QtQuick
import QtQuick.Controls
import QtQuick.Effects

/**
 * 头像裁剪弹层（M4）。
 *
 * 职责边界：
 *  - 只做预览与交互：原图始终只读，取消 / 关闭不会写入任何内容；
 *  - 确认后把圆形预览渲染成 512×512 PNG 写入临时目录，再交给
 *    Personalize.setAvatar() 复制进 assets（QML 不碰配置、不拼业务路径）；
 *  - 预览层按原图宽高比计算宽高（Stretch + 等比尺寸），圆形遮罩只裁形状，
 *    不改变像素比例，因此方图 / 竖图 / 横图都不会拉伸；
 *  - 平移按真实溢出量夹紧：1× 下方图不可拖、横图仅横向可拖、竖图仅纵向可拖，
 *    放大后两轴各自按溢出收边，图片始终铺满圆形、不露底色。
 */
Popup {
    id: root

    /// 待裁剪的原图（file:// URL 或本地路径）
    property string source: ""
    /// 缩放倍率，1x–4x
    property real zoom: 1.0
    /// 平移偏移（预览像素）
    property real panX: 0
    property real panY: 0
    /// 错误 / 失败原因（显示在弹层内）
    property string errorText: ""

    /// 头像图片读取失败：交给外层页面的顶部 Toast 展示
    signal errorReported(string message)

    readonly property real minZoom: 1.0
    readonly property real maxZoom: 4.0
    /// 预览直径：随窗口收窄，但不超过 264
    readonly property int previewSize: Math.max(160, Math.min(264, root.width - Theme.s5 * 2))

    parent: Overlay.overlay
    width: Math.min(560, parent ? parent.width - 48 : 520)
    height: Math.min(body.implicitHeight + Theme.s5 * 2,
                     parent ? parent.height - 48 : 760)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: Theme.s5
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    /// 打开并重置为「原图居中、1x」
    function start(sourceUrl) {
        root.source = sourceUrl
        resetTransform()
        root.errorText = ""
        root.open()
    }

    function resetTransform() {
        root.zoom = minZoom
        root.panX = 0
        root.panY = 0
    }

    function clampPan() {
        // 图片未加载完成时保持默认居中，避免无效平移与除零
        if (!previewImage.loaded) {
            root.panX = 0
            root.panY = 0
            return
        }
        // 按预览层相对舞台的真实溢出量夹紧；单轴无溢出时该轴归零
        const maxPanX = Math.max(0, (previewImage.width - stage.width) / 2)
        const maxPanY = Math.max(0, (previewImage.height - stage.height) / 2)
        root.panX = Math.max(-maxPanX, Math.min(maxPanX, root.panX))
        root.panY = Math.max(-maxPanY, Math.min(maxPanY, root.panY))
    }

    /// 以预览中心为锚点缩放，并按边界收回平移量
    function setZoom(value) {
        const next = Math.max(minZoom, Math.min(maxZoom, value))
        if (Math.abs(next - root.zoom) < 0.001)
            return
        const ratio = next / root.zoom
        root.zoom = next
        root.panX *= ratio
        root.panY *= ratio
        clampPan()
    }

    /// 渲染圆形预览 → 512×512 PNG → setAvatar；任一步失败都把原因留在弹层
    function confirmCrop() {
        if (previewImage.status !== Image.Ready) {
            root.errorText = qsTr("图片尚未加载完成，请稍后重试。")
            return
        }

        const target = App.avatarCropTempUrl()
        if (!target || target.length === 0) {
            root.errorText = qsTr("无法获取临时目录，裁剪结果未能保存。")
            return
        }

        // 抓取不带描边的遮罩容器：输出只含圆形头像本体与透明角
        stageWrap.grabToImage(function (result) {
            if (!result) {
                root.errorText = qsTr("裁剪结果生成失败，请重试。")
                return
            }
            if (!result.saveToFile(target)) {
                root.errorText = qsTr("裁剪结果写入失败，请检查临时目录权限。")
                return
            }
            if (!Personalize.setAvatar(target)) {
                root.errorText = Personalize.lastError.length > 0
                                 ? Personalize.lastError
                                 : qsTr("头像保存失败，请检查本地数据目录权限。")
                return
            }
            root.close()
        }, Qt.size(512, 512))
    }

    background: Rectangle {
        radius: Theme.rLg
        color: Theme.ink3
        border.width: 1
        border.color: Theme.hairlineStrong
    }

    contentItem: Column {
        id: body
        spacing: Theme.s3

        Column {
            width: parent.width
            spacing: Theme.s1

            MLabel { label: qsTr("头像裁剪") }

            Text {
                width: parent.width
                text: qsTr("头像只保存在本机，原图不会被覆盖或删除。")
                color: Theme.textFaint
                font.pixelSize: Theme.fSmall
                wrapMode: Text.Wrap
            }
        }

        // ------------------------------------------------ 圆形预览
        Item {
            width: parent.width
            height: cropFrame.height

            Rectangle {
                id: cropFrame
                readonly property int diameter: root.previewSize

                width: diameter + 2
                height: width
                anchors.centerIn: parent
                radius: width / 2
                color: "transparent"
                border.width: 1
                border.color: Theme.hairlineStrong

                // 圆形遮罩舞台：裁剪 / 显示都只走等比裁切，不做非等比拉伸
                Item {
                    id: stageWrap
                    anchors.fill: parent
                    anchors.margins: 1

                    // 圆形遮罩（供 MultiEffect.maskSource 使用，自身不可见）
                    Rectangle {
                        id: circleMask
                        anchors.fill: parent
                        radius: width / 2
                        color: "white"
                        visible: false
                        layer.enabled: true
                    }

                    Item {
                        id: stage
                        anchors.fill: parent
                        clip: true
                        // 预览层按 512 渲染，导出时 1:1 取样，避免放大后发虚
                        layer.enabled: true
                        layer.textureSize: Qt.size(512, 512)
                        layer.effect: MultiEffect {
                            maskEnabled: true
                            maskSource: circleMask
                        }

                        Rectangle {
                            anchors.fill: parent
                            color: Theme.ink1
                            visible: previewImage.status !== Image.Ready

                            Text {
                                anchors.centerIn: parent
                                width: parent.width - Theme.s4
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.Wrap
                                text: previewImage.status === Image.Error
                                      ? qsTr("图片无法加载，请换一张图片。")
                                      : qsTr("正在加载…")
                                color: previewImage.status === Image.Error ? Theme.danger : Theme.textGhost
                                font.pixelSize: Theme.fSmall
                            }
                        }

                        Image {
                            id: previewImage
                            source: root.source

                            // 无法读取时不再弹层内提示：上报页面顶部 Toast 并收起弹层
                            onStatusChanged: {
                                if (root.opened && status === Image.Error) {
                                    root.errorReported(qsTr("头像图片无法读取，请换一张图片。"))
                                    root.close()
                                }
                            }

                            /// 原图（受 sourceSize 限制后）的自然尺寸；未加载时为 0
                            readonly property real naturalWidth: implicitWidth > 0 ? implicitWidth : 0
                            readonly property real naturalHeight: implicitHeight > 0 ? implicitHeight : 0
                            readonly property bool loaded: naturalWidth > 0 && naturalHeight > 0
                            /// 等比铺满舞台的最小倍率 × 用户缩放
                            readonly property real coverScale: loaded
                                ? Math.max(stage.width / naturalWidth,
                                           stage.height / naturalHeight) * root.zoom
                                : 1
                            /// 两轴各自的真实溢出余量（以预览中心为基准）
                            readonly property real overflowX: Math.max(0, (width - stage.width) / 2)
                            readonly property real overflowY: Math.max(0, (height - stage.height) / 2)

                            width: loaded ? naturalWidth * coverScale : stage.width * root.zoom
                            height: loaded ? naturalHeight * coverScale : stage.height * root.zoom
                            x: (stage.width - width) / 2 + root.panX
                            y: (stage.height - height) / 2 + root.panY
                            // 宽高已按原图比例算好，不能再让 PreserveAspectCrop 二次裁剪
                            fillMode: Image.Stretch
                            sourceSize.width: 1536
                            sourceSize.height: 1536
                            asynchronous: true
                            visible: status === Image.Ready
                        }

                        MouseArea {
                            id: dragArea
                            anchors.fill: parent
                            cursorShape: (previewImage.overflowX > 0.5 || previewImage.overflowY > 0.5)
                                         ? Qt.SizeAllCursor
                                         : Qt.ArrowCursor

                            property real startPanX: 0
                            property real startPanY: 0
                            property real startMouseX: 0
                            property real startMouseY: 0

                            onPressed: function (mouse) {
                                dragArea.startPanX = root.panX
                                dragArea.startPanY = root.panY
                                dragArea.startMouseX = mouse.x
                                dragArea.startMouseY = mouse.y
                            }

                            onPositionChanged: function (mouse) {
                                if (!dragArea.pressed)
                                    return
                                root.panX = dragArea.startPanX + (mouse.x - dragArea.startMouseX)
                                root.panY = dragArea.startPanY + (mouse.y - dragArea.startMouseY)
                                root.clampPan()
                            }
                        }

                        WheelHandler {
                            onWheel: function (wheel) {
                                const steps = wheel.angleDelta.y / 120
                                if (steps === 0)
                                    return
                                root.setZoom(root.zoom + steps * 0.2)
                            }
                        }
                    }
                }
            }
        }

        // ------------------------------------------------ 缩放
        Row {
            width: parent.width
            spacing: Theme.s3

            Text {
                text: qsTr("缩放")
                color: Theme.textFaint
                font.pixelSize: Theme.fSmall
                width: 32
                anchors.verticalCenter: parent.verticalCenter
            }

            Slider {
                id: zoomSlider
                width: parent.width - 96
                height: 24
                from: root.minZoom
                to: root.maxZoom
                stepSize: 0.05
                onMoved: root.setZoom(zoomSlider.value)

                background: Rectangle {
                    x: zoomSlider.leftPadding
                    y: zoomSlider.topPadding + zoomSlider.availableHeight / 2 - height / 2
                    width: zoomSlider.availableWidth
                    height: 2
                    radius: 1
                    color: Theme.hairlineStrong

                    Rectangle {
                        width: Math.round(parent.width * zoomSlider.visualPosition)
                        height: parent.height
                        radius: 1
                        color: Theme.accent
                    }
                }

                handle: Rectangle {
                    x: zoomSlider.leftPadding + zoomSlider.visualPosition * (zoomSlider.availableWidth - width)
                    y: zoomSlider.topPadding + zoomSlider.availableHeight / 2 - height / 2
                    width: 14
                    height: 14
                    radius: width / 2
                    color: zoomSlider.pressed ? Theme.accentHi : Theme.accent
                }
            }

            Text {
                width: 44
                text: root.zoom.toFixed(1) + "×"
                color: Theme.textDim
                font.pixelSize: Theme.fSmall
                horizontalAlignment: Text.AlignRight
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // 滑杆与滚轮共用 zoom；拖动滑杆时临时解除回写绑定，避免互相打断
        Binding {
            target: zoomSlider
            property: "value"
            value: root.zoom
            when: !zoomSlider.pressed
        }

        Text {
            width: parent.width
            text: qsTr("图片铺满圆形，拖动可调整显示区域；滚轮或滑杆缩放。确认后生成 512×512 圆形头像。")
            color: Theme.textFaint
            font.pixelSize: Theme.fSmall
            wrapMode: Text.Wrap
            lineHeight: 1.5
        }

        Text {
            width: parent.width
            visible: root.errorText.length > 0
            text: root.errorText
            color: Theme.danger
            font.pixelSize: Theme.fSmall
            wrapMode: Text.Wrap
            lineHeight: 1.5
        }

        // ------------------------------------------------ 操作
        Item {
            width: parent.width
            height: confirmButton.height

            MButton {
                text: qsTr("重置")
                variant: "quiet"
                small: true
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                onClicked: {
                    root.resetTransform()
                    root.errorText = ""
                }
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.s2

                MButton {
                    text: qsTr("取消")
                    variant: "ghost"
                    small: true
                    onClicked: root.close()
                }

                MButton {
                    id: confirmButton
                    text: qsTr("确认")
                    variant: "primary"
                    small: true
                    enabled: previewImage.status === Image.Ready && Personalize.available
                    onClicked: root.confirmCrop()
                }
            }
        }
    }
}
