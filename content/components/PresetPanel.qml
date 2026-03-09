import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as T
import QtQuick.Dialogs
import Main

Rectangle {
    id: root
    implicitWidth: 320
    color: Theme.background
    border.color: Theme.border
    border.width: 0
    
    // Left border
    Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.border }

    property var viewport: null
    property real viewTopPadding: 0
    property string presetToDelete: ""
    property var pendingPresetSettings: ({})

    MessageDialog {
        id: deleteConfirmDialog
        title: "Delete Preset"
        text: "Are you sure you want to delete the preset '" + root.presetToDelete + "'?"
        buttons: MessageDialog.Yes | MessageDialog.No
        onButtonClicked: (button, role) => {
            if (button === MessageDialog.Yes) {
                PresetManager.deletePreset(root.presetToDelete)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: root.viewTopPadding
        spacing: 0

        // Pinned Histogram at the top
        Histogram {
            id: headerHistogram
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            Layout.margins: 12
            histogramRed: root.viewport ? root.viewport.histogramRed : []
            histogramGreen: root.viewport ? root.viewport.histogramGreen : []
            histogramBlue: root.viewport ? root.viewport.histogramBlue : []
            histogramLuma: root.viewport ? root.viewport.histogramLuma : []
        }

        // Inner Tool Content
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "PRESETS"
                    color: Theme.mutedFg
                    font.pixelSize: Theme.fontSmall.pixelSize
                    font.bold: true
                    Layout.fillWidth: true
                }
                
                T.Button {
                    id: savePresetBtn
                    text: "+"
                    implicitWidth: 24
                    implicitHeight: 24
                    onClicked: {
                        if (!root.viewport)
                            return
                        root.pendingPresetSettings = ({})
                        settingsSelectionDialog.openForSettings(root.viewport.currentSettings())
                    }
                    T.ToolTip.visible: hovered
                    T.ToolTip.text: "Save Current as Preset"
                    
                    contentItem: Text {
                        text: savePresetBtn.text
                        font: Theme.fontMedium
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    background: Rectangle {
                        color: savePresetBtn.hovered ? Theme.secondary : "transparent"
                        radius: 4
                    }
                }
            }

            ListView {
                id: presetList
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: PresetManager.presets
                spacing: 4
                clip: true
                T.ScrollBar.vertical: PhotonScrollBar {}

                delegate: T.ItemDelegate {
                    width: presetList.width
                    height: 42
                    
                    contentItem: RowLayout {
                        Text {
                            text: modelData
                            color: parent.hovered ? "white" : Theme.foreground
                            font: Theme.fontMedium
                            Layout.fillWidth: true
                        }
                        
                        PhotonButton {
                            visible: parent.parent.hovered
                            implicitWidth: 32
                            implicitHeight: 32
                            onClicked: {
                                root.presetToDelete = modelData
                                deleteConfirmDialog.open()
                            }
                            
                            icon.source: "qrc:/Main/assets/icons/trash.svg"
                            icon.width: 16
                            icon.height: 16
                            icon.color: Theme.foreground
                            variantDestructive: true
                            
                        }
                    }

                    background: Rectangle {
                        color: parent.hovered ? Theme.secondary : "transparent"
                        radius: 4
                    }

                    onClicked: {
                        var settings = PresetManager.loadPreset(modelData)
                        if (Object.keys(settings).length > 0 && root.viewport) {
                            root.viewport.applySettings(settings)
                        }
                    }
                }
                
                Text {
                    anchors.centerIn: parent
                    text: "No presets saved"
                    color: Theme.mutedFg
                    font: Theme.fontSmall
                    visible: presetList.count === 0
                }
            }
        } // End Inner Tool Content
    }

    SettingsSelectionDialog {
        id: settingsSelectionDialog
        dialogTitle: "Select Settings to Save"
        dialogDescription: "Choose which adjustments to include in this preset."
        confirmButtonText: "Continue"
        onSelectionAccepted: (filteredSettings, selectedKeys) => {
            root.pendingPresetSettings = filteredSettings
            presetNameDialog.open()
        }
    }

    T.Dialog {
        id: presetNameDialog
        title: "Save Preset"
        anchors.centerIn: T.Overlay.overlay
        modal: true
        standardButtons: T.Dialog.Save | T.Dialog.Cancel
        
        ColumnLayout {
            spacing: 10
            Text { text: "Enter preset name:"; color: "white" }
            T.TextField {
                id: presetNameInput
                Layout.fillWidth: true
                placeholderText: "Portrait Moody..."
                focus: true
                background: Rectangle {
                    color: Theme.secondary
                    radius: 4
                    border.color: Theme.border
                }
                color: "white"
            }
        }

        onAccepted: {
            var presetName = presetNameInput.text.trim()
            if (presetName.length > 0 && Object.keys(root.pendingPresetSettings).length > 0) {
                PresetManager.savePreset(presetName, root.pendingPresetSettings)
            }
            presetNameInput.text = ""
            root.pendingPresetSettings = ({})
        }

        onRejected: {
            presetNameInput.text = ""
            root.pendingPresetSettings = ({})
        }
    }
}
