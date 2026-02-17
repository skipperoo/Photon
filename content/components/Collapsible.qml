import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Main

Control {
    id: root
    Layout.fillWidth: true
    Layout.leftMargin: 12
    Layout.rightMargin: 12
    Layout.bottomMargin: 12
    
    property string title: "Section"
    property bool expanded: true
    default property alias content: contentLoader.data

    padding: 0

    background: Rectangle {
        color: "#121214" // Lighter than background
        border.color: Theme.border
        border.width: 1
        radius: Theme.radius
    }

    contentItem: ColumnLayout {
        spacing: 0

        // Header
        MouseArea {
            id: headerArea
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            onClicked: root.expanded = !root.expanded
            hoverEnabled: true

            Rectangle {
                anchors.fill: parent
                color: parent.containsMouse ? "#1a1a1c" : "transparent"
                radius: Theme.radius
                
                // Only round top corners if expanded, all if collapsed
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: parent.radius
                    color: parent.color
                    visible: root.expanded
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                Text {
                    text: root.expanded ? "▼" : "▶"
                    font.pixelSize: 10
                    color: Theme.mutedFg
                }

                Text {
                    text: root.title
                    font: Theme.fontMedium
                    color: Theme.foreground
                    Layout.fillWidth: true
                }
            }
            
            // Separator line when expanded
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
                visible: root.expanded
            }
        }

        // Content
        Item {
            id: contentContainer
            Layout.fillWidth: true
            Layout.preferredHeight: root.expanded ? contentLoader.implicitHeight : 0
            clip: true
            
            // Add a subtle animation
            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
            }

            ColumnLayout {
                id: contentLoader
                width: parent.width
                spacing: 16
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 16
                anchors.topMargin: 12
                anchors.bottomMargin: 20 // Added more space at the bottom
            }
        }
    }
}
