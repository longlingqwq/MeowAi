import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MeowAi

/**
 * 技能仓库页（功能 #9 · M19）。
 *
 * 左：技能列表（名称 / 来源 / 说明 / 触发词 / 启用开关）；右：详情与 skill.md 全文。
 * 顶部：全局模式（自动 / 手动，二选一）+ 导入 zip。
 * 业务全部经 `Skills`（SkillCatalog）转发：本文件不读文件、不解析 manifest、不碰数据库。
 * 删除只对「导入」技能开放，内置技能只能禁用；删除前必须二次确认。
 */
Item {
    id: page

    /// 操作结果统一交给 main.qml 的 Toast 宿主（页面不自己持有 Toast 实例）
    signal toastRequested(string message, bool isError)

    property string selectedId: ""
    /// 技能集合变更计数：detail() 是函数调用，靠它触发详情重算
    property int revision: 0
    /// 导入失败的完整原因（Toast 只有两行，长文案在这里完整展示）
    property string importNotice: ""

    readonly property var skillList: Skills ? Skills.skills : []
    readonly property var selectedDetail: {
        page.revision
        return (Skills && page.selectedId.length > 0) ? Skills.detail(page.selectedId) : null
    }

    Component.onCompleted: ensureSelection()

    Connections {
        target: Skills

        // 技能被删除 / 导入后：选中项若已不存在，回落到第一个
        function onSkillsChanged() {
            page.revision += 1
            page.ensureSelection()
        }
    }

    function ensureSelection() {
        if (page.selectedId.length > 0) {
            for (let i = 0; i < page.skillList.length; ++i) {
                if (page.skillList[i].id === page.selectedId)
                    return
            }
        }
        page.selectedId = page.skillList.length > 0 ? page.skillList[0].id : ""
    }

    /// FileDialog 给的是 url，core 只收本地路径
    function localPath(url) {
        let text = url.toString()
        if (text.startsWith("file:///"))
            text = text.substring(8)
        else if (text.startsWith("file://"))
            text = text.substring(7)
        return decodeURIComponent(text)
    }

    // ============================================================ 顶部工具条
    Item {
        id: toolbar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 44

        Row {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.s3

            MLabel { label: qsTr("调用模式"); anchors.verticalCenter: parent.verticalCenter }

            // 二选一：同一时间只有一种模式生效
            Rectangle {
                id: modeSwitch
                width: 168
                height: 32
                radius: Theme.rPill
                color: Theme.ink1
                border.width: 1
                border.color: Theme.hairline

                Row {
                    anchors.fill: parent
                    anchors.margins: 3
                    spacing: 2

                    Repeater {
                        model: [
                            { "key": "auto", "text": qsTr("自动") },
                            { "key": "manual", "text": qsTr("手动") }
                        ]

                        delegate: Rectangle {
                            required property var modelData
                            width: (modeSwitch.width - 8) / 2
                            height: parent.height
                            radius: Theme.rPill
                            color: Skills && Skills.mode === modelData.key
                                   ? Theme.accentGhost
                                   : (modeHover.hovered ? Theme.overlayHover : "transparent")
                            border.width: 1
                            border.color: Skills && Skills.mode === modelData.key
                                          ? Theme.accentHair : "transparent"

                            Text {
                                anchors.centerIn: parent
                                text: modelData.text
                                color: Skills && Skills.mode === modelData.key
                                       ? Theme.accent : Theme.textDim
                                font.pixelSize: Theme.fSmall
                                font.weight: Font.DemiBold
                            }

                            HoverHandler { id: modeHover }

                            TapHandler {
                                cursorShape: Qt.PointingHandCursor
                                onTapped: {
                                    if (Skills)
                                        Skills.mode = modelData.key
                                }
                            }
                        }
                    }
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(120, toolbar.width - 660)
                text: Skills && Skills.manualMode
                      ? qsTr("手动：在对话输入区选择技能，仅本次发送生效")
                      : qsTr("自动：按触发词命中，最多注入 2 个已启用技能")
                color: Theme.textFaint
                font.pixelSize: Theme.fLabel
                elide: Text.ElideRight
            }
        }

        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.s3

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("内置 %1 · 导入 %2")
                      .arg(Skills ? Skills.bundledCount : 0)
                      .arg(Skills ? Skills.importedCount : 0)
                color: Theme.textGhost
                font.pixelSize: Theme.fLabel
            }

            MButton {
                text: qsTr("导入技能包")
                variant: "primary"
                small: true
                anchors.verticalCenter: parent.verticalCenter
                onClicked: importDialog.open()
            }
        }
    }

    // ============================================================ 导入失败详情
    // Toast 最多两行且会省略，正确示例这类长文案在这里完整展示
    Rectangle {
        id: importNoticeBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: toolbar.bottom
        anchors.topMargin: visible ? Theme.s3 : 0
        visible: page.importNotice.length > 0
        height: visible ? noticeText.implicitHeight + Theme.s2 * 2 : 0
        radius: Theme.rMd
        color: Theme.dangerGhost
        border.width: 1
        border.color: Theme.dangerHair

        Text {
            id: noticeText
            anchors.left: parent.left
            anchors.right: noticeClose.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.s3
            anchors.rightMargin: Theme.s2
            text: page.importNotice
            color: Theme.danger
            font.pixelSize: Theme.fSmall
            lineHeight: 1.3
            wrapMode: Text.Wrap
        }

        MButton {
            id: noticeClose
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.s1
            width: 26
            height: 26
            text: "\u2715"
            variant: "quiet"
            small: true
            onClicked: page.importNotice = ""
        }
    }

    // ============================================================ 列表 + 详情
    Row {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: importNoticeBar.bottom
        anchors.bottom: parent.bottom
        anchors.topMargin: page.importNotice.length > 0 ? Theme.s3 : Theme.s4
        spacing: Theme.s4

        // ---------------------------------------------------------- 列表
        MSurface {
            id: listPanel
            width: 360
            height: parent.height
            elevated: true

            Column {
                anchors.fill: parent
                anchors.margins: Theme.s4
                spacing: Theme.s3

                MLabel { label: qsTr("技能列表") }

                Text {
                    width: parent.width
                    visible: page.skillList.length === 0
                    text: qsTr("还没有技能：点右上角「导入技能包」安装第一个")
                    color: Theme.textFaint
                    font.pixelSize: Theme.fSmall
                    wrapMode: Text.Wrap
                }

                ListView {
                    id: skillView
                    width: parent.width
                    height: parent.height - Theme.s3 - 18
                    clip: true
                    spacing: Theme.s2
                    model: page.skillList

                    delegate: Rectangle {
                        id: row
                        required property var modelData

                        width: ListView.view ? ListView.view.width : 0
                        height: 96
                        radius: Theme.rMd
                        color: page.selectedId === row.modelData.id
                               ? Theme.accentGhost
                               : (rowHover.hovered ? Theme.overlayFaint : "transparent")
                        border.width: 1
                        border.color: page.selectedId === row.modelData.id
                                      ? Theme.accentHair : Theme.hairline

                        Behavior on color {
                            ColorAnimation { duration: Theme.dFast }
                        }

                        // 选中：垫在最底层，开关仍能优先拿到点击
                        TapHandler {
                            onTapped: page.selectedId = row.modelData.id
                        }

                        Column {
                            anchors.left: parent.left
                            anchors.right: toggle.left
                            anchors.top: parent.top
                            anchors.leftMargin: Theme.s3
                            anchors.rightMargin: Theme.s2
                            anchors.topMargin: Theme.s2 + 2
                            spacing: 4

                            Row {
                                spacing: Theme.s2

                                Text {
                                    text: row.modelData.name
                                    color: Theme.text
                                    font.pixelSize: Theme.fBody
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                    width: Math.min(implicitWidth, 150)
                                }

                                MChip {
                                    text: row.modelData.sourceLabel
                                    tone: row.modelData.source === "bundled" ? "neutral" : "accent"
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            Text {
                                width: parent.width
                                text: row.modelData.description.length > 0
                                      ? row.modelData.description
                                      : qsTr("（无说明）")
                                color: Theme.textDim
                                font.pixelSize: Theme.fSmall
                                elide: Text.ElideRight
                            }

                            Text {
                                width: parent.width
                                text: row.modelData.autoAvailable
                                      ? qsTr("触发词：%1").arg(row.modelData.triggers.join(" · "))
                                      : qsTr("无触发词 · 不参与自动匹配")
                                color: row.modelData.autoAvailable ? Theme.textFaint : Theme.warn
                                font.pixelSize: Theme.fLabel
                                elide: Text.ElideRight
                            }
                        }

                        MSwitch {
                            id: toggle
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.rightMargin: Theme.s3
                            anchors.topMargin: Theme.s3
                            checked: row.modelData.enabled
                            onToggled: {
                                if (Skills && !Skills.setSkillEnabled(row.modelData.id, checked))
                                    page.toastRequested(qsTr("启用状态保存失败"), true)
                            }
                        }

                        HoverHandler { id: rowHover }
                    }
                }
            }
        }

        // ---------------------------------------------------------- 详情
        MSurface {
            id: detailPanel
            width: parent.width - listPanel.width - Theme.s4
            height: parent.height

            Text {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: Theme.s4
                visible: page.selectedDetail === null
                text: qsTr("左侧选择一个技能查看详情")
                color: Theme.textFaint
                font.pixelSize: Theme.fSmall
            }

            Column {
                id: detailHeader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.s4
                height: visible ? implicitHeight : 0
                spacing: Theme.s3
                visible: page.selectedDetail !== null

                // 头部：名称 / 版本 / 来源 / 删除
                Item {
                    width: parent.width
                    height: 40

                    Row {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.s2

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: page.selectedDetail ? page.selectedDetail.name : ""
                            color: Theme.text
                            font.pixelSize: Theme.fH2
                            font.weight: Font.DemiBold
                        }

                        MChip {
                            anchors.verticalCenter: parent.verticalCenter
                            text: page.selectedDetail
                                  ? qsTr("%1 · v%2").arg(page.selectedDetail.sourceLabel)
                                                    .arg(page.selectedDetail.version)
                                  : ""
                            tone: page.selectedDetail && page.selectedDetail.source === "bundled"
                                  ? "neutral" : "accent"
                        }

                        MChip {
                            anchors.verticalCenter: parent.verticalCenter
                            text: page.selectedDetail && page.selectedDetail.enabled
                                  ? qsTr("已启用") : qsTr("已禁用")
                            tone: page.selectedDetail && page.selectedDetail.enabled ? "ok" : "neutral"
                            dot: true
                        }
                    }

                    MButton {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        visible: page.selectedDetail !== null && page.selectedDetail.canDelete
                        text: qsTr("删除")
                        variant: "danger"
                        small: true
                        onClicked: deleteDialog.open()
                    }
                }

                Text {
                    width: parent.width
                    text: page.selectedDetail ? page.selectedDetail.description : ""
                    color: Theme.textDim
                    font.pixelSize: Theme.fSmall
                    wrapMode: Text.Wrap
                }

                Text {
                    width: parent.width
                    text: page.selectedDetail && page.selectedDetail.autoAvailable
                          ? qsTr("触发词：%1").arg(page.selectedDetail.triggers.join(" · "))
                          : qsTr("该技能没有触发词，只能手动选择，不参与自动匹配")
                    color: page.selectedDetail && page.selectedDetail.autoAvailable
                           ? Theme.textFaint : Theme.warn
                    font.pixelSize: Theme.fLabel
                    wrapMode: Text.Wrap
                }

                Rectangle {
                    width: parent.width
                    height: 1
                    color: Theme.hairline
                }

                MLabel { label: qsTr("技能文档 · SKILL.MD") }
            }

            // skill.md 全文：只读滚动区，保留原文（不按 Markdown 渲染）
            Flickable {
                id: documentView
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: detailHeader.bottom
                anchors.bottom: parent.bottom
                anchors.margins: Theme.s4
                anchors.topMargin: page.selectedDetail !== null ? Theme.s3 : 0
                visible: page.selectedDetail !== null
                clip: true
                contentWidth: width
                contentHeight: documentText.height
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick

                Text {
                    id: documentText
                    width: parent.width
                    text: page.selectedDetail ? page.selectedDetail.document : ""
                    color: Theme.textDim
                    font.pixelSize: Theme.fSmall
                    lineHeight: 1.45
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                }

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                    width: 8

                    contentItem: Rectangle {
                        implicitWidth: 3
                        radius: 2
                        color: Theme.hairlineStrong
                        opacity: 0.6
                    }
                }
            }
        }
    }

    // ============================================================ 导入
    FileDialog {
        id: importDialog
        title: qsTr("选择技能包（zip）")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("技能包 (*.zip)")]

        onAccepted: {
            const result = Skills ? Skills.importPackage(page.localPath(selectedFile)) : null
            if (!result)
                return
            page.importNotice = result.ok ? "" : result.message
            page.toastRequested(result.ok ? result.message : qsTr("技能包校验未通过，详情见页面提示"),
                                !result.ok)
            if (result.ok) {
                page.selectedId = result.id
                page.ensureSelection()
            }
        }
    }

    // ============================================================ 删除确认
    Popup {
        id: deleteDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(420, page.width - Theme.s6)
        padding: Theme.s4
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            radius: Theme.rLg
            color: Theme.ink3
            border.width: 1
            border.color: Theme.hairlineStrong
        }

        contentItem: Column {
            spacing: Theme.s3

            MLabel { label: qsTr("删除技能") }

            Text {
                width: parent.width
                text: page.selectedDetail
                      ? qsTr("确定删除「%1」？只删除本地技能目录，不影响其他数据。")
                            .arg(page.selectedDetail.name)
                      : ""
                color: Theme.textDim
                font.pixelSize: Theme.fSmall
                wrapMode: Text.Wrap
            }

            Row {
                anchors.right: parent.right
                spacing: Theme.s2

                MButton {
                    text: qsTr("取消")
                    variant: "ghost"
                    small: true
                    onClicked: deleteDialog.close()
                }

                MButton {
                    text: qsTr("删除")
                    variant: "danger"
                    small: true
                    onClicked: {
                        const id = page.selectedId
                        deleteDialog.close()
                        const result = Skills ? Skills.removeSkill(id) : null
                        if (result)
                            page.toastRequested(result.message, !result.ok)
                    }
                }
            }
        }
    }
}
