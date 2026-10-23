import QtQuick
import QtMultimedia

/**
 * 开屏视频覆盖层（功能 #11）：播放用户自定义的开屏视频，并播放视频自带音轨。
 *
 * 纯展示组件，不做业务判断：视频源与音效开关都来自 Personalize（core/ThemeManager）。
 * 播放结束 / 无法播放 / 用户点击跳过时发出 finished，由上层决定隐藏。
 */
Item {
    id: root

    /// 是否播放音效（关闭时静音，画面继续播放）
    property bool soundEnabled: Personalize.splashSoundEnabled

    signal finished()

    /// 结束只发一次：跳过与自然播完/解码失败共用
    property bool done: false

    opacity: 0

    /// 结束后先静音再暂停，避免尾音在交叉淡化期间继续输出
    function finishPlayback() {
        if (done)
            return;
        done = true;
        audioOut.muted = true;
        player.pause();
        finished();
    }

    Component.onCompleted: {
        player.play()
        fadeIn.start()
    }

    NumberAnimation {
        id: fadeIn
        target: root
        property: "opacity"
        to: 1
        duration: 760
        easing.type: Easing.OutCubic
    }

    MediaPlayer {
        id: player
        source: Personalize.splashSource
        videoOutput: videoOutput
        audioOutput: AudioOutput { id: audioOut; muted: !root.soundEnabled }

        onMediaStatusChanged: {
            if (status === MediaPlayer.EndOfMedia || status === MediaPlayer.InvalidMedia)
                root.finishPlayback()
        }
        onErrorOccurred: root.finishPlayback()
    }

    VideoOutput {
        id: videoOutput
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectCrop
    }

    // 上下压暗：让品牌文字与「跳过」在任何画面上都可读
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: parent.height * 0.24
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.splashScrimTop }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: parent.height * 0.34
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: Theme.splashScrimBottom }
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
            ColorAnimation { duration: Theme.dFast }
        }
        Behavior on border.color {
            ColorAnimation { duration: Theme.dFast }
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

        HoverHandler { id: skipHover }
        TapHandler { onTapped: root.finishPlayback() }
    }

    // 底部：品牌标识（保留小 logo 与品牌名，不再显示状态小字、进度条与载入提示）
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

    // 被隐藏时立即停止，避免声音在后台继续播放
    onVisibleChanged: if (!visible) player.stop()
}
