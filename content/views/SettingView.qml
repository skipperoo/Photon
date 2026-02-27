import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import Main
import "../components"

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

            // --- Performance Section ---
            Card {
                Layout.fillWidth: true
                
                ColumnLayout {
                    spacing: 24
                    width: parent.width

                    Text { text: "Performance"; font: Theme.fontMedium; color: Theme.foreground }

                    ColumnLayout {
                        spacing: 8
                        Layout.fillWidth: true
                        Text { text: "Preferred GPU"; font: Theme.fontRegular; color: Theme.mutedFg }
                        ComboBox {
                            id: gpuCombo
                            Layout.fillWidth: true
                            model: AppState.availableGpus
                            currentIndex: model.indexOf(AppState.preferredGpu)
                            onActivated: AppState.setPreferredGpu(currentText)
                            
                            background: Rectangle {
                                implicitHeight: 32
                                color: Theme.secondary
                                border.color: Theme.border
                                radius: 4
                            }
                        }
                        Text { 
                            text: "Requires application restart to take effect."
                            font: Theme.fontSmall
                            color: Theme.mutedFg
                        }
                    }

                    ColumnLayout {
                        spacing: 8
                        Layout.fillWidth: true
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "GPU Denoising"; font: Theme.fontRegular; color: Theme.foreground; Layout.fillWidth: true }
                            PhotonSwitch {
                                checked: AppState.useGpuDenoise
                                onClicked: AppState.setUseGpuDenoise(checked)
                            }
                        }
                        Text {
                            text: "If enabled, offloads heavy denoising calculations to the GPU using Vulkan compute shaders."
                            font: Theme.fontSmall
                            color: Theme.mutedFg
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }

                    ColumnLayout {
                        spacing: 8
                        Layout.fillWidth: true
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "Full Quality Denoise Preview"; font: Theme.fontRegular; color: Theme.foreground; Layout.fillWidth: true }
                            PhotonSwitch {
                                checked: AppState.previewDenoiseFull
                                onClicked: AppState.setPreviewDenoiseFull(checked)
                            }
                        }
                        Text {
                            text: "If enabled, uses the high-fidelity 2-step denoiser for previews. Slower but higher quality."
                            font: Theme.fontSmall
                            color: Theme.mutedFg
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }

                    ColumnLayout {
                        spacing: 12
                        Layout.fillWidth: true
                        Text { text: "Maintenance"; font: Theme.fontRegular; color: Theme.mutedFg }
                        PhotonButton {
                            text: "Clear Thumbnail Cache"
                            variantOutline: true
                            enabled: AppState.currentFolder !== ""
                            onClicked: AppState.clearThumbnailCache()
                            Layout.fillWidth: true
                        }
                        Text {
                            text: AppState.currentFolder === "" ? "Open a workspace to enable cache clearing." : "Clears generated thumbnails for the current workspace."
                            font: Theme.fontSmall
                            color: Theme.mutedFg
                        }
                    }
                }
            }

            // --- Appearance Section ---
            Card {
                Layout.fillWidth: true
                
                ColumnLayout {
                    spacing: 24
                    width: parent.width

                    Text { text: "Appearance"; font: Theme.fontMedium; color: Theme.foreground }

                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "Dark Mode"; font: Theme.fontRegular; color: Theme.foreground; Layout.fillWidth: true }
                        PhotonSwitch {
                            checked: AppState.isDarkMode
                            onClicked: AppState.setIsDarkMode(checked)
                        }
                    }

                    ColumnLayout {
                        spacing: 12
                        Layout.fillWidth: true
                        Text { text: "Accent Color"; font: Theme.fontRegular; color: Theme.mutedFg }
                        RowLayout {
                            spacing: 12
                            readonly property var colors: ["#3b82f6", "#f43f5e", "#10b981", "#f59e0b", "#8b5cf6"] // Blue, Rose, Emerald, Amber, Violet
                            Repeater {
                                model: parent.colors
                                Rectangle {
                                    width: 32; height: 32; radius: 16
                                    color: modelData
                                    border.color: AppState.accentColor === modelData ? Theme.foreground : "transparent"
                                    border.width: 2
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: AppState.setAccentColor(modelData)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // --- Diagnostics Section ---
            Card {
                Layout.fillWidth: true
                
                ColumnLayout {
                    spacing: 24
                    width: parent.width

                    Text { text: "Diagnostics"; font: Theme.fontMedium; color: Theme.foreground }

                    ColumnLayout {
                        spacing: 8
                        Layout.fillWidth: true
                        Text { text: "Log File Location"; font: Theme.fontRegular; color: Theme.mutedFg }
                        RowLayout {
                            Layout.fillWidth: true
                            PhotonInput { 
                                Layout.fillWidth: true
                                text: AppState.logLocation
                                readOnly: true 
                            }
                            PhotonButton { 
                                text: "Select Location"; 
                                variantOutline: true 
                                onClicked: logFileDialog.open()
                            }
                        }
                    }

                    ColumnLayout {
                        spacing: 8
                        Layout.fillWidth: true
                        Text { text: "Log Level"; font: Theme.fontRegular; color: Theme.mutedFg }
                        ComboBox {
                            Layout.fillWidth: true
                            model: ["DEBUG", "INFO", "WARNING", "ERROR"]
                            currentIndex: model.indexOf(AppState.logLevel)
                            onActivated: AppState.setLogLevel(currentText)
                            background: Rectangle {
                                implicitHeight: 32
                                color: Theme.secondary
                                border.color: Theme.border
                                radius: 4
                            }
                        }
                    }

                    PhotonButton {
                        text: "Clear Current Log"
                        variantOutline: true
                        onClicked: Logger.clearLog()
                        Layout.fillWidth: true
                    }
                }
            }
            
            Item { height: 20 }

            Rectangle {
                width: parent.width
                height: 1
                color: Theme.border
                opacity: 0.5
            }

            Text {
                text: "Photon v" + AppState.version
                font: Theme.fontSmall
                color: Theme.mutedFg
                anchors.horizontalCenter: parent.horizontalCenter
            }
            
            Item { height: 40 } // Bottom padding
        }
    }

FileDialog {
    id: logFileDialog
    title: "Select Log File Location"
    fileMode: FileDialog.SaveFile
    onAccepted: AppState.setLogLocation(selectedFile)
  }
}
