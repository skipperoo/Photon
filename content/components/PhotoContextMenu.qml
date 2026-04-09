import QtQuick
import QtQuick.Controls.Basic as T
import Main

T.Menu {
    id: root
    implicitWidth: 200
    topPadding: 4
    bottomPadding: 4
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

    // ─── Properties ──────────────────────────────────────────────────────────

    property int selectionCount: 0
    property bool canCopy: true
    property bool canPaste: false
    property bool showFilterSection: true
    property int filterOperator: 2
    property int filterRating: 0
    property var operatorLabels: ["=", ">", "≥", "<", "≤"]
    property bool keepFilterMenuOpen: false

    // ─── Signals ─────────────────────────────────────────────────────────────

    signal copyRequested()
    signal pasteRequested()
    signal ratingRequested(int rating)
    signal filterOperatorCycleRequested()
    signal filterRatingRequested(int rating)
    signal rotateRightRequested()
    signal rotateLeftRequested()
    signal flipHorizontalRequested()
    signal flipVerticalRequested()
    signal createPanoramaRequested()

    // ─── Helpers ─────────────────────────────────────────────────────────────
 
    function openAt(x, y) {
        root.x = x
        root.y = y
        open()
    }

    function stars(value) {
        if (value <= 0) return "Off"
        var s = ""
        for (var i = 0; i < value; i++) s += "★"
        return s
    }

    onAboutToHide: {
        if (!keepFilterMenuOpen) return
        keepFilterMenuOpen = false
        Qt.callLater(function() {
            root.x = root.x
            root.y = root.y
            root.open()
            filterMenu.open()
        })
    }

    // ─── Internal components ─────────────────────────────────────────────────

    component MenuBg: Rectangle {
        color: Theme.secondary
        border.color: Theme.border
        border.width: 1
        radius: Theme.radius
    }

    component StyledMenuItem: T.MenuItem {
        id: item
        implicitWidth: 200
        implicitHeight: 36
        leftPadding: 12
        rightPadding: 12
        topPadding: 0
        bottomPadding: 0
        spacing: 8

        background: Rectangle {
            color: item.highlighted ? Theme.accent : "transparent"
            radius: Theme.radius
            anchors.fill: parent
            anchors.margins: 2
        }

        contentItem: Row {
            spacing: item.spacing
            anchors.verticalCenter: parent.verticalCenter

            Image {
                source: item.icon.source
                width: 16
                height: 16
                anchors.verticalCenter: parent.verticalCenter
                visible: item.icon.source != ""
                fillMode: Image.PreserveAspectFit
                // Respect icon.color tinting if set
                layer.enabled: item.icon.color !== Qt.rgba(0,0,0,0) && item.icon.color !== "#000000"
                layer.effect: null
            }

            Text {
                text: item.text
                font: Theme.fontSmall
                color: item.enabled ? Theme.foreground : Theme.mutedFg
                verticalAlignment: Text.AlignVCenter
                height: item.implicitHeight
                leftPadding: (item.icon.source == "" ) ? 24 : 0
            }
        }

        // Submenu arrow indicator
        indicator: Item {
            width: 12
            height: item.implicitHeight
            visible: item.subMenu !== null
            anchors.right: parent.right
            anchors.rightMargin: 8
            Text {
                anchors.centerIn: parent
                color: item.enabled ? Theme.foreground : Theme.mutedFg
            }
        }
    }

    component StyledMenu: T.Menu {
        implicitWidth: 150
        topPadding: 4
        bottomPadding: 4
        closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

        background: MenuBg {}

        delegate: StyledMenuItem {}
    }

    component StyledSeparator: T.MenuSeparator {
        topPadding: 4
        bottomPadding: 4
        contentItem: Rectangle {
            implicitWidth: root.implicitWidth
            implicitHeight: 1
            color: Theme.mutedFg
        }
    }

    // ─── Root menu background ────────────────────────────────────────────────

    background: MenuBg {}
    delegate: StyledMenuItem {}

    // ─── Items ───────────────────────────────────────────────────────────────

    StyledMenuItem {
        text: "Copy settings"
        icon.source: "qrc:/Main/assets/icons/copy-menu.svg"
        enabled: root.canCopy
        onTriggered: { root.copyRequested(); root.close() }
    }

    StyledMenuItem {
        text: root.canPaste
              ? root.selectionCount > 1
                ? "Paste settings to " + root.selectionCount + " photos"
                : "Paste settings"
              : "Settings buffer empty"
        icon.source: "qrc:/Main/assets/icons/clipboard-paste-menu.svg"
        enabled: root.canPaste
        onTriggered: { root.pasteRequested(); root.close() }
    }

    StyledSeparator {}

    StyledMenu {
        title: "Rating"

        StyledMenuItem {
            text: "No rating"
            onTriggered: root.ratingRequested(0)
        }
        Repeater {
            model: 5
            delegate: StyledMenuItem {
                required property int index
                text: (index + 1) + " " + root.stars(index + 1)
                onTriggered: root.ratingRequested(index + 1)
            }
        }
    }

    StyledMenu {
        id: filterMenu
        title: "Filter"
        enabled: root.showFilterSection

        StyledMenuItem {
            text: "Criteria: " + root.operatorLabels[root.filterOperator]
            onTriggered: {
                root.filterOperatorCycleRequested()
                root.keepFilterMenuOpen = true
            }
        }
        StyledMenuItem {
            text: "All"
            onTriggered: root.filterRatingRequested(0)
        }
        Repeater {
            model: 5
            delegate: StyledMenuItem {
                required property int index
                text: (index + 1) + " " + root.stars(index + 1)
                onTriggered: root.filterRatingRequested(index + 1)
            }
        }
    }

    StyledSeparator { visible: root.showFilterSection }

    StyledMenu {
        id: mergeMenu
        title: "Merge Photos"
        enabled: root.selectionCount > 1

        StyledMenuItem {
            text: "Panorama"
            icon.source: "qrc:/Main/assets/icons/panorama.svg"
            onTriggered: { root.createPanoramaRequested(); root.close() }
        }
        StyledMenuItem {
            text: "HDR"
            icon.source: "qrc:/Main/assets/icons/hdr.svg"
            enabled: false
        }
    }

    StyledSeparator {}

    StyledMenuItem {
        text: "Rotate right"
        icon.source: "qrc:/Main/assets/icons/rotate-cw-menu.svg"
        onTriggered: { root.rotateRightRequested(); root.close() }
    }

    StyledMenuItem {
        text: "Rotate left"
        icon.source: "qrc:/Main/assets/icons/rotate-ccw.svg"
        icon.color: Theme.foreground
        onTriggered: { root.rotateLeftRequested(); root.close() }
    }

    StyledMenuItem {
        text: "Flip horizontally"
        icon.source: "qrc:/Main/assets/icons/flip-horizontal-menu.svg"
        onTriggered: { root.flipHorizontalRequested(); root.close() }
    }

    StyledMenuItem {
        text: "Flip vertically"
        icon.source: "qrc:/Main/assets/icons/flip-vertical-menu.svg"
        onTriggered: { root.flipVerticalRequested(); root.close() }
    }
}
