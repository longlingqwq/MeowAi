import QtQuick
import QtQuick.Window
import MeowAi

/**
 * 最小可运行主窗口。
 * UI 层只做展示与绑定，业务一律经 AppController（App）转发给 core / providers。
 */
Window {
    id: root

    width: 960
    height: 640
    visible: true
    title: App.appName + " · " + App.platformId

    color: Theme.background

    Column {
        anchors.centerIn: parent
        spacing: Theme.spacing

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: App.appName
            color: Theme.foreground
            font.pixelSize: 34
            font.family: Theme.fontFamily
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("阶段一 · 底层框架骨架")
            color: Theme.muted
            font.pixelSize: 15
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: App.statusMessage
            color: Theme.muted
            font.pixelSize: 15
        }
    }
}
