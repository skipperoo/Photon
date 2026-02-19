import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as T
import Main

Rectangle {
    id: root
    implicitWidth: 250
    color: Theme.background
    border.color: Theme.border
    border.width: 0
    
    // Left border only
    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
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
                onClicked: presetNameDialog.open()
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

            delegate: T.ItemDelegate {
                width: presetList.width
                height: 32
                
                contentItem: RowLayout {
                    Text {
                        text: modelData
                        color: parent.hovered ? "white" : Theme.foreground
                        font: Theme.fontMedium
                        Layout.fillWidth: true
                    }
                    
                    T.Button {
                        text: "×"
                        visible: parent.parent.hovered
                        implicitWidth: 20
                        implicitHeight: 20
                        onClicked: PresetManager.deletePreset(modelData)
                        background: null
                        contentItem: Text {
                            text: "×"
                            color: Theme.mutedFg
                            font.pixelSize: 18
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                background: Rectangle {
                    color: parent.hovered ? Theme.secondary : "transparent"
                    radius: 4
                }

                onClicked: {
                    var settings = PresetManager.loadPreset(modelData)
                    if (Object.keys(settings).length > 0) {
                        rawViewport.applySettings(settings)
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
            PresetManager.savePreset(presetNameInput.text, rawViewport.currentSettings())
            presetNameInput.text = ""
        }
    }
}
