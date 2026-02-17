import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Main

Control {
    id: root

    // Define signals so App.qml can talk to C++
    signal exposureChanged(real value)
    signal contrastChanged(real value)
    signal exportClicked()

    background: Rectangle {
        color: Theme.background
        border.color: Theme.border
        border.width: 0
        // Draw a border only on the left side
        Rectangle { width: 1; height: parent.height; color: Theme.border }
    }

    ScrollView {
        anchors.fill: parent
        clip: true // Don't let sliders draw outside the panel

        ColumnLayout {
            width: parent.width
            spacing: 24

            // Padding around the whole column
            anchors.margins: 20
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right

            // --- Header ---
            Text {
                text: "Develop"
                font: Theme.fontLarge
                color: Theme.foreground
                Layout.topMargin: 20
                Layout.leftMargin: 20
            }

            // --- Light Panel ---
            Card {
                Layout.fillWidth: true
                Layout.margins: 20

                ColumnLayout {
                    spacing: 16
                    width: parent.width

                    Text { text: "Light"; font: Theme.fontMedium; color: Theme.foreground }

                    // Exposure Control
                    ColumnLayout {
                        spacing: 8
                        Layout.fillWidth: true

                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "Exposure"; font: Theme.fontRegular; color: Theme.mutedFg }
                            Item { Layout.fillWidth: true } // Spacer
                            Text { text: exposureSlider.value.toFixed(2); font: Theme.fontRegular; color: Theme.foreground }
                        }

                        Slider {
                            id: exposureSlider
                            Layout.fillWidth: true
                            from: -5.0; to: 5.0; value: 0.0
                            onMoved: root.exposureChanged(value)
                        }
                    }

                    // Contrast Control
                    ColumnLayout {
                        spacing: 8
                        Layout.fillWidth: true

                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "Contrast"; font: Theme.fontRegular; color: Theme.mutedFg }
                            Item { Layout.fillWidth: true }
                            Text { text: contrastSlider.value.toFixed(2); font: Theme.fontRegular; color: Theme.foreground }
                        }

                        Slider {
                            id: contrastSlider
                            Layout.fillWidth: true
                            from: 0.0; to: 2.0; value: 1.0
                            onMoved: root.contrastChanged(value)
                        }
                    }
                }
            }

            // --- Color Panel ---
            Card {
                Layout.fillWidth: true
                Layout.margins: 20

                ColumnLayout {
                    spacing: 16
                    width: parent.width

                    Text { text: "Color"; font: Theme.fontMedium; color: Theme.foreground }

                    // Temp
                    ColumnLayout {
                        spacing: 8
                        Layout.fillWidth: true
                        Text { text: "Temperature"; font: Theme.fontRegular; color: Theme.mutedFg }
                        Slider { Layout.fillWidth: true; from: 2000; to: 10000; value: 5600 }
                    }

                    // Tint
                    ColumnLayout {
                        spacing: 8
                        Layout.fillWidth: true
                        Text { text: "Tint"; font: Theme.fontRegular; color: Theme.mutedFg }
                        Slider { Layout.fillWidth: true; from: -50; to: 50; value: 0 }
                    }
                }
            }

            // --- Footer / Export ---
            Item { Layout.fillHeight: true; Layout.minimumHeight: 20 } // Spacer

            Button {
                Layout.fillWidth: true
                Layout.margins: 20
                text: "Export Image"
                onClicked: root.exportClicked()
            }

            Item { height: 20 } // Bottom padding
        }
    }
}
