import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Main

Control {
    id: root

    background: Rectangle {
        color: Theme.background
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 20

        // --- Top Bar ---
        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            Text {
                text: "Library"
                font: Theme.fontLarge
                color: Theme.foreground
            }

            Item { Layout.fillWidth: true } // Spacer

            RowLayout {
                spacing: 8
                Button { text: "Date"; variantOutline: true }
                Button { text: "Name"; variantOutline: true }
                Button { text: "Rating"; variantOutline: true }
            }
        }

        // --- Central Grid ---
        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            cellWidth: 220
            cellHeight: 200
            clip: true

            model: 12 // Dummy 12 items

            delegate: Item {
                width: 200
                height: 180

                Card {
                    anchors.fill: parent
                    anchors.margins: 4
                    
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: Theme.secondary
                        radius: Theme.radiusSm
                        
                        Text {
                            anchors.centerIn: parent
                            text: "RAW"
                            color: Theme.mutedFg
                            font: Theme.fontSmall
                        }
                    }

                    Text {
                        text: "IMG_" + (index + 1).toString().padStart(4, '0') + ".ARW"
                        font: Theme.fontSmall
                        color: Theme.foreground
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }
        }
    }
}
