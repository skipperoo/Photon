import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import Main 1.0

Window {
    id: window
    width: 1280
    height: 800
    visible: true
    title: "Photon"
    color: Theme.background

    // File scanner for finding RAW files in the current folder
    FileScanner {
        id: fileScanner
    }

    // List model to hold the RAW files
    ListModel {
        id: rawFilesModel
    }

    // Function to refresh the file list
    function refreshFiles() {
        rawFilesModel.clear();
        
        // Scan for RAW files in the current folder
        var files = fileScanner.scanForRawFiles(AppState.currentFolder);
        for (var i = 0; i < files.length; i++) {
            var file = files[i];
            rawFilesModel.append({
                "path": file.path,
                "name": file.name,
                "size": file.size,
                "modified": file.modified
            });
            
            // Pre-generate thumbnails
            thumbnailProvider.generateThumbnailAsync(file.path);
        }
    }

    // Refresh files when the folder changes
    Connections {
        target: AppState
        function onCurrentFolderChanged() {
            refreshFiles();
        }
    }



    RowLayout {
        anchors.fill: parent
        spacing: 0

        // --- Sidebar Navigation (hidden on Welcome screen) ---
    Rectangle {
        Layout.preferredWidth: AppState.currentView === AppState.ViewState.Welcome ? 0 : 64
        Layout.fillHeight: true
        color: Theme.background
        border.color: Theme.border
        border.width: 0
        visible: AppState.currentView !== AppState.ViewState.Welcome
        clip: true
            
            Rectangle { 
                anchors.right: parent.right; 
                width: 1; 
                height: parent.height; 
                color: Theme.border 
                visible: parent.visible
            }

            ColumnLayout {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.topMargin: 20
                spacing: 20
                visible: AppState.currentView !== AppState.ViewState.Welcome

                Button {
                    text: "L"
                    Layout.alignment: Qt.AlignHCenter
                    variantOutline: AppState.currentView !== AppState.ViewState.Library
                    onClicked: AppState.setCurrentView(AppState.ViewState.Library)
                    ToolTip.visible: hovered
                    ToolTip.text: "Library"
                }

                Button {
                    text: "D"
                    Layout.alignment: Qt.AlignHCenter
                    variantOutline: AppState.currentView !== AppState.ViewState.Develop
                    onClicked: AppState.setCurrentView(AppState.ViewState.Develop)
                    ToolTip.visible: hovered
                    ToolTip.text: "Develop"
                }

                Item { Layout.fillHeight: true }

                Button {
                    text: "S"
                    Layout.alignment: Qt.AlignHCenter
                    variantOutline: AppState.currentView !== AppState.ViewState.Welcome
                    onClicked: AppState.setCurrentView(AppState.ViewState.Welcome)
                    ToolTip.visible: hovered
                    ToolTip.text: "Welcome"
                }
                
                Item { height: 20 }
            }
        }

        // --- Main Content Area with Transitions ---
        StackLayout {
            id: mainStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: {
                switch (AppState.currentView) {
                    case AppState.ViewState.Welcome: return 0
                    case AppState.ViewState.Library: return 1
                    case AppState.ViewState.Develop: return 2
                    default: return 0
                }
            }

            // 0: Welcome View
            WelcomeView {
                onContinueSessionRequested: {
                    AppState.setCurrentView(AppState.ViewState.Library)
                }
                onSettingsRequested: {
                    // TODO: Show settings modal
                    console.log("Settings requested")
                }
                hasLastSession: AppState.hasLastSession
            }

            // 1: Library View
            LibraryView {}

            // 2: Develop View Layout (Holy Grail)
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
                        model: rawFilesModel
                        delegate: Rectangle {
                            width: 150
                            height: 100
                            anchors.verticalCenter: parent.verticalCenter
                            color: Theme.secondary
                            radius: Theme.radiusSm
                            border.color: Theme.primary
                            border.width: index === 0 ? 2 : 0
                            
                            // Thumbnail image
                            Image {
                                id: filmstripThumbnail
                                anchors.fill: parent
                                anchors.margins: 4
                                fillMode: Image.PreserveAspectFit
                                source: "image://thumbnail/" + model.path
                                asynchronous: true
                                visible: status === Image.Ready
                                
                                // Fallback text when no thumbnail is available
                                Text {
                                    anchors.centerIn: parent
                                    text: "RAW"
                                    color: Theme.mutedFg
                                    font: Theme.fontSmall
                                    visible: filmstripThumbnail.status !== Image.Ready
                                }
                            }
                        }
                        leftMargin: 20
                        rightMargin: 20
                    }
                }
            }
        }
    }
}
