import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Main

Control {
    id: root
    property real viewTopPadding: 0
    property int ratingFilter: 0
    property int ratingOperator: 2 // 0: =, 1: >, 2: >=, 3: <, 4: <=
    readonly property var operatorLabels: ["=", ">", "≥", "<", "≤"]

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
        if (!files) return;

        for (var i = 0; i < files.length; i++) {
            var file = files[i];
            var r = file.rating || 0;
            
            // Apply rating filter
            var match = true;
            if (root.ratingFilter > 0) {
                switch (root.ratingOperator) {
                    case 0: match = (r === root.ratingFilter); break;
                    case 1: match = (r > root.ratingFilter); break;
                    case 2: match = (r >= root.ratingFilter); break;
                    case 3: match = (r < root.ratingFilter); break;
                    case 4: match = (r <= root.ratingFilter); break;
                }
            } else if (root.ratingFilter === 0 && root.ratingOperator === 0) {
                match = (r === 0);
            }

            if (!match) continue;

            rawFilesModel.append({
                "path": file.path,
                "name": file.name,
                "size": file.size,
                "modified": file.modified,
                "rating": r
            });
            
            // Pre-generate thumbnails
            thumbnailProvider.generateThumbnailAsync(file.path);
        }
    }

    // Function to get all file paths in the model
    function getAllPaths() {
        var paths = [];
        for (var i = 0; i < rawFilesModel.count; i++) {
            paths.push(rawFilesModel.get(i).path);
        }
        return paths;
    }

    // Refresh files when signals are received
    Connections {
        target: AppState
        function onCurrentFolderChanged() {
            refreshFiles();
        }
        function onRatingUpdated() {
            refreshFiles();
        }
    }

    // Keyboard shortcuts for selection
    Shortcut {
        sequences: ["Ctrl+A"]
        onActivated: AppState.selectAll(root.getAllPaths())
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: root.viewTopPadding + 20
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        anchors.bottomMargin: 20
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

            // Filter Control
            Button {
                id: filterButton
                text: "Filter: " + (root.ratingFilter === 0 ? "All" : root.operatorLabels[root.ratingOperator] + " " + root.ratingFilter + "★")
                variantOutline: true
                onClicked: filterPopup.open()
                
                Popup {
                    id: filterPopup
                    y: filterButton.height + 5
                    width: 330
                    padding: 12
                    background: Rectangle {
                        color: Theme.secondary
                        border.color: Theme.border
                        radius: Theme.radius
                    }
                    
                    ColumnLayout {
                        width: parent.width
                        spacing: 10
                        
                        Text { text: "Rating Filter"; color: Theme.foreground; font: Theme.fontMedium }
                        
                        RowLayout {
                            spacing: 8
                            
                            Button {
                                text: root.operatorLabels[root.ratingOperator]
                                Layout.preferredWidth: 40
                                Layout.preferredHeight: 40
                                variantOutline: true
                                onClicked: {
                                    root.ratingOperator = (root.ratingOperator + 1) % 5
                                    root.refreshFiles()
                                }
                            }

                            RowLayout {
                                spacing: 2
                                Repeater {
                                    model: 6
                                    Button {
                                        text: index === 0 ? "Off" : "★"
                                        Layout.preferredWidth: 40
                                        Layout.preferredHeight: 40
                                        variantOutline: (index === 0 && root.ratingFilter > 0) || root.ratingFilter < index
                                        onClicked: {
                                            root.ratingFilter = index
                                            root.refreshFiles()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            Item { width: 20 } // Spacer

            Button { text: "Date"; variantOutline: true }
            Button { text: "Name"; variantOutline: true }
            Button { text: "Rating"; variantOutline: true }
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
                width: 220
                height: 200

                property bool isSelected: AppState.selectedImages.indexOf(model.path) !== -1
                property int itemRating: (model && typeof model.rating !== 'undefined') ? model.rating : 0

                Card {
                    anchors.fill: parent
                    anchors.margins: 10
                    border.color: isSelected ? Theme.accent : Theme.border
                    border.width: isSelected ? 2 : 1
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 0 // Overriding Card default margins
                        spacing: 0

                        // Thumbnail placeholder
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: Theme.secondary
                            radius: Theme.radius
                            clip: true
                            
                            Image {
                                id: thumbImage
                                anchors.fill: parent
                                anchors.margins: isSelected ? 2 : 4
                                fillMode: Image.PreserveAspectFit
                                source: (model && model.path) ? "image://thumbnail/" + model.path : ""
                                asynchronous: true
                                visible: status === Image.Ready
                                
                                // Fallback icon when no thumbnail is available
                                Text {
                                    anchors.centerIn: parent
                                    text: "RAW"
                                    color: Theme.mutedFg
                                    font: Theme.fontSmall
                                    visible: thumbImage.status !== Image.Ready
                                }
                            }

                            // Selection overlay
                            Rectangle {
                                anchors.fill: parent
                                color: Theme.accent
                                opacity: 0.1
                                visible: isSelected
                            }
                        }

                        // Info
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 50
                            Layout.leftMargin: 8
                            Layout.rightMargin: 8
                            Layout.bottomMargin: 8
                            Layout.topMargin: 4
                            spacing: 2

                            Text {
                                text: model ? model.name : ""
                                font: Theme.fontSmall
                                color: isSelected ? Theme.foreground : Theme.mutedFg
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            RowLayout {
                                Text {
                                    text: model ? (model.size / (1024 * 1024)).toFixed(1) + " MB" : ""
                                    font: Theme.fontSmall
                                    color: Theme.mutedFg
                                    Layout.fillWidth: true
                                }
                                // Rating display
                                Row {
                                    spacing: 1
                                    Repeater {
                                        model: 5
                                        Text {
                                            text: "★"
                                            font.pixelSize: 10
                                            color: index < itemRating ? "#eab308" : "#333333"
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onDoubleClicked: (mouse) => {
                        if (!model || !model.path) return;
                        if (mouse.button === Qt.LeftButton) {
                            AppState.setCurrentImage(model.path)
                            AppState.setCurrentView(AppState.ViewState.Develop)
                        }
                    }
                    onClicked: (mouse) => {
                        if (!model || !model.path) return;
                        if (mouse.button === Qt.LeftButton) {
                            if (mouse.modifiers & Qt.ControlModifier) {
                                AppState.toggleSelection(model.path)
                            } else if (mouse.modifiers & Qt.ShiftModifier) {
                                AppState.selectRange(model.path, root.getAllPaths())
                            } else {
                                AppState.clearSelection()
                                AppState.toggleSelection(model.path)
                                AppState.setCurrentImage(model.path)
                            }
                        }
                    }
                }
            }
        }
    }
}
