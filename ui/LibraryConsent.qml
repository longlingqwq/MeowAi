import QtQuick
import QtQuick.Controls
import MeowAi

/**
 * 资料库「首次启用 / 内容外发」同意层（v0.2.5 · M8）。
 *
 * 只在用户**明确点击同意**后才把内容送去 embedding / 视觉模型；
 * 文案必须让用户看清「哪些内容会离开本机」「去哪里」「如何撤回」。
 * 组件本身不写配置：同意动作由调用方执行 `Library.grantConsent()`。
 */
Item {
    id: root

    property bool open: false
    signal accepted()
    signal rejected()

    visible: open
    z: 1000

    Rectangle {
        anchors.fill: parent
        color: Theme.scrim

        MouseArea {
            anchors.fill: parent
            // 遮罩吞掉点击，防止误触底层页面
            onClicked: {}
        }
    }

    MSurface {
        id: card
        width: Math.min(560, root.width - Theme.s6)
        anchors.centerIn: parent
        elevated: true
        implicitHeight: content.implicitHeight + Theme.s6

        Column {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.s5
            spacing: Theme.s3

            MLabel { label: "资料库 · 首次启用" }

            Text {
                width: parent.width
                text: qsTr("启用后，为了让你能「基于资料库提问」，MeowAi 会把检索命中的片段（以及你选择入库的图片）发送给下面配置的模型服务：")
                color: Theme.text
                font.pixelSize: Theme.fBody
                wrapMode: Text.Wrap
                lineHeight: 1.45
            }

            Column {
                width: parent.width
                spacing: Theme.s1

                Text {
                    width: parent.width
                    text: "· 文本：片段正文会作为上下文发给对话模型；建立向量时片段会发给 embedding 模型。"
                    color: Theme.textDim
                    font.pixelSize: Theme.fSmall
                    wrapMode: Text.Wrap
                }
                Text {
                    width: parent.width
                    text: "· 图片：只有在你另行同意图片外发并配置视觉模型后，才会压缩转码后发送用于生成描述。"
                    color: Theme.textDim
                    font.pixelSize: Theme.fSmall
                    wrapMode: Text.Wrap
                }
                Text {
                    width: parent.width
                    text: "· 本地优先：文件本体不会上传，库内副本与索引保存在本机；API Key 只存本机加密保险库。"
                    color: Theme.textDim
                    font.pixelSize: Theme.fSmall
                    wrapMode: Text.Wrap
                }
            }

            Rectangle {
                width: parent.width
                height: 1
                color: Theme.hairline
            }

            Text {
                width: parent.width
                text: qsTr("你可以随时在「资料库 → 检索与隐私」中撤回同意；撤回后不会再外发任何内容，已建立的本地索引仍保留。")
                color: Theme.textFaint
                font.pixelSize: Theme.fSmall
                wrapMode: Text.Wrap
            }

            Row {
                width: parent.width
                spacing: Theme.s2
                layoutDirection: Qt.RightToLeft

                MButton {
                    text: qsTr("同意并启用")
                    variant: "primary"
                    onClicked: {
                        root.open = false
                        root.accepted()
                    }
                }
                MButton {
                    text: qsTr("暂不启用")
                    variant: "ghost"
                    onClicked: {
                        root.open = false
                        root.rejected()
                    }
                }
            }
        }
    }
}
