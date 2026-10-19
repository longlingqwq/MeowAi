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

    opacity: 0

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
        audioOutput: AudioOutput { muted: !root.soundEnabled }

        onMediaStatusChanged: {
            if (status === MediaPlayer.EndOfMedia || status === MediaPlayer.InvalidMedia)
                root.finished()
        }
        onErrorOccurred: root.finished()
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
            GradientStop { position: 0.0; color: "#8C08090D" }
            GradientStop { position: 1.0; color: "#0008090D" }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: parent.height * 0.34
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0008090D" }
            GradientStop { position: 1.0; color: "#D908090D" }
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
        color: skipHover.hovered ? "#2EFFFFFF" : "#14FFFFFF"
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
        TapHandler { onTapped: root.finished() }
    }

    // 载入提示
    Text {
        anchors.centerIn: parent
        visible: player.mediaStatus === MediaPlayer.LoadingMedia
                    || player.mediaStatus === MediaPlayer.StalledMedia
        text: qsTr("正在载入…")
        color: Theme.textFaint
        font.pixelSize: Theme.fSmall
        font.letterSpacing: 1.5
    }

    // 底部：品牌 + 播放进度
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
            anchors.bottom: progressTrack.top
            anchors.bottomMargin: Theme.s3
            spacing: Theme.s2

            EyeMark {
                width: 22
                height: 22
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

            MLabel {
                label: root.soundEnabled ? qsTr("播放中 · 有声") : qsTr("播放中 · 静音")
                tone: Theme.textGhost
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Rectangle {
            id: progressTrack
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 2
            radius: 1
            color: Theme.hairline

            Rectangle {
                height: parent.height
                radius: 1
                color: Theme.accent
                width: player.duration > 0
                       ? parent.width * Math.min(1, player.position / player.duration)
                       : 0
            }
        }
    }

    // 被隐藏时立即停止，避免声音在后台继续播放
    onVisibleChanged: if (!visible) player.stop()
}
