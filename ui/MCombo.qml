import QtQuick
import QtQuick.Controls

/**
 * 下拉选择：完全自绘，避开 Windows 11 原生亮色弹层。
 * 外观与 MField 一致，弹层为墨色悬浮面板 + 琥珀高亮。
 */
ComboBox {
    id: root

    /// 未选中任何项（currentIndex < 0）时显示的占位文案
    property string placeholderText: ""

    implicitHeight: 38
    leftPadding: 12
    rightPadding: 30
    topPadding: 8
    bottomPadding: 8

    font.pixelSize: Theme.fBody

    readonly property bool hasSelection: currentIndex >= 0 && displayText.length > 0

    contentItem: Text {
        leftPadding: 0
        rightPadding: 0
        text: root.hasSelection ? root.displayText : root.placeholderText
        color: root.hasSelection ? (root.enabled ? Theme.text : Theme.textGhost)
                                 : Theme.textGhost
        font: root.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Text {
        x: root.width - root.rightPadding - width + 8
        y: (root.height - height) / 2
        text: "\u25BE"
        color: root.hovered ? Theme.textDim : Theme.textGhost
        font.pixelSize: Theme.fSmall
    }

    background: Rectangle {
        radius: Theme.rMd
        color: Theme.ink1
        border.width: 1
        border.color: (root.activeFocus || root.popup.visible)
                      ? Theme.accentHair
                      : (root.hovered ? Theme.hairlineStrong : Theme.hairline)

        Behavior on border.color {
            ColorAnimation { duration: Theme.dFast }
        }
    }

    delegate: ItemDelegate {
        id: item
        required property int index
        required property var modelData

        width: root.width - 8
        height: 32
        highlighted: root.highlightedIndex === index

        contentItem: Text {
            text: item.modelData
            color: item.highlighted ? Theme.accentHi : Theme.text
            font.pixelSize: Theme.fSmall
            leftPadding: 8
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: Theme.rSm
            color: item.highlighted ? Theme.accentGhost : "transparent"
        }
    }

    popup: Popup {
        y: root.height + 6
        width: root.width
        implicitHeight: Math.min(280, contentItem.implicitHeight + 8)
        padding: 4

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex

            ScrollIndicator.vertical: ScrollIndicator { }
        }

        background: Rectangle {
            radius: Theme.rMd
            color: Theme.ink3
            border.width: 1
            border.color: Theme.hairlineStrong
        }
    }
}
