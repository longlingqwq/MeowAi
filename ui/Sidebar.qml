import QtQuick

/**
 * 左侧导航栏（外壳）。
 *
 * 只负责导航与侧栏状态：页面切换、缩回 / 展开、会话列表容器。
 * 页面本身不在此处；会话列表本体由 M2 的 ConversationList.qml 提供。
 *
 * 状态由 main.qml 的 pageIndex / navCollapsed / conversationExpanded 落成：
 * 本组件只发请求信号，不自行改写外部属性，避免打断外部绑定。
 */
Item {
    id: sidebar

    /// 当前页（0 对话 / 1 个性化 / 2 资料库）
    property int pageIndex: 0
    /// 是否缩回为窄条（约 64px）
    property bool navCollapsed: false
    /// 对话列表是否展开
    property bool conversationExpanded: true

    signal pageRequested(int index)
    signal navCollapsedRequested(bool collapsed)
    signal conversationExpandedRequested(bool expanded)
    signal newConversationRequested()

    width: navCollapsed ? 64 : 228
    clip: true

    Behavior on width {
        NumberAnimation {
            duration: Theme.dBase
            easing.type: Theme.easeInOut
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.navRail

        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: Theme.hairline
        }
    }

    // ============================================================ 展开态
    Item {
        id: expandedPane
        anchors.fill: parent
        visible: !sidebar.navCollapsed

        Column {
            id: topColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: Theme.s4
            anchors.rightMargin: Theme.s4
            anchors.topMargin: Theme.s3
            spacing: Theme.s2

            // 一行两件：左侧紧凑主行动胶囊，右侧 32×32 缩回按钮
            Item {
                width: parent.width
                height: 32

                MButton {
                    id: newChatButton
                    anchors.left: parent.left
                    // 右侧贴近缩回按钮，中间只留一个基础间距
                    anchors.right: collapseButton.left
                    anchors.rightMargin: Theme.s2
                    anchors.verticalCenter: parent.verticalCenter
                    height: 30
                    text: qsTr("＋ 新对话")
                    variant: "primary"
                    small: true
                    onClicked: sidebar.newConversationRequested()

                    // 胶囊拉长后沿用居中会让文字悬在长条中部，这里改为贴左排版
                    contentItem: Text {
                        text: newChatButton.text
                        color: Theme.accentInk
                        font.pixelSize: Theme.fSmall
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }

                Item {
                    id: collapseButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 32
                    height: 32

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.rMd
                        color: collapseHover.hovered ? Theme.overlayHover : "transparent"
                        border.width: 1
                        border.color: Theme.hairlineStrong

                        Behavior on color {
                            ColorAnimation { duration: Theme.dFast }
                        }
                    }

                    GlyphChevron {
                        anchors.centerIn: parent
                        pointingLeft: true
                        color: collapseHover.hovered ? Theme.text : Theme.textFaint
                    }

                    HoverHandler { id: collapseHover }

                    TapHandler {
                        cursorShape: Qt.PointingHandCursor
                        onTapped: sidebar.navCollapsedRequested(true)
                    }
                }
            }

            NavItem {
                id: navTalk
                label: qsTr("对话")
                mono: qsTr("对")
                active: sidebar.pageIndex === 0
                expandable: true
                expanded: sidebar.conversationExpanded
                onActivated: sidebar.pageRequested(0)
                onExpandToggled: sidebar.conversationExpandedRequested(!sidebar.conversationExpanded)
            }
        }

        // 会话列表：M2 实现，外壳只提供容器与外部接线
        ConversationList {
            id: conversationList
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: topColumn.bottom
            anchors.bottom: bottomColumn.top
            anchors.leftMargin: Theme.s2
            anchors.rightMargin: Theme.s2
            anchors.topMargin: Theme.s2
            anchors.bottomMargin: Theme.s2
            visible: sidebar.conversationExpanded
            currentConversationId: History.conversationId
            onConversationActivated: (id) => History.openConversation(id)
            onNewConversationRequested: History.newConversation()
        }

        // 底部导航：个性化 / 资料库
        Column {
            id: bottomColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: Theme.s4
            anchors.rightMargin: Theme.s4
            anchors.bottomMargin: Theme.s4
            spacing: Theme.s2

            Rectangle {
                width: parent.width
                height: 1
                color: Theme.hairline
            }

            NavItem {
                label: qsTr("个性化")
                mono: qsTr("个")
                active: sidebar.pageIndex === 1
                onActivated: sidebar.pageRequested(1)
            }

            NavItem {
                label: qsTr("资料库")
                mono: qsTr("资")
                active: sidebar.pageIndex === 2
                onActivated: sidebar.pageRequested(2)
            }
        }
    }

    // ============================================================ 缩回态
    Item {
        id: collapsedPane
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 64
        visible: sidebar.navCollapsed

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: Theme.s2
            anchors.rightMargin: Theme.s2
            anchors.topMargin: Theme.s3
            spacing: Theme.s2

            // 窄条最顶部：展开按钮（与展开态同一套图标语言，方向相反）
            Item {
                width: parent.width
                height: 32

                Item {
                    id: expandButton
                    anchors.centerIn: parent
                    width: 32
                    height: 32

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.rMd
                        color: expandHover.hovered ? Theme.overlayHover : "transparent"
                        border.width: 1
                        border.color: Theme.hairlineStrong

                        Behavior on color {
                            ColorAnimation { duration: Theme.dFast }
                        }
                    }

                    GlyphChevron {
                        anchors.centerIn: parent
                        pointingLeft: false
                        color: expandHover.hovered ? Theme.text : Theme.textFaint
                    }

                    HoverHandler { id: expandHover }

                    TapHandler {
                        cursorShape: Qt.PointingHandCursor
                        onTapped: sidebar.navCollapsedRequested(false)
                    }
                }
            }

            MButton {
                width: parent.width
                text: qsTr("＋")
                variant: "primary"
                small: true
                onClicked: sidebar.newConversationRequested()
            }

            NavItem {
                label: qsTr("对话")
                mono: qsTr("对")
                active: sidebar.pageIndex === 0
                collapsed: true
                onActivated: sidebar.pageRequested(0)
            }
        }

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: Theme.s2
            anchors.rightMargin: Theme.s2
            anchors.bottomMargin: Theme.s4
            spacing: Theme.s2

            NavItem {
                label: qsTr("个性化")
                mono: qsTr("个")
                active: sidebar.pageIndex === 1
                collapsed: true
                onActivated: sidebar.pageRequested(1)
            }

            NavItem {
                label: qsTr("资料库")
                mono: qsTr("资")
                active: sidebar.pageIndex === 2
                collapsed: true
                onActivated: sidebar.pageRequested(2)
            }
        }
    }

    // ------------------------------------------------------------ 箭头图形
    // 两根 1px 竖条旋转 45° 咬合成 chevron，避免依赖字体里的 ‹ › 字形
    component GlyphChevron: Item {
        id: chev

        property bool pointingLeft: true
        property color color: Theme.textFaint

        implicitWidth: 10
        implicitHeight: 10

        Rectangle {
            width: 1
            height: 6.4
            x: chev.pointingLeft ? 3.75 : 5.25
            y: 0.05
            color: chev.color
            rotation: chev.pointingLeft ? 45 : -45
            transformOrigin: Item.Center
        }

        Rectangle {
            width: 1
            height: 6.4
            x: chev.pointingLeft ? 3.75 : 5.25
            y: 3.55
            color: chev.color
            rotation: chev.pointingLeft ? -45 : 45
            transformOrigin: Item.Center
        }
    }

    // ------------------------------------------------------------ 导航项
    component NavItem: Item {
        id: nav

        property string label: ""
        /// 缩回态显示的首字 / 图标
        property string mono: ""
        property bool active: false
        property bool collapsed: false
        property bool expandable: false
        property bool expanded: false

        /// 展开箭头的固定占位与命中宽度，箭头与点击区共用，保证两者对齐
        readonly property int expandZoneWidth: Theme.s5 + Theme.s3

        signal activated()
        signal expandToggled()

        width: parent ? parent.width : 0
        height: 40

        Rectangle {
            anchors.fill: parent
            radius: Theme.rMd
            color: nav.active ? Theme.accentGhost
                              : (navHover.hovered ? Theme.overlayFaint : "transparent")

            Behavior on color {
                ColorAnimation { duration: Theme.dFast }
            }
        }

        Rectangle {
            visible: nav.active
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 2
            height: 18
            radius: 1
            color: Theme.accent
        }

        Text {
            visible: !nav.collapsed
            anchors.left: parent.left
            anchors.leftMargin: Theme.s3 + 4
            anchors.right: parent.right
            anchors.rightMargin: nav.expandable ? nav.expandZoneWidth : Theme.s2
            anchors.verticalCenter: parent.verticalCenter
            text: nav.label
            color: nav.active ? Theme.text : Theme.textDim
            font.pixelSize: Theme.fBody
            font.weight: nav.active ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
        }

        Text {
            visible: nav.collapsed
            anchors.centerIn: parent
            text: nav.mono
            color: nav.active ? Theme.text : Theme.textDim
            font.pixelSize: Theme.fBody
            font.weight: nav.active ? Font.DemiBold : Font.Normal
        }

        // 展开箭头：改用与缩回按钮同一套自绘折线，比三角字形更大、更清楚
        Item {
            id: navArrow
            visible: nav.expandable && !nav.collapsed
            anchors.right: parent.right
            width: nav.expandZoneWidth
            height: parent.height

            GlyphChevron {
                anchors.centerIn: parent
                pointingLeft: true
                color: navHover.hovered ? Theme.textDim : Theme.textFaint
                // 收起时朝右（展开列表），展开时朝下；以 10×10 折线为基准放大
                rotation: nav.expanded ? -90 : 180
                scale: 1.4
            }
        }

        HoverHandler { id: navHover }

        MouseArea {
            anchors.fill: parent
            onClicked: nav.activated()
        }

        // 箭头命中区独立于整行：点箭头只切换展开，不切页；放大命中区方便点准
        MouseArea {
            visible: nav.expandable && !nav.collapsed
            anchors.right: parent.right
            width: nav.expandZoneWidth
            height: parent.height
            onClicked: nav.expandToggled()
        }
    }
}
