import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Main
import "../components"

Control {
    id: root
    property real viewTopPadding: 0
    property string menuSourcePath: ""
    readonly property var operatorLabels: window.ratingOperatorLabels
    property int sortProperty: window.sortProperty
    property bool sortAscending: window.sortAscending
    readonly property var sortLabels: ["Name", "Date", "Rating"]

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

    PhotoContextMenu {
        id: thumbnailContextMenu
        selectionCount: AppState.selectionCount
        canCopy: AppState.selectionCount == 1
        canPaste: Object.keys(window.copiedSettings).length > 0
        showFilterSection: true
        filterOperator: window.ratingOperator
        filterRating: window.ratingFilter
        operatorLabels: window.ratingOperatorLabels
        onCopyRequested: {
            if (root.menuSourcePath)
                window.openCopySettingsDialogForPath(root.menuSourcePath)
        }
        onPasteRequested: window.pasteCopiedSettingsToSelection()
        onRatingRequested: (rating) => AppState.setRatingForSelected(rating)
        onFilterOperatorCycleRequested: window.ratingOperator = (window.ratingOperator + 1) % 5
        onFilterRatingRequested: (rating) => window.ratingFilter = rating
        onRotateRightRequested: AppState.rotateSelectedRight("")
        onRotateLeftRequested: AppState.rotateSelectedLeft("")
        onFlipHorizontalRequested: AppState.flipSelectedHorizontal("")
        onFlipVerticalRequested: AppState.flipSelectedVertical("")
        onCreatePanoramaRequested: Panorama.stitchAsync(AppState.selectedImages)
    }

    // Function to refresh the file list
    function refreshFiles() {
        rawFilesModel.clear();
        
        var files = fileScanner.scanForRawFiles(AppState.currentFolder);
        if (!files) return;

        // Filter
        var filtered = [];
        for (var i = 0; i < files.length; i++) {
            var file = files[i];
            var r = file.rating || 0;
            
            var match = true;
            if (window.ratingFilter > 0) {
                switch (window.ratingOperator) {
                    case 0: match = (r === window.ratingFilter); break;
                    case 1: match = (r > window.ratingFilter); break;
                    case 2: match = (r >= window.ratingFilter); break;
                    case 3: match = (r < window.ratingFilter); break;
                    case 4: match = (r <= window.ratingFilter); break;
                }
            } else if (window.ratingFilter === 0 && window.ratingOperator === 0) {
                match = (r === 0);
            }

            if (match) filtered.push(file);
        }

        // Sort
        var dir = root.sortAscending ? 1 : -1;
        filtered.sort(function(a, b) {
            switch (root.sortProperty) {
                case 0: // Name
                    return dir * a.name.localeCompare(b.name);
                case 1: // Date (modified)
                    if (a.modified < b.modified) return -dir;
                    if (a.modified > b.modified) return dir;
                    return 0;
                case 2: // Rating
                    return dir * ((a.rating || 0) - (b.rating || 0));
                default: return 0;
            }
        });

        for (var j = 0; j < filtered.length; j++) {
            var f = filtered[j];
            rawFilesModel.append({
                "path": f.path,
                "name": f.name,
                "size": f.size,
                "modified": f.modified,
                "rating": f.rating || 0
            });
            thumbnailProvider.generateThumbnailAsync(f.path);
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
        function onEditsUpdated() {
            refreshFiles();
        }
    }
    Connections {
        target: window
        function onRatingFilterChanged() {
            refreshFiles();
        }
        function onRatingOperatorChanged() {
            refreshFiles();
        }
    }

    onSortPropertyChanged: refreshFiles()
    onSortAscendingChanged: refreshFiles()

    // Auto-scan timer for new files
    Timer {
        id: scanTimer
        interval: AppState.scanIntervalSeconds * 1000
        repeat: true
        running: AppState.currentFolder !== ""
        onTriggered: {
            var existing = new Set();
            for (var i = 0; i < rawFilesModel.count; i++)
                existing.add(rawFilesModel.get(i).path);
            var allFiles = fileScanner.scanForRawFiles(AppState.currentFolder);
            var newFiles = allFiles.filter(f => !existing.has(f.path));
            for (var j = 0; j < newFiles.length; j++) {
                var f = newFiles[j];
                if (window.ratingFilter > 0) {
                    var r = f.rating || 0;
                    var pass = false;
                    switch (window.ratingOperator) {
                        case 0: pass = (r === window.ratingFilter); break;
                        case 1: pass = (r > window.ratingFilter); break;
                        case 2: pass = (r >= window.ratingFilter); break;
                        case 3: pass = (r < window.ratingFilter); break;
                        case 4: pass = (r <= window.ratingFilter); break;
                    }
                    if (!pass) continue;
                }
                rawFilesModel.append(f);
                thumbnailProvider.generateThumbnailAsync(f.path);
            }
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
            PhotonButton {
                id: filterButton
                text: "Filter: " + (window.ratingFilter === 0 ? "All" : root.operatorLabels[window.ratingOperator] + " " + window.ratingFilter + "★")
                variantOutline: true
                onClicked: filterPopup.open()
                
                Popup {
                    id: filterPopup
                    y: filterButton.height + 5
                    x: filterButton.width - width
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
                            
                            PhotonButton {
                                text: root.operatorLabels[window.ratingOperator]
                                Layout.preferredWidth: 40
                                Layout.preferredHeight: 40
                                variantOutline: true
                                onClicked: {
                                    window.ratingOperator = (window.ratingOperator + 1) % 5
                                    root.refreshFiles()
                                }
                            }

                            RowLayout {
                                spacing: 2
                                Repeater {
                                    model: 6
                                    PhotonButton {
                                        text: index === 0 ? "Off" : "★"
                                        Layout.preferredWidth: 40
                                        Layout.preferredHeight: 40
                                        variantOutline: (index === 0 && window.ratingFilter > 0) || window.ratingFilter < index
                                        onClicked: {
                                            window.ratingFilter = index
                                            root.refreshFiles()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            Item { width: 8 }

            // Sort dropdown
            PhotonButton {
                id: sortButton
                text: "Sort: " + root.sortLabels[root.sortProperty] + " " + (root.sortAscending ? "↑" : "↓")
                variantOutline: true
                onClicked: sortPopup.open()

                Popup {
                    id: sortPopup
                    y: sortButton.height + 5
                    x: sortButton.width - width
                    width: 260
                    padding: 12
                    background: Rectangle {
                        color: Theme.secondary
                        border.color: Theme.border
                        radius: Theme.radius
                    }

                    ColumnLayout {
                        width: parent.width
                        spacing: 8

                        Text { text: "Sort By"; font: Theme.fontSmall; color: Theme.mutedFg }

                        RowLayout {
                            spacing: 4
                            Repeater {
                                model: root.sortLabels
                                PhotonButton {
                                    text: modelData
                                    variantOutline: root.sortProperty !== index
                                    Layout.fillWidth: true
                                    onClicked: window.sortProperty = index
                                }
                            }
                        }

                        Text { text: "Direction"; font: Theme.fontSmall; color: Theme.mutedFg }

                        RowLayout {
                            spacing: 4
                            PhotonButton {
                                text: "↑ Ascending"
                                variantOutline: !root.sortAscending
                                Layout.fillWidth: true
                                onClicked: window.sortAscending = true
                            }
                            PhotonButton {
                                text: "↓ Descending"
                                variantOutline: root.sortAscending
                                Layout.fillWidth: true
                                onClicked: window.sortAscending = false
                            }
                        }
                    }
                }
            }

            Item { width: 8 }

            // Home button
            Button {
                icon.source: "qrc:/Main/assets/icons/home.svg"
                icon.color: Theme.foreground
                icon.width: 20; icon.height: 20
                flat: true
                onClicked: AppState.setCurrentView(AppState.ViewState.Welcome)
                background: Rectangle {
                    color: parent.hovered ? Theme.highlight : "transparent"
                    radius: Theme.radius
                }
                implicitWidth: 36; implicitHeight: 36
            }
        }

        // --- Central Grid ---
        GridView {
            id: grid
            width: parent.width
            Layout.fillHeight: true
            Layout.preferredWidth: Math.floor(parent.width / cellWidth) * cellWidth
            Layout.alignment: Qt.AlignHCenter
            cellWidth: 440
            cellHeight: 400
            clip: true
            // ScrollBar.vertical: PhotonScrollBar {}

            model: rawFilesModel

            delegate: Item {
                width: grid.cellWidth; height: grid.cellHeight
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
                        } else if (mouse.button === Qt.RightButton) {
                            if (!isSelected) {
                                AppState.clearSelection()
                                AppState.toggleSelection(model.path)
                                AppState.setCurrentImage(model.path)
                            }
                            root.menuSourcePath = model.path
                            var p = mapToItem(null, mouse.x, mouse.y)
                            thumbnailContextMenu.openAt(p.x, p.y)
                        }
                    }
                }
            }
        }
    }
}
