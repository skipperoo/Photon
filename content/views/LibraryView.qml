import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Main

Control {
    id: root

    background: Rectangle {
        color: Theme.background
    }

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

    // Refresh files when the view becomes visible or when the folder changes
    Connections {
        target: AppState
        function onCurrentFolderChanged() {
            refreshFiles();
        }
    }

    // Refresh files when the view is loaded
    Component.onCompleted: {
        if (AppState.currentFolder !== "") {
            refreshFiles();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 20

        // --- Top Bar ---
        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            Text {
                text: "Library"
                font: Theme.fontLarge
                color: Theme.foreground
            }

            Item { Layout.fillWidth: true } // Spacer

            RowLayout {
                spacing: 8
                Button { text: "Date"; variantOutline: true }
                Button { text: "Name"; variantOutline: true }
                Button { text: "Rating"; variantOutline: true }
            }
        }

        // --- Central Grid ---
        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            cellWidth: 220
            cellHeight: 200
            clip: true

            model: rawFilesModel

            delegate: Item {
                width: 200
                height: 180

                Card {
                    anchors.fill: parent
                    anchors.margins: 4
                    
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8
                        
                        // Thumbnail area
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: Theme.secondary
                            radius: Theme.radiusSm
                            
                            // Thumbnail image
                            Image {
                                id: thumbnailImage
                                anchors.fill: parent
                                anchors.margins: 4
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                
                                // Generate thumbnail asynchronously
                                source: "image://thumbnail/" + model.path
                                
                                // Fallback text when no thumbnail is available
                                Text {
                                    anchors.centerIn: parent
                                    text: "RAW"
                                    color: Theme.mutedFg
                                    font: Theme.fontSmall
                                    visible: thumbnailImage.status !== Image.Ready
                                }
                            }
                        }

                        // Filename
                        Text {
                            Layout.fillWidth: true
                            text: model.name
                            font: Theme.fontSmall
                            color: Theme.foreground
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                    
                    // Handle click to open in Develop view
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            // TODO: Open the selected file in Develop view
                            console.log("Selected file:", model.path);
                        }
                    }
                }
            }
        }
    }
}
