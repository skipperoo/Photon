import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import Main
import "../components"

Item {
    id: root

    signal openFolderRequested()
    signal continueSessionRequested()
    signal settingsRequested()

    property bool hasLastSession: false

    Rectangle {
        anchors.fill: parent
        color: Theme.background

        RowLayout {
            anchors.fill: parent
            spacing: 0

            // Left Pane: Aesthetic Photo
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#1a1a2e"

                // Gradient placeholder for aesthetic photo
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#16213e" }
                        GradientStop { position: 0.5; color: "#0f3460" }
                        GradientStop { position: 1.0; color: "#e94560" }
                    }
                }

                // Optional: Add some abstract shapes or overlay
                Rectangle {
                    anchors.centerIn: parent
                    width: Math.min(parent.width, parent.height) * 0.5
                    height: width
                    radius: width / 2
                    color: "#ffffff"
                    opacity: 0.1
                }
            }

            // Right Pane: Action Zone
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.background

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: Theme.spacingLg
                    width: Math.min(400, parent.width * 0.8)

                    // Logo / App Name
                    Text {
                        text: "Photon"
                        font: Theme.fontLarge
                        color: Theme.foreground
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Text {
                        text: "Professional RAW Image Editor"
                        font: Theme.fontRegular
                        color: Theme.mutedFg
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Item { height: Theme.spacingXl }

                    // Continue Session Button (conditional)
                    PhotonButton {
                        id: continueButton
                        Layout.fillWidth: true
                        text: "Continue Session"
                        visible: root.hasLastSession
                        onClicked: {
                          root.continueSessionRequested()
                        }
                    }

                    // Open Folder Button
                    PhotonButton {
                        id: openFolderButton
                        Layout.fillWidth: true
                        text: "Open Folder"
                        variantOutline: true
                        onClicked: folderDialog.open()
                    }

                    Item { height: Theme.spacingXl }

                    // Settings Button
                    PhotonButton {
                        id: settingsButton
                        Layout.alignment: Qt.AlignHCenter
                        text: "⚙ Settings"
                        variantOutline: true
                        onClicked: root.settingsRequested()
                    }
                }
            }
        }
    }

FolderDialog {
    id: folderDialog
    title: "Select a folder containing RAW images"
    onAccepted: {
      // Set the current folder and switch to Library view
      AppState.setCurrentFolder(selectedFolder)
      AppState.setCurrentView(AppState.ViewState.Library)
    }
  }
}
