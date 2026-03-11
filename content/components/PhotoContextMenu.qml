import QtQuick
import QtQuick.Controls as T
import Main

T.Menu {
    id: root

    property int selectionCount: 0
    property bool canCopy: true
    property bool canPaste: false
    property bool showFilterSection: true
    property int filterOperator: 2
    property int filterRating: 0
    property var operatorLabels: ["=", ">", "≥", "<", "≤"]
    property bool keepFilterMenuOpen: false

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
        if (!keepFilterMenuOpen)
            return
        keepFilterMenuOpen = false
        Qt.callLater(function() {
            root.openAt(root.x, root.y)
            filterMenu.open()
        })
    }

    background: Rectangle {
        color: Theme.secondary
        border.color: Theme.border
        border.width: 1
        radius: Theme.radius
    }

    T.MenuItem {
        text: "Copy settings"
        icon.source: "qrc:/Main/assets/icons/copy-menu.svg"
        enabled: root.canCopy
        onTriggered: {
            root.copyRequested()
            root.close()
        }
    }

    T.MenuItem {
        text: canPaste ? root.selectionCount > 1
              ? "Paste settings to " + root.selectionCount + " photos"
              : "Paste settings" : "Settings buffer empty"
        icon.source: "qrc:/Main/assets/icons/clipboard-paste-menu.svg"
        enabled: root.canPaste
        onTriggered: {
            root.pasteRequested()
            root.close()
        }
    }

    T.MenuSeparator {}

    T.Menu {
        title: "Rating"
        T.MenuItem {
            text: "No rating"
            onTriggered: root.ratingRequested(0)
        }
        Repeater {
            model: 5
            delegate: T.MenuItem {
                required property int index
                text: (index + 1) + " " + root.stars(index + 1)
                onTriggered: root.ratingRequested(index + 1)
            }
        }
    }

    T.Menu {
        id: filterMenu
        title: "Filter"
        enabled: root.showFilterSection
        T.MenuItem {
            text: "Criteria: " + root.operatorLabels[root.filterOperator]
            onTriggered: {
                root.filterOperatorCycleRequested()
                root.keepFilterMenuOpen = true
            }
        }
        T.MenuItem {
            text: "All"
            onTriggered: root.filterRatingRequested(0)
        }
        Repeater {
            model: 5
            delegate: T.MenuItem {
                required property int index
                text: (index + 1) + " " + root.stars(index + 1)
                onTriggered: root.filterRatingRequested(index + 1)
            }
        }
    }

    T.MenuSeparator {
      visible: root.showFilterSection
    }

    T.Menu {
        id: mergeMenu
        title: "Merge Photos"
        enabled: root.selectionCount > 1
        T.MenuItem {
            text: "Panorama"
            icon.source: "qrc:/Main/assets/icons/panorama.svg"
            onTriggered: {
                root.createPanoramaRequested()
                root.close()
            }
        }
        T.MenuItem {
            text: "HDR"
            icon.source: "qrc:/Main/assets/icons/hdr.svg"
            enabled: false
            onTriggered: {
            }
        }
    }


    T.MenuSeparator {}

    T.MenuItem {
        text: "Rotate right"
        icon.source: "qrc:/Main/assets/icons/rotate-cw-menu.svg"
        onTriggered: {
            root.rotateRightRequested()
            root.close()
        }
    }

    T.MenuItem {
        text: "Rotate left"
        icon.source: "qrc:/Main/assets/icons/rotate-ccw.svg"
        icon.color: Theme.foreground
        onTriggered: {
            root.rotateLeftRequested()
            root.close()
        }
    }

    T.MenuItem {
        text: "Flip horizontally"
        icon.source: "qrc:/Main/assets/icons/flip-horizontal-menu.svg"
        onTriggered: {
            root.flipHorizontalRequested()
            root.close()
        }
    }

    T.MenuItem {
        text: "Flip vertically"
        icon.source: "qrc:/Main/assets/icons/flip-vertical-menu.svg"
        onTriggered: {
            root.flipVerticalRequested()
            root.close()
        }
    }
}
