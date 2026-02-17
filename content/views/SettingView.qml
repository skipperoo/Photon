import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Main

Control {
    id: root
    property real viewTopPadding: 0

    background: Rectangle {
        color: Theme.background
    }

    ScrollView {
        anchors.fill: parent
        anchors.topMargin: root.viewTopPadding
        contentWidth: availableWidth

        ColumnLayout {
            width: Math.min(parent.width - 80, 600)
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 40
            spacing: 32

            Text {
                text: "Settings"
                font: Theme.fontLarge
                color: Theme.foreground
            }

            Card {
                Layout.fillWidth: true
                
                ColumnLayout {
                    spacing: 24
                    width: parent.width

                    Text { text: "General"; font: Theme.fontMedium; color: Theme.foreground }

                    ColumnLayout {
                        spacing: 8
                        Layout.fillWidth: true
                        Text { text: "Cache Directory"; font: Theme.fontRegular; color: Theme.mutedFg }
                        RowLayout {
                            Layout.fillWidth: true
                            Input { Layout.fillWidth: true; text: "~/.cache/photon"; readOnly: true }
                            Button { text: "Browse"; variantOutline: true }
                        }
                    }

                    ColumnLayout {
                        spacing: 8
                        Layout.fillWidth: true
                        Text { text: "Maximum Cache Size (GB)"; font: Theme.fontRegular; color: Theme.mutedFg }
                        Slider { 
                            Layout.fillWidth: true; from: 1; to: 100; value: AppState.cacheSizeGB
                            onMoved: AppState.setCacheSizeGB(value)
                        }
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                
                ColumnLayout {
                    spacing: 24
                    width: parent.width

                    Text { text: "Performance"; font: Theme.fontMedium; color: Theme.foreground }

                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "Preferred GPU"; font: Theme.fontRegular; color: Theme.foreground; Layout.fillWidth: true }
                        ComboBox {
                            id: gpuCombo
                            model: ["Auto", "NVIDIA GeForce RTX 3080", "Integrated Graphics"]
                            currentIndex: model.indexOf(AppState.preferredGpu)
                            onActivated: AppState.setPreferredGpu(currentText)
                        }
                    }

                    ColumnLayout {
                        spacing: 8
                        Layout.fillWidth: true
                        Text { text: "Thread Count"; font: Theme.fontRegular; color: Theme.mutedFg }
                        Slider { Layout.fillWidth: true; from: 1; to: 16; value: 8; stepSize: 1; snapMode: Slider.SnapAlways }
                    }
                }
            }
            
            Item { height: 40 } // Bottom padding
        }
    }
}
