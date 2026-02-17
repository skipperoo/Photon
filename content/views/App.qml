import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import Main

Window {
    id: window
    width: 1280
    height: 800
    visible: true
    title: "Photon"
    color: Theme.background

    property string currentView: "develop" // "library", "develop", "settings"

    FileDialog {
        id: fileDialog
        title: "Please choose a RAW file"
        nameFilters: ["RAW files (*.ARW *.CR2 *.NEF *.DNG *.ORF *.RAF)", "All files (*)"]
        onAccepted: {
            rawViewport.source = selectedFile.toString().replace("file://", "")
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // --- Sidebar Navigation ---
        Rectangle {
            Layout.preferredWidth: 64
            Layout.fillHeight: true
            color: Theme.background
            border.color: Theme.border
            border.width: 0
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }

            ColumnLayout {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.topMargin: 20
                spacing: 20

                Button {
                    text: "L"
                    Layout.alignment: Qt.AlignHCenter
                    variantOutline: window.currentView !== "library"
                    onClicked: window.currentView = "library"
                    ToolTip.visible: hovered
                    ToolTip.text: "Library"
                }

                Button {
                    text: "D"
                    Layout.alignment: Qt.AlignHCenter
                    variantOutline: window.currentView !== "develop"
                    onClicked: window.currentView = "develop"
                    ToolTip.visible: hovered
                    ToolTip.text: "Develop"
                }

                Item { Layout.fillHeight: true }

                Button {
                    text: "S"
                    Layout.alignment: Qt.AlignHCenter
                    variantOutline: window.currentView !== "settings"
                    onClicked: window.currentView = "settings"
                    ToolTip.visible: hovered
                    ToolTip.text: "Settings"
                }
                
                Item { height: 20 }
            }
        }

        // --- Main Content Area ---
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: {
                if (window.currentView === "library") return 0
                if (window.currentView === "develop") return 1
                if (window.currentView === "settings") return 2
                return 0
            }

            LibraryView {}

            // Develop View Layout (Holy Grail)
            ColumnLayout {
                spacing: 0
                
                RowLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    // The Viewport
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "#000000"
                        clip: true

                        RawViewport {
                            id: rawViewport
                            anchors.fill: parent
                            anchors.margins: 2
                        }

                        // Toolbar Overlay
                        RowLayout {
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.margins: 20
                            spacing: 10
                            
                            Rectangle {
                                color: "#CC09090B"
                                radius: Theme.radius
                                border.color: Theme.border
                                width: toolbarLayout.implicitWidth + 24
                                height: 44
                                
                                RowLayout {
                                    id: toolbarLayout
                                    anchors.centerIn: parent
                                    spacing: 12
                                    Button { text: "Open"; variantOutline: true; onClicked: fileDialog.open() }
                                    Button { text: "Fit"; variantOutline: true }
                                    Button { text: "1:1"; variantOutline: true }
                                }
                            }
                        }

                        // Info Overlay
                        Text {
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.margins: 20
                            text: rawViewport.source !== "" ? rawViewport.source.split('/').pop() : "No file loaded"
                            color: Theme.mutedFg
                            font: Theme.fontSmall
                        }
                    }

                    // Side Tool Panel
                    DevelopView {
                        id: sidebar
                        Layout.preferredWidth: 320
                        Layout.fillHeight: true
                        onExposureChanged: (val) => rawViewport.exposure = val
                    }
                }

                // Filmstrip
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 120
                    color: Theme.background
                    border.color: Theme.border
                    border.width: 0
                    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }

                    ListView {
                        anchors.fill: parent
                        orientation: ListView.Horizontal
                        spacing: 10
                        model: 10
                        delegate: Rectangle {
                            width: 150
                            height: 100
                            anchors.verticalCenter: parent.verticalCenter
                            color: Theme.secondary
                            radius: Theme.radiusSm
                            border.color: Theme.primary
                            border.width: index === 0 ? 2 : 0
                        }
                        leftMargin: 20
                        rightMargin: 20
                    }
                }
            }

            SettingView {}
        }
    }
}
