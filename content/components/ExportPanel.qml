import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import Main

Control {
    id: root
    
    background: Rectangle {
        color: Theme.background
        border.color: Theme.border
        radius: Theme.radius
    }

FolderDialog {
    id: folderDialog
    title: "Select Export Folder"
    currentFolder: AppState.currentFolder
    onAccepted: {
      folderInput.text = folderDialog.selectedFolder
    }
  }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 25

        Text {
            text: "Export Selected (" + AppState.selectionCount + " images)"
            font: Theme.fontLarge
            color: Theme.foreground
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 15

            // Format & Quality
            RowLayout {
                spacing: 15
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Text { text: "Format"; color: Theme.mutedFg; font: Theme.fontSmall }
                    ComboBox {
                        id: formatCombo
                        model: ["JPG", "TIFF"]
                        Layout.fillWidth: true
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    visible: formatCombo.currentText === "JPG"
                    Text { text: "Quality"; color: Theme.mutedFg; font: Theme.fontSmall }
                    RowLayout {
                        PhotonSlider {
                            id: qualitySlider
                            from: 10; to: 100; value: 90
                            Layout.fillWidth: true
                        }
                        Text { text: Math.round(qualitySlider.value); color: Theme.foreground; font: Theme.fontSmall; width: 30 }
                    }
                }
            }

            // Output Folder - Dedicated Section
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                
                Text { text: "Output Folder"; color: Theme.mutedFg; font: Theme.fontSmall }
                
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    PhotonInput {
                        id: folderInput
                        text: AppState.currentFolder + "/Export"
                        Layout.fillWidth: true
                    }
                    PhotonButton {
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        variantOutline: true
                        onClicked: folderDialog.open()
                        
                        icon.source: "qrc:/Main/assets/icons/folder.svg"
                        icon.color: Theme.foreground
                        icon.width: 20
                        icon.height: 20
                        display: AbstractButton.IconOnly
                    }
                }
            }
        }
        Item { Layout.fillHeight: true }
        // Progress Area
        ColumnLayout {
            Layout.fillWidth: true
            visible: ExportManager.isExporting
            spacing: 10

            RowLayout {
                Text {
                    text: "Exporting..."
                    color: Theme.foreground
                    font: Theme.fontSmall
                }
                Rectangle {
                    width: 14; height: 14; color: "transparent"
                    radius: 7
                    Canvas {
                        anchors.fill: parent
                        onPaint: {
                            var ctx = getContext("2d");
                            ctx.reset();
                            ctx.lineWidth = 1.5;
                            ctx.strokeStyle = Theme.foreground;
                            ctx.beginPath();
                            ctx.arc(7, 7, 6, 0, Math.PI / 2);
                            ctx.stroke();
                        }
                    }
                    RotationAnimation on rotation {
                        from: 0; to: 360; duration: 1000; loops: Animation.Infinite; running: parent.parent.visible
                    }
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: ExportManager.doneCount + " / " + ExportManager.totalCount
                    color: Theme.mutedFg
                    font: Theme.fontSmall
                }
            }

            ProgressBar {
                value: ExportManager.progress
                Layout.fillWidth: true
                background: Rectangle {
                    implicitHeight: 4
                    color: Theme.secondary
                    radius: 2
                }
                contentItem: Item {
                    Rectangle {
                        width: parent.width * ExportManager.progress
                        height: parent.height
                        color: Theme.accent
                        radius: 2
                    }
                }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 10

            PhotonButton {
                text: "Cancel"
                variantOutline: true
                enabled: ExportManager.isExporting
                onClicked: ExportManager.cancelExport()
            }

            PhotonButton {
                text: "Start Export"
                enabled: !ExportManager.isExporting && AppState.selectionCount > 0
                onClicked: {
                    ExportManager.startExport(AppState.selectedImages, folderInput.text, formatCombo.currentText, Math.round(qualitySlider.value))
                }
            }
        }
    }
}
