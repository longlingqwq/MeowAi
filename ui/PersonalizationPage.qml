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
                            width: 300
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
                            text: qsTr("自定义背景会覆盖在墨色舞台之上，并自动压暗以保持文字可读。")
                            color: Theme.textFaint
                            font.pixelSize: Theme.fSmall
                            wrapMode: Text.Wrap
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
                onAccepted: Personalize.setBackground(selectedFile.toString())
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
                onAccepted: Personalize.setAvatar(selectedFile.toString())
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

                        EyeMark {
                            anchors.centerIn: parent
                            width: 44
                            height: 44
                            opacity: Personalize.splashSource !== "" ? 0.85 : 0.22
                        }

                        MChip {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 8
                            tone: Personalize.splashSource !== "" ? "ok" : "neutral"
                            dot: true
                            text: Personalize.splashSource !== "" ? qsTr("已设置") : qsTr("未设置")
                        }
                    }

                    Column {
                        spacing: Theme.s3

                        MLabel {
                            label: qsTr("开屏动画")
                            anchors.left: parent.left
                        }

                        Text {
                            text: qsTr("启动时播放一段本地视频（含其自带音轨），播放结束或点击「跳过」即进入主界面。")
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
                                enabled: Personalize.available && Personalize.splashSource !== ""
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
                onAccepted: Personalize.setSplash(selectedFile.toString())
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
                visible: Personalize.lastError.length > 0
                radius: Theme.rMd
                fill: Theme.dangerGhost
                stroke: "#3DE5605A"

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
