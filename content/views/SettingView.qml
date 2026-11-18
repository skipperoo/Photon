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

    // A plain Flickable: drag (mouse or touch) on empty space scrolls, like
    // the Library; controls keep their own presses and drags.
    Flickable {
        id: settingsScroll
        anchors.fill: parent
        anchors.topMargin: root.viewTopPadding
        clip: true
        contentWidth: width
        contentHeight: settingsColumn.implicitHeight + settingsColumn.anchors.topMargin
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: PhotonScrollBar {}

        ColumnLayout {
            id: settingsColumn
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
                        PhotonComboBox {
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
                            text: "Clear Cache"
                            variantOutline: true
                            enabled: AppState.currentFolder !== ""
                            onClicked: AppState.clearCache()
                            Layout.fillWidth: true
                        }
                        Text {
                            text: AppState.currentFolder === "" ? "Open a workspace to enable cache clearing." : "Deletes the workspace's generated data: thumbnails, previews, camera looks and AI denoise results. Edits and ratings are kept; everything is rebuilt when needed."
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            font: Theme.fontSmall
                            color: Theme.mutedFg
                        }
                    }
                }
            }

            // --- Denoise Section ---
            Card {
                Layout.fillWidth: true

                ColumnLayout {
                    spacing: 24
                    width: parent.width

                    Text { text: "Denoise"; font: Theme.fontMedium; color: Theme.foreground }

                    ColumnLayout {
                        spacing: 8
                        Layout.fillWidth: true
                        Text { text: "Engine"; font: Theme.fontRegular; color: Theme.mutedFg }
                        PhotonComboBox {
                            id: denoiseEngineCombo
                            readonly property var values: ["ai", "classic"]
                            Layout.fillWidth: true
                            model: ["AI (RawNIND)", "Classic (BM3D)"]
                            currentIndex: AppState.denoiseEngine === "classic" ? 1 : 0
                            onActivated: (index) => AppState.setDenoiseEngine(values[index])

                            background: Rectangle {
                                implicitHeight: 32
                                color: Theme.secondary
                                border.color: Theme.border
                                radius: 4
                            }
                        }
                        Text {
                            text: AppState.aiDenoiseAvailable
                                  ? "AI removes luminance noise on Bayer RAW files, then the Color slider applies chroma BM3D + guided filtering. Photos AI cannot handle (X-Trans, JPEG/TIFF) use Classic automatically."
                                  : "The AI model is not part of this build: Classic is used for every photo."
                            font: Theme.fontSmall
                            color: Theme.mutedFg
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }

                    ColumnLayout {
                        spacing: 8
                        Layout.fillWidth: true
                        visible: AppState.aiDenoiseAvailable
                        Text { text: "AI denoise device"; font: Theme.fontRegular; color: Theme.mutedFg }
                        PhotonComboBox {
                            id: aiDeviceCombo
                            Layout.fillWidth: true
                            model: AppState.aiDenoiseDevices
                            currentIndex: Math.max(0, model.indexOf(AppState.aiDenoiseDevice))
                            onActivated: AppState.setAiDenoiseDevice(currentText)

                            background: Rectangle {
                                implicitHeight: 32
                                color: Theme.secondary
                                border.color: Theme.border
                                radius: 4
                            }
                        }
                        Text {
                            text: "Auto uses the GPU when it gives the same result as the CPU and is faster. Applies to the next photo processed."
                            font: Theme.fontSmall
                            color: Theme.mutedFg
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
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

            // --- Library Section ---
            Card {
                Layout.fillWidth: true

                ColumnLayout {
                    spacing: 24
                    width: parent.width

                    Text { text: "Library"; font: Theme.fontMedium; color: Theme.foreground }

                    ColumnLayout {
                        spacing: 8
                        Layout.fillWidth: true
                        Text { text: "Auto-Scan Interval (seconds)"; font: Theme.fontRegular; color: Theme.mutedFg }
                        RowLayout {
                            spacing: 12
                            PhotonSpinBox {
                                id: scanIntervalSpin
                                from: 1; to: 300; value: AppState.scanIntervalSeconds
                                editable: true
                                onValueModified: AppState.setScanIntervalSeconds(value)
                            }
                            Text {
                                text: "Periodically check for new RAW files in the workspace"
                                font: Theme.fontSmall
                                color: Theme.mutedFg
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }

            // --- Remote Access Section (builds with remote mode) ---
            Loader {
                Layout.fillWidth: true
                active: typeof RemoteServer !== "undefined"
                visible: active
                sourceComponent: Card {
                    ColumnLayout {
                        spacing: 24
                        width: parent.width

                        Text { text: "Remote Access"; font: Theme.fontMedium; color: Theme.foreground }

                        ColumnLayout {
                            spacing: 8
                            Layout.fillWidth: true
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "Use Photon from a browser"; font: Theme.fontRegular; color: Theme.foreground; Layout.fillWidth: true }
                                PhotonSwitch {
                                    checked: RemoteServer.enabled
                                    enabled: RemoteServer.hasPassword
                                    onClicked: RemoteServer.enabled = checked
                                }
                            }
                            Text {
                                text: "Edit on another device on your network, like an iPad, from its browser. "
                                      + "Photon keeps running here and its window shows that it is in use."
                                font: Theme.fontSmall
                                color: Theme.mutedFg
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                        }

                        ColumnLayout {
                            spacing: 8
                            Layout.fillWidth: true
                            Text { text: "Password"; font: Theme.fontRegular; color: Theme.mutedFg }
                            RowLayout {
                                Layout.fillWidth: true
                                PhotonInput {
                                    id: remotePassword
                                    Layout.fillWidth: true
                                    echoMode: TextInput.Password
                                    placeholderText: RemoteServer.hasPassword ? "Enter a new password" : "Choose a password"
                                    onAccepted: setRemotePassword.clicked()
                                }
                                PhotonButton {
                                    id: setRemotePassword
                                    text: RemoteServer.hasPassword ? "Change" : "Set Password"
                                    variantOutline: true
                                    enabled: remotePassword.text.length >= 8
                                    onClicked: {
                                        if (RemoteServer.setPassword(remotePassword.text))
                                            remotePassword.text = ""
                                    }
                                }
                            }
                            Text {
                                text: "At least 8 characters. Changing it signs out connected devices."
                                font: Theme.fontSmall
                                color: Theme.mutedFg
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                        }

                        ColumnLayout {
                            spacing: 8
                            Layout.fillWidth: true
                            Text { text: "Port"; font: Theme.fontRegular; color: Theme.mutedFg }
                            PhotonSpinBox {
                                from: 1024; to: 65535; value: RemoteServer.port
                                editable: true
                                // A port is shown without digit grouping.
                                textFromValue: (value) => value.toString()
                                valueFromText: (text) => parseInt(text, 10)
                                onValueModified: RemoteServer.port = value
                            }
                        }

                        ColumnLayout {
                            spacing: 8
                            Layout.fillWidth: true
                            visible: RemoteServer.running
                            Text { text: "Open on the device"; font: Theme.fontRegular; color: Theme.mutedFg }
                            Repeater {
                                model: RemoteServer.addresses
                                delegate: TextEdit {
                                    required property string modelData
                                    text: modelData
                                    readOnly: true
                                    selectByMouse: true
                                    font: Theme.fontMedium
                                    color: Theme.foreground
                                }
                            }
                            Text {
                                text: RemoteServer.clientConnected ? "Connected from " + RemoteServer.clientAddress
                                                                   : "No device connected"
                                font: Theme.fontSmall
                                color: Theme.mutedFg
                            }
                            Text {
                                text: "The first time, the browser warns about the certificate. Install Photon's "
                                      + "certificate from the link on the login page and trust it (iPad: Settings \u203A "
                                      + "General \u203A VPN & Device Management, then About \u203A Certificate Trust "
                                      + "Settings), or continue past the warning."
                                font: Theme.fontSmall
                                color: Theme.mutedFg
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                        }

                        Text {
                            visible: RemoteServer.error !== ""
                            text: RemoteServer.error
                            font: Theme.fontSmall
                            color: Theme.destructive
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
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
                        PhotonComboBox {
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

    // Drawn inside the UI when non-native (a remote session), not as a desktop window.

    popupType: Popup.Item
    id: logFileDialog
    title: "Select Log File Location"
    fileMode: FileDialog.SaveFile
    onAccepted: AppState.setLogLocation(selectedFile)
  }
}
