import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

/**
 * 个性化设置页（功能 #11）。
 *
 * 纯展示与绑定：所有读写都转发给 Personalize（core/ThemeManager），
 * 本文件不写任何业务逻辑，也不直接访问文件系统或配置。
 */
Item {
    id: page

    /// 请求主界面播放一次开屏预览（由外层接管，本页不做播放）
    signal previewSplash()

    /// 文件类失败上报给 main.qml 的 Toast 宿主（页面不自己持有 Toast 实例）
    signal toastRequested(string message, bool isError)

    /// 最近一次已用 Toast 展示的错误：同一文案不再在页面底部重复出现
    property string toastSuppressedError: ""

    /// 背景 / 开屏 / 头像读取失败统一走顶部 Toast；空字符串兜底为通用文案
    function reportFileError(message) {
        const text = message !== undefined && message !== null && String(message).trim().length > 0
                   ? String(message) : qsTr("操作失败，请重试")
        page.toastSuppressedError = text
        page.toastRequested(text, true)
    }

    ScrollView {
        id: scrollView
        anchors.fill: parent
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Column {
            width: scrollView.availableWidth
            spacing: Theme.s4

            Text {
                width: parent.width
                text: Personalize.available
                      ? qsTr("字体、背景、头像与开屏视频都只保存在本机 assets 目录，绝不上传。")
                      : qsTr("个性化配置不可用：本地数据目录尚未就绪。")
                color: Personalize.available ? Theme.textFaint : Theme.warn
                font.pixelSize: Theme.fSmall
                wrapMode: Text.Wrap
                lineHeight: 1.5
            }

            // ------------------------------------------------ 主题（阶段三）
            MSurface {
                width: parent.width
                height: themeSection.implicitHeight + Theme.s5 * 2

                Column {
                    id: themeSection
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.s5
                    spacing: Theme.s3

                    Row {
                        spacing: Theme.s3

                        MLabel {
                            label: qsTr("主题")
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: qsTr("换主题只替换设计令牌的值，不写任何界面代码；选择会记住，重启后保持。")
                            color: Theme.textFaint
                            font.pixelSize: Theme.fSmall
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    Flow {
                        width: parent.width
                        spacing: Theme.s2

                        Repeater {
                            model: ThemePreset.presets

                            delegate: MButton {
                                required property var modelData

                                text: modelData.name
                                variant: modelData.id === ThemePreset.currentPresetId
                                         ? "primary"
                                         : "ghost"
                                small: true
                                onClicked: ThemePreset.selectPreset(modelData.id)
                            }
                        }
                    }

                    Text {
                        visible: ThemePreset.lastError.length > 0
                        width: parent.width
                        text: ThemePreset.lastError
                        color: Theme.danger
                        font.pixelSize: Theme.fSmall
                        wrapMode: Text.Wrap
                    }
                }
            }

            // ------------------------------------------------ 字体
            MSurface {
                width: parent.width
                height: fontSection.implicitHeight + Theme.s5 * 2

                Column {
                    id: fontSection
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.s5
                    spacing: Theme.s3

                    Row {
                        spacing: Theme.s3

                        MLabel {
                            label: qsTr("字体")
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: Personalize.fontFamily.length > 0
                                  ? Personalize.fontFamily
                                  : qsTr("跟随系统默认字体")
                            color: Personalize.fontFamily.length > 0 ? Theme.text : Theme.textFaint
                            font.pixelSize: Theme.fSmall
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    Row {
                        width: parent.width
                        spacing: Theme.s2

                        MCombo {
                            id: fontBox
                            width: Math.max(160, Math.min(300, parent.width - 330))
                            enabled: Personalize.available
                            model: Personalize.availableFontFamilies()
                            placeholderText: qsTr("选择字体…")

                            // 未设置自定义字体时保持"未选中"，避免误把首项当成当前字体
                            Component.onCompleted: currentIndex =
                                                   model.indexOf(Personalize.fontFamily)
                        }

                        MButton {
                            text: qsTr("使用此字体")
                            variant: "primary"
                            small: true
                            enabled: Personalize.available && fontBox.currentText.length > 0
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: Personalize.setSystemFont(fontBox.currentText)
                        }

                        MButton {
                            text: qsTr("导入字体文件…")
                            small: true
                            enabled: Personalize.available
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: fontDialog.open()
                        }

                        MButton {
                            text: qsTr("恢复默认")
                            variant: "quiet"
                            small: true
                            enabled: Personalize.available
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: Personalize.clearFont()
                        }
                    }
                }
            }

            FileDialog {
                id: fontDialog
                title: qsTr("选择字体文件")
                nameFilters: [qsTr("字体文件 (*.ttf *.otf *.ttc)")]
                onAccepted: Personalize.importFont(selectedFile.toString())
            }

            // ------------------------------------------------ 背景
            MSurface {
                width: parent.width
                height: backgroundSection.implicitHeight + Theme.s5 * 2

                Row {
                    id: backgroundSection
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.s5
                    spacing: Theme.s4

                    // 预览缩略图
                    Rectangle {
                        width: 132
                        height: 76
                        radius: Theme.rMd
                        color: Theme.ink1
                        border.width: 1
                        border.color: Theme.hairline
                        clip: true

                        Image {
                            anchors.fill: parent
                            source: Personalize.backgroundSource
                            fillMode: Image.PreserveAspectCrop
                            visible: Personalize.backgroundSource !== ""
                            asynchronous: true
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: Personalize.backgroundSource === ""
                            text: qsTr("未设置")
                            color: Theme.textGhost
                            font.pixelSize: Theme.fSmall
                        }
                    }

                    Column {
                        spacing: Theme.s3

                        MLabel {
                            label: qsTr("背景")
                            anchors.left: parent.left
                        }

                        Text {
                            // 扩展名与上限对齐 backgroundDialog 过滤器及 ThemeManager.cpp 的 kImageLimit
                            text: qsTr("自定义背景会覆盖在墨色舞台之上，并自动压暗以保持文字可读。支持 PNG / JPG / JPEG / WEBP / BMP / GIF / SVG，单个文件不超过 128 MB。")
                            color: Theme.textFaint
                            font.pixelSize: Theme.fSmall
                            width: Math.min(page.width - 220, 420)
                            wrapMode: Text.Wrap
                            lineHeight: 1.5
                        }

                        Row {
                            spacing: Theme.s2

                            MButton {
                                text: qsTr("选择背景图…")
                                small: true
                                enabled: Personalize.available
                                onClicked: backgroundDialog.open()
                            }

                            MButton {
                                text: qsTr("清除背景")
                                variant: "quiet"
                                small: true
                                enabled: Personalize.available && Personalize.backgroundSource !== ""
                                onClicked: Personalize.clearBackground()
                            }
                        }
                    }
                }
            }

            FileDialog {
                id: backgroundDialog
                title: qsTr("选择背景图")
                nameFilters: [qsTr("图片 (*.png *.jpg *.jpeg *.webp *.bmp *.gif *.svg)")]
                onAccepted: {
                    if (!Personalize.setBackground(selectedFile.toString()))
                        page.reportFileError(Personalize.lastError)
                }
            }

            // ------------------------------------------------ 头像
            MSurface {
                width: parent.width
                height: avatarSection.implicitHeight + Theme.s5 * 2

                Row {
                    id: avatarSection
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.s5
                    spacing: Theme.s4

                    MAvatar {
                        width: 76
                        height: 76
                        source: Personalize.avatarSource
                        fallbackText: qsTr("猫")
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Column {
                        spacing: Theme.s3
                        anchors.verticalCenter: parent.verticalCenter

                        MLabel {
                            label: qsTr("头像")
                            anchors.left: parent.left
                        }

                        Text {
                            text: qsTr("显示在顶栏右上角，仅本机可见。")
                            color: Theme.textFaint
                            font.pixelSize: Theme.fSmall
                        }

                        Row {
                            spacing: Theme.s2

                            MButton {
                                text: qsTr("选择头像…")
                                small: true
                                enabled: Personalize.available
                                onClicked: avatarDialog.open()
                            }

                            MButton {
                                text: qsTr("清除头像")
                                variant: "quiet"
                                small: true
                                enabled: Personalize.available && Personalize.avatarSource !== ""
                                onClicked: Personalize.clearAvatar()
                            }
                        }
                    }
                }
            }

            FileDialog {
                id: avatarDialog
                title: qsTr("选择头像图片")
                nameFilters: [qsTr("图片 (*.png *.jpg *.jpeg *.webp *.bmp *.gif *.svg)")]
                onAccepted: avatarCrop.start(selectedFile.toString())
            }

            // 头像裁剪弹层：原图只读，取消 / 关闭不会修改现有头像
            AvatarCrop {
                id: avatarCrop
                onErrorReported: (message) => page.reportFileError(message)
            }

            // ------------------------------------------------ 开屏动画
            MSurface {
                width: parent.width
                height: splashSection.implicitHeight + Theme.s5 * 2

                Row {
                    id: splashSection
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.s5
                    spacing: Theme.s4

                    Rectangle {
                        width: 132
                        height: 76
                        radius: Theme.rMd
                        color: Theme.ink1
                        border.width: 1
                        border.color: Theme.hairline

                        BrandLogo {
                            anchors.centerIn: parent
                            width: 44
                            height: 44
                            markOnly: true
                            opacity: Personalize.splashSource !== "" ? 0.85 : 0.55
                        }

                        MChip {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 8
                            tone: Personalize.splashSource !== "" ? "ok" : "neutral"
                            dot: true
                            text: Personalize.splashSource !== "" ? qsTr("自定义") : qsTr("内置")
                        }
                    }

                    Column {
                        spacing: Theme.s3

                        MLabel {
                            label: qsTr("开屏动画")
                            anchors.left: parent.left
                        }

                        Text {
                            // 格式与上限对齐 splashDialog 过滤器及 ThemeManager.cpp 的 kVideoLimit
                            text: qsTr("未设置自定义内容时按当前主题播放内置开屏，设置后以自定义内容为准。播放结束或点击「跳过」即进入主界面。\n支持 MP4 / MOV / M4V / WEBM / MKV / AVI，单个文件不超过 200 MB。")
                            color: Theme.textFaint
                            font.pixelSize: Theme.fSmall
                            width: Math.min(page.width - 220, 420)
                            wrapMode: Text.Wrap
                            lineHeight: 1.5
                        }

                        Row {
                            spacing: Theme.s2

                            MButton {
                                text: qsTr("选择视频…")
                                small: true
                                enabled: Personalize.available
                                onClicked: splashDialog.open()
                            }

                            MButton {
                                text: qsTr("预览")
                                small: true
                                enabled: Personalize.available
                                onClicked: page.previewSplash()
                            }

                            MButton {
                                text: qsTr("清除")
                                variant: "quiet"
                                small: true
                                enabled: Personalize.available && Personalize.splashSource !== ""
                                onClicked: Personalize.clearSplash()
                            }
                        }

                        Row {
                            spacing: Theme.s4

                            Row {
                                spacing: Theme.s2

                                MSwitch {
                                    id: splashEnabledSwitch
                                    checked: Personalize.splashEnabled
                                    enabled: Personalize.available
                                    onToggled: (value) => Personalize.splashEnabled = value
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Text {
                                    text: qsTr("启动时播放")
                                    color: Theme.textDim
                                    font.pixelSize: Theme.fSmall
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            Row {
                                spacing: Theme.s2

                                MSwitch {
                                    id: splashSoundSwitch
                                    checked: Personalize.splashSoundEnabled
                                    enabled: Personalize.available && Personalize.splashEnabled
                                    onToggled: (value) => Personalize.splashSoundEnabled = value
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Text {
                                    text: qsTr("播放音效")
                                    color: Personalize.splashEnabled ? Theme.textDim : Theme.textGhost
                                    font.pixelSize: Theme.fSmall
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }
                    }
                }
            }

            FileDialog {
                id: splashDialog
                title: qsTr("选择开屏视频")
                nameFilters: [qsTr("视频 (*.mp4 *.mov *.m4v *.webm *.mkv *.avi)")]
                onAccepted: {
                    if (!Personalize.setSplash(selectedFile.toString()))
                        page.reportFileError(Personalize.lastError)
                }
            }

            // ------------------------------------------------ 帮助与关于
            MSurface {
                width: parent.width
                height: helpSection.implicitHeight + Theme.s5 * 2

                Row {
                    id: helpSection
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.s5
                    spacing: Theme.s4

                    Column {
                        spacing: Theme.s2
                        width: Math.min(page.width - 220, 460)

                        MLabel {
                            label: qsTr("帮助")
                            anchors.left: parent.left
                        }

                        Text {
                            width: parent.width
                            text: qsTr("开发者：Longling　联系方式：3086865618@qq.com")
                            color: Theme.textDim
                            font.pixelSize: Theme.fSmall
                            wrapMode: Text.Wrap
                        }

                        Text {
                            width: parent.width
                            text: qsTr("遇到问题、想反馈建议或申请新功能，都可以通过上面的邮箱联系。")
                            color: Theme.textFaint
                            font.pixelSize: Theme.fSmall
                            wrapMode: Text.Wrap
                            lineHeight: 1.5
                        }
                    }

                    MButton {
                        text: qsTr("帮助")
                        variant: "primary"
                        small: true
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: helpDialog.open()
                    }
                }
            }

            /// 帮助弹层：开发者与联系方式为固定信息，与《用户手册》保持一致
            Popup {
                id: helpDialog

                /// 手册缺失提示：仅由 App.openUserGuide() 的返回值驱动
                property bool guideMissing: false

                parent: Overlay.overlay
                width: Math.min(520, parent ? parent.width - 48 : 520)
                height: Math.min(helpContent.implicitHeight + Theme.s5 * 2,
                                parent ? parent.height - 48 : 720)
                x: parent ? Math.round((parent.width - width) / 2) : 0
                y: parent ? Math.round((parent.height - height) / 2) : 0
                padding: 0
                modal: true
                focus: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                onOpened: guideMissing = false

                background: Rectangle {
                    radius: Theme.rLg
                    color: Theme.ink3
                    border.width: 1
                    border.color: Theme.hairlineStrong
                }

                contentItem: Item {
                    id: helpBody

                    Column {
                        id: helpContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.s5
                        spacing: Theme.s3

                        Row {
                            spacing: Theme.s2

                            BrandLogo {
                                width: 28
                                height: 28
                                markOnly: true
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: "MeowAi"
                                color: Theme.text
                                font.pixelSize: Theme.fH1
                                font.weight: Font.DemiBold
                                font.letterSpacing: 1.6
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        Text {
                            width: parent.width
                            text: qsTr("开发者：Longling　联系方式：3086865618@qq.com")
                            color: Theme.textDim
                            font.pixelSize: Theme.fSmall
                            wrapMode: Text.Wrap
                        }

                        Rectangle {
                            width: parent.width
                            height: 1
                            color: Theme.hairline
                        }

                        MLabel { label: qsTr("快速上手") }

                        Text {
                            width: parent.width
                            text: qsTr("1. 在「对话」页选择模型，填入自己的 API Key；\n"
                                       + "2. 在「资料库」新建小库并导入文件，索引完成后挂载；\n"
                                       + "3. 打开「参考资料模式」，助理会先引用你的文件再回答；\n"
                                       + "4. 数据（对话、索引、密钥）都保存在本机，只把提问所需的文字发往你选择的模型服务。")
                            color: Theme.textFaint
                            font.pixelSize: Theme.fSmall
                            wrapMode: Text.Wrap
                            lineHeight: 1.5
                        }

                        Text {
                            width: parent.width
                            text: qsTr("完整操作说明见随安装包提供的《用户手册》（USER_GUIDE.md）。")
                            color: Theme.textFaint
                            font.pixelSize: Theme.fSmall
                            wrapMode: Text.Wrap
                            lineHeight: 1.5
                        }

                        Item {
                            width: parent.width
                            height: closeButton.height

                            MButton {
                                text: qsTr("打开用户手册")
                                variant: "primary"
                                small: true
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                onClicked: helpDialog.guideMissing = !App.openUserGuide()
                            }

                            MButton {
                                id: closeButton
                                text: qsTr("关闭")
                                variant: "ghost"
                                small: true
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                onClicked: helpDialog.close()
                            }
                        }

                        Text {
                            width: parent.width
                            visible: helpDialog.guideMissing
                            text: qsTr("未找到用户手册，请确认安装包完整。")
                            color: Theme.warn
                            font.pixelSize: Theme.fSmall
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }

            // ------------------------------------------------ 重置
            MSurface {
                width: parent.width
                height: resetSection.implicitHeight + Theme.s5 * 2

                Row {
                    id: resetSection
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.s5
                    spacing: Theme.s4

                    Column {
                        spacing: Theme.s2
                        width: Math.min(page.width - 220, 420)

                        MLabel {
                            label: qsTr("重置")
                            anchors.left: parent.left
                        }

                        Text {
                            text: qsTr("恢复全部默认外观，并清理已导入的字体、背景、头像与开屏视频文件。")
                            color: Theme.textFaint
                            font.pixelSize: Theme.fSmall
                            width: parent.width
                            wrapMode: Text.Wrap
                            lineHeight: 1.5
                        }
                    }

                    MButton {
                        text: qsTr("恢复全部默认")
                        variant: "danger"
                        small: true
                        enabled: Personalize.available
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: Personalize.resetAll()
                    }
                }
            }

            // ------------------------------------------------ 错误
            MSurface {
                width: parent.width
                height: errorText.implicitHeight + Theme.s4 * 2
                // 已由顶部 Toast 展示的文件类错误不再在页底重复；其余错误仍内联可见
                visible: Personalize.lastError.length > 0
                         && Personalize.lastError !== page.toastSuppressedError
                radius: Theme.rMd
                fill: Theme.dangerGhost
                stroke: Theme.dangerHair

                Text {
                    id: errorText
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: Theme.s4
                    text: Personalize.lastError
                    color: Theme.danger
                    font.pixelSize: Theme.fSmall
                    wrapMode: Text.Wrap
                }
            }

            Item { width: 1; height: Theme.s4 }
        }
    }

    // 外部源变化时回同步（用户操作会打断 checked 绑定）
    Connections {
        target: Personalize

        function onFontFamilyChanged() {
            fontBox.currentIndex = fontBox.model.indexOf(Personalize.fontFamily)
        }
        function onSplashChanged() {
            splashEnabledSwitch.checked = Personalize.splashEnabled
            splashSoundSwitch.checked = Personalize.splashSoundEnabled
        }
    }
}
