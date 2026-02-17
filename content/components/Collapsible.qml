import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Main

Control {
    id: root
    Layout.fillWidth: true
    
    property string title: "Section"
    property bool expanded: true
    default property alias content: contentLoader.data

    padding: 0

    background: Rectangle {
        color: "transparent"
        border.color: Theme.border
        border.width: 0
        // Bottom border only
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
            visible: true
        }
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
                color: parent.containsMouse ? "#121214" : "transparent"
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
                anchors.topMargin: 4
                anchors.bottomMargin: 16
            }
        }
    }
}
