import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Main

Control {
    id: root

    // Reference to the viewport being controlled (optional, but useful)
    property var viewport: null

    background: Rectangle {
        color: Theme.background
        border.color: Theme.border
        border.width: 0
        Rectangle { width: 1; height: parent.height; color: Theme.border }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- Header ---
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            color: "transparent"
            
            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 20
                text: "Develop"
                font: Theme.fontLarge
                color: Theme.foreground
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: 0

                // --- Histogram Placeholder ---
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 180
                    Layout.margins: 12
                    color: "#121214"
                    radius: Theme.radius
                    border.color: Theme.border
                    
                    Text {
                        anchors.centerIn: parent
                        text: "Histogram"
                        color: Theme.mutedFg
                        font: Theme.fontSmall
                    }
                }

                // --- Light Section ---
                Collapsible {
                    title: "Light"
                    expanded: true

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        ControlGroup { title: "Exposure"; value: root.viewport ? root.viewport.exposure : 0.0; from: -5; to: 5; onMoved: (v) => { if(root.viewport) root.viewport.exposure = v } }
                        ControlGroup { title: "Contrast"; value: root.viewport ? root.viewport.contrast : 1.0; from: 0; to: 2; onMoved: (v) => { if(root.viewport) root.viewport.contrast = v } }
                        
                        Rectangle { Layout.fillWidth: true; height: 1; color: "#1A1A1C"; Layout.topMargin: 4; Layout.bottomMargin: 4 }

                        ControlGroup { title: "Highlights"; value: root.viewport ? root.viewport.highlights : 0.0; from: -100; to: 100; onMoved: (v) => { if(root.viewport) root.viewport.highlights = v } }
                        ControlGroup { title: "Shadows"; value: root.viewport ? root.viewport.shadows : 0.0; from: -100; to: 100; onMoved: (v) => { if(root.viewport) root.viewport.shadows = v } }
                        ControlGroup { title: "Whites"; value: root.viewport ? root.viewport.whites : 0.0; from: -100; to: 100; onMoved: (v) => { if(root.viewport) root.viewport.whites = v } }
                        ControlGroup { title: "Blacks"; value: root.viewport ? root.viewport.blacks : 0.0; from: -100; to: 100; onMoved: (v) => { if(root.viewport) root.viewport.blacks = v } }
                    }
                }

                // --- Presence Section ---
                Collapsible {
                    title: "Presence"
                    expanded: true

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        ControlGroup { title: "Vibrance"; value: root.viewport ? root.viewport.vibrance : 0.0; from: -100; to: 100; onMoved: (v) => { if(root.viewport) root.viewport.vibrance = v } }
                        ControlGroup { title: "Saturation"; value: root.viewport ? root.viewport.saturation : 0.0; from: -100; to: 100; onMoved: (v) => { if(root.viewport) root.viewport.saturation = v } }
                    }
                }

                // --- Effects Section ---
                Collapsible {
                    title: "Effects"
                    expanded: false

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 16
                        ControlGroup { title: "Clarity"; value: 0; from: -100; to: 100 }
                        ControlGroup { title: "Dehaze"; value: 0; from: -100; to: 100 }
                        ControlGroup { title: "Structure"; value: 0; from: -100; to: 100 }
                    }
                }

                // --- Detail Section ---
                Collapsible {
                    title: "Detail"
                    expanded: false

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 16
                        ControlGroup { title: "Sharpening"; value: 0; from: 0; to: 100 }
                        ControlGroup { title: "Noise Reduction"; value: 0; from: 0; to: 100 }
                    }
                }

                Item { Layout.preferredHeight: 40 }
            }
        }
    }
}
