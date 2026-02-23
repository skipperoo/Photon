import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Basic as T
import QtQuick.Dialogs
import Main 1.0

Window {
    id: window
    width: 1280
    height: 800
    visible: true
    title: "Photon"
    color: Theme.background

    property bool showTopbar: true

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
                "modified": file.modified,
                "rating": file.rating || 0
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

    function navigateFilmstrip(offset) {
        if (rawFilesModel.count === 0) return;
        var currentPath = AppState.currentImage;
        var idx = -1;
        for (var i = 0; i < rawFilesModel.count; i++) {
            if (rawFilesModel.get(i).path === currentPath) {
                idx = i;
                break;
            }
        }
        
        var nextIdx = idx + offset;
        if (nextIdx >= 0 && nextIdx < rawFilesModel.count) {
            var nextPath = rawFilesModel.get(nextIdx).path;
            AppState.clearSelection();
            AppState.toggleSelection(nextPath);
            AppState.setCurrentImage(nextPath);
            
            // Ensure the ListView scrolls to show the selected item
            filmstripList.positionViewAtIndex(nextIdx, ListView.Beginning);
        }
    }

    // Refresh files when the folder changes or rating is updated
    Connections {
        target: AppState
        function onCurrentFolderChanged() {
            refreshFiles();
        }
        function onCurrentViewChanged() {
            // Strictly hide topbar in Develop view
            if (AppState.currentView === AppState.ViewState.Develop) {
                window.showTopbar = false
            } else {
                window.showTopbar = true
            }
        }
        function onRatingUpdated() {
            refreshFiles();
        }
    }

    // Global keyboard shortcuts for rating and navigation
    Item {
        Shortcut { sequence: "0"; context: Qt.WindowShortcut; onActivated: AppState.setRatingForSelected(0) }
        Shortcut { sequence: "1"; context: Qt.WindowShortcut; onActivated: AppState.setRatingForSelected(1) }
        Shortcut { sequence: "2"; context: Qt.WindowShortcut; onActivated: AppState.setRatingForSelected(2) }
        Shortcut { sequence: "3"; context: Qt.WindowShortcut; onActivated: AppState.setRatingForSelected(3) }
        Shortcut { sequence: "4"; context: Qt.WindowShortcut; onActivated: AppState.setRatingForSelected(4) }
        Shortcut { sequence: "5"; context: Qt.WindowShortcut; onActivated: AppState.setRatingForSelected(5) }
        
        Shortcut { sequence: "Left"; context: Qt.WindowShortcut; onActivated: window.navigateFilmstrip(-1) }
        Shortcut { sequence: "Right"; context: Qt.WindowShortcut; onActivated: window.navigateFilmstrip(1) }
    }

    // --- Main Layout ---
    Item {
        anchors.fill: parent

        // Hover area to show topbar in Develop view (if we want it there, but currently topbar is hidden in Develop)
        // For now, we disable the hover functionality as requested for Library/Settings.
        MouseArea {
            id: topbarHoverArea
            anchors.top: parent.top
            x: viewportContainer.mapToItem(parent, 0, 0).x
            width: viewportContainer.width
            height: 100
            hoverEnabled: true
            // Only enabled in Develop view if we want hover-to-show there, 
            // but the topbar is explicitly hidden in Develop view (visible: ... check).
            // So we disable this entirely for now to follow the "removing hover functionality" request.
            enabled: false 
            onEntered: window.showTopbar = true
            onExited: {
                if (!topbarMouseArea.containsMouse) {
                    window.showTopbar = false
                }
            }
            z: 1000 
        }

        // --- Content Area ---
        StackLayout {
            id: mainStack
            anchors.fill: parent
            currentIndex: {
                switch (AppState.currentView) {
                    case AppState.ViewState.Welcome: return 0
                    case AppState.ViewState.Library: return 1
                    case AppState.ViewState.Develop: return 2
                    case AppState.ViewState.Settings: return 3
                    default: return 0
                }
            }

            // 0: Welcome View
            WelcomeView {
                onContinueSessionRequested: {
                    AppState.continueSession()
                }
                onSettingsRequested: {
                    AppState.setCurrentView(AppState.ViewState.Settings)
                }
                hasLastSession: AppState.hasLastSession
            }

            // 1: Library View
            LibraryView {
                viewTopPadding: window.showTopbar ? 80 : 20
            }

            // 2: Develop View Layout
            ColumnLayout {
                id: developLayout
                spacing: 0
                
                property int activeSidebar: 1 // 0: Metadata, 1: Edit, 2: Crop, 3: Lens, 4: Presets, 5: Export

                RowLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    // The Viewport
                    Rectangle {
                        id: viewportContainer
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: Theme.background
                        clip: true

                        RawViewport {
                            id: rawViewport
                            anchors.fill: parent
                            anchors.margins: 2
                            source: AppState.currentImage
                            visible: true
                        }

                        // Timer to trigger denoise after interaction stops
                        Timer {
                            id: interactionDenoiseTimer
                            interval: 500 // Wait 500ms after last interaction
                            repeat: false
                            onTriggered: {
                                if (rawViewport.denoiseEnabled && rawViewport.denoiseAmount > 0) {
                                    rawViewport.startAsyncDenoise(AppState.previewDenoiseFull, rawViewport.zoom, rawViewport.visibleImageRect());
                                }
                            }
                        }

                        // Timer to delay double-click zoom (allows detecting if user wants to pan instead)
                        Timer {
                            id: doubleClickZoomTimer
                            interval: 250
                            repeat: false
                            property real targetZoom: 1.0
                            onTriggered: {
                                rawViewport.zoom = targetZoom
                                interactionDenoiseTimer.restart()
                            }
                        }

                        ShaderEffect {
                            anchors.fill: rawViewport
                            property variant source: ShaderEffectSource { 
                                sourceItem: rawViewport
                                hideSource: true
                                live: true
                            }
                            property real exposure: rawViewport.exposure
                            property real contrast: rawViewport.contrast
                            property real highlights: rawViewport.highlights
                            property real shadows: rawViewport.shadows
                            property real whites: rawViewport.whites
                            property real blacks: rawViewport.blacks
                            property real vibrance: rawViewport.vibrance
                            property real saturation: rawViewport.saturation
                            property real temperature: rawViewport.temperature
                            property real tint: rawViewport.tint
                            property real tonemappingEnabled: rawViewport.tonemappingEnabled ? 1.0 : 0.0
                            property real grainAmount: rawViewport.grainAmount
                            property real grainSize: rawViewport.grainSize
                            property real grainRoughness: rawViewport.grainRoughness
                            property real vignetteAmount: rawViewport.vignetteAmount
                            property real vignetteMidpoint: rawViewport.vignetteMidpoint
                            property real vignetteRoundness: rawViewport.vignetteRoundness
                            property real vignetteFeather: rawViewport.vignetteFeather
                            property vector4d imageRect: Qt.vector4d(rawViewport.imageRect.x, rawViewport.imageRect.y, rawViewport.imageRect.width, rawViewport.imageRect.height)
                            property size viewportSize: Qt.size(rawViewport.width, rawViewport.height)
                            property color backgroundColor: Theme.background
                            property real denoiseAmount: rawViewport.denoiseAmount
                            property size sourceSize: Qt.size(rawViewport.sourceWidth, rawViewport.sourceHeight)
                            property real isPreview: rawViewport.showingPreview ? 1.0 : 0.0
                            property int orientation: rawViewport.orientation

                            // HSL Panel
                            property real hslRedHue: rawViewport.hslRedHue
                            property real hslRedSaturation: rawViewport.hslRedSaturation
                            property real hslRedLuminance: rawViewport.hslRedLuminance
                            property real hslOrangeHue: rawViewport.hslOrangeHue
                            property real hslOrangeSaturation: rawViewport.hslOrangeSaturation
                            property real hslOrangeLuminance: rawViewport.hslOrangeLuminance
                            property real hslYellowHue: rawViewport.hslYellowHue
                            property real hslYellowSaturation: rawViewport.hslYellowSaturation
                            property real hslYellowLuminance: rawViewport.hslYellowLuminance
                            property real hslGreenHue: rawViewport.hslGreenHue
                            property real hslGreenSaturation: rawViewport.hslGreenSaturation
                            property real hslGreenLuminance: rawViewport.hslGreenLuminance
                            property real hslAquaHue: rawViewport.hslAquaHue
                            property real hslAquaSaturation: rawViewport.hslAquaSaturation
                            property real hslAquaLuminance: rawViewport.hslAquaLuminance
                            property real hslBlueHue: rawViewport.hslBlueHue
                            property real hslBlueSaturation: rawViewport.hslBlueSaturation
                            property real hslBlueLuminance: rawViewport.hslBlueLuminance
                            property real hslPurpleHue: rawViewport.hslPurpleHue
                            property real hslPurpleSaturation: rawViewport.hslPurpleSaturation
                            property real hslPurpleLuminance: rawViewport.hslPurpleLuminance
                            property real hslMagentaHue: rawViewport.hslMagentaHue
                            property real hslMagentaSaturation: rawViewport.hslMagentaSaturation
                            property real hslMagentaLuminance: rawViewport.hslMagentaLuminance
                            
                            // Color Grading
                            property real cgShadowsHue: rawViewport.cgShadowsHue
                            property real cgShadowsSaturation: rawViewport.cgShadowsSaturation
                            property real cgShadowsLuminance: rawViewport.cgShadowsLuminance
                            property real cgMidtonesHue: rawViewport.cgMidtonesHue
                            property real cgMidtonesSaturation: rawViewport.cgMidtonesSaturation
                            property real cgMidtonesLuminance: rawViewport.cgMidtonesLuminance
                            property real cgHighlightsHue: rawViewport.cgHighlightsHue
                            property real cgHighlightsSaturation: rawViewport.cgHighlightsSaturation
                            property real cgHighlightsLuminance: rawViewport.cgHighlightsLuminance
                            property real cgBalance: rawViewport.cgBalance
                            property real cgBlending: rawViewport.cgBlending
                            
                            fragmentShader: "qrc:/Main/shaders/RawViewport.frag.qsb"
                        }

                        // Interaction Layer
                        MouseArea {
                            anchors.fill: rawViewport
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton
                            scrollGestureEnabled: false
                            preventStealing: true
                            
                            property point lastPos
                            property point startPos
                            property bool isDragging: false
                            
                            onWheel: (wheel) => {
                                wheel.accepted = true;
                                
                                var delta = wheel.angleDelta.y;
                                if (delta === 0) delta = wheel.pixelDelta.y * 5; // Scale pixelDelta appropriately
                                
                                // Apply smooth zoom factor
                                if (delta !== 0) {
                                    // Use a smaller base for touchpad precision if needed
                                    var factor = Math.pow(1.001, delta)
                                    rawViewport.zoom = Math.max(0.1, Math.min(10.0, rawViewport.zoom * factor))
                                    interactionDenoiseTimer.restart();
                                }
                            }
                            
                            onPressed: (mouse) => {
                                lastPos = Qt.point(mouse.x, mouse.y)
                                startPos = Qt.point(mouse.x, mouse.y)
                                isDragging = false
                            }
                            
                            onReleased: (mouse) => {
                                if (rawViewport.isPanning) {
                                    rawViewport.isPanning = false;
                                }
                                
                                if (isDragging) {
                                    interactionDenoiseTimer.restart();
                                }
                                isDragging = false;
                            }
                            
                            onDoubleClicked: (mouse) => {
                                // Cycle: 1.0 -> 2.0 -> 4.0 -> 1.0
                                // Delay zoom to allow detecting if user wants to pan instead
                                if (rawViewport.zoom < 1.0) doubleClickZoomTimer.targetZoom = 1.0;
                                else if (rawViewport.zoom < 2.0) doubleClickZoomTimer.targetZoom = 2.0;
                                else if (rawViewport.zoom < 4.0) doubleClickZoomTimer.targetZoom = 4.0;
                                else doubleClickZoomTimer.targetZoom = 1.0;
                                
                                doubleClickZoomTimer.restart()
                            }
                            
                            onPositionChanged: (mouse) => {
                                if (pressed) {
                                    var dx = mouse.x - startPos.x;
                                    var dy = mouse.y - startPos.y;
                                    var dist = Math.sqrt(dx*dx + dy*dy);
                                    
                                    // Cancel double-click zoom if user starts moving (they want to pan)
                                    if (dist > 5) {
                                        doubleClickZoomTimer.stop()
                                    }
                                    
                                    if (dist > 10) { // 10px threshold
                                        isDragging = true;
                                        if (rawViewport.zoom > 1.0) {
                                            rawViewport.isPanning = true;
                                        }
                                    }

                                    if (isDragging && rawViewport.zoom > 1.0) {
                                        var delta = Qt.point(mouse.x - lastPos.x, mouse.y - lastPos.y)
                                        rawViewport.pan = Qt.point(rawViewport.pan.x + delta.x, rawViewport.pan.y + delta.y)
                                        lastPos = Qt.point(mouse.x, mouse.y)
                                    }
                                }
                            }
                        }

                        Image {
                            id: previewImage
                            anchors.fill: rawViewport
                            anchors.margins: 2
                            source: rawViewport.previewPath ? "file://" + rawViewport.previewPath : ""
                            fillMode: Image.PreserveAspectFit
                            visible: rawViewport.isLoading && status === Image.Ready
                            z: 1
                            
                            opacity: visible ? 1.0 : 0.0
                            Behavior on opacity { NumberAnimation { duration: 250 } }
                        }

                        // Bottom Toolbar
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 36
                            color: Theme.background
                            opacity: 0.8
                            border.color: Theme.border
                            border.width: 0
                            
                            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 20
                                anchors.rightMargin: 20
                                spacing: 12

                                Image {
                                    source: "qrc:/Main/assets/icons/search.svg"
                                    sourceSize: Qt.size(16, 16)
                                    opacity: 0.6
                                }

                                Slider {
                                    id: zoomSlider
                                    Layout.preferredWidth: 200
                                    from: 0.1
                                    to: 10.0
                                    value: rawViewport.zoom
                                    onMoved: {
                                        rawViewport.zoom = value
                                        interactionDenoiseTimer.restart()
                                    }
                                }                                
                                Text {
                                    text: Math.round(rawViewport.zoom * 100) + "%"
                                    color: Theme.foreground
                                    font: Theme.fontSmall
                                    Layout.preferredWidth: 40
                                }
                                
                                Rectangle { width: 1; height: 20; color: Theme.border; Layout.leftMargin: 8; Layout.rightMargin: 8 }
                                T.Button {
                                    id: undoBtn
                                    icon.source: "qrc:/Main/assets/icons/undo.svg"
                                    icon.width: 16
                                    icon.height: 16
                                    icon.color: Theme.foreground
                                    implicitWidth: 24
                                    implicitHeight: 24
                                    onClicked: rawViewport.undo()
                                    flat: true
                                    enabled: rawViewport.canUndo
                                    opacity: enabled ? 1.0 : 0.3
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Undo"
                                    display: AbstractButton.IconOnly
                                    padding: 0
                                    background: null
                                }

                                T.Button {
                                    id: redoBtn
                                    icon.source: "qrc:/Main/assets/icons/redo.svg"
                                    icon.width: 16
                                    icon.height: 16
                                    icon.color: Theme.foreground
                                    implicitWidth: 24
                                    implicitHeight: 24
                                    onClicked: rawViewport.redo()
                                    flat: true
                                    enabled: rawViewport.canRedo
                                    opacity: enabled ? 1.0 : 0.3
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Redo"
                                    display: AbstractButton.IconOnly
                                    padding: 0
                                    background: null
                                }

                                T.Button {
                                    id: restoreBtn
                                    icon.source: "qrc:/Main/assets/icons/rotate-ccw.svg"
                                    icon.width: 16
                                    icon.height: 16
                                    icon.color: "white"
                                    implicitWidth: 24
                                    implicitHeight: 24
                                    onClicked: rawViewport.resetToOriginal()
                                    flat: true
                                    enabled: !rawViewport.isDefault
                                    opacity: enabled ? 1.0 : 0.3
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Restore to Original"
                                    display: AbstractButton.IconOnly
                                    padding: 0
                                    background: null
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: AppState.currentImage !== "" ? AppState.currentImage.split('/').pop() : "No file loaded"
                                    color: Theme.mutedFg
                                    font: Theme.fontSmall
                                }
                            }
                        }
                    }

                    // --- Right Side Tool Stack & Switcher ---
                    Item {
                        Layout.preferredWidth: 320 + 48
                        Layout.fillHeight: true
                        
                        RowLayout {
                            anchors.fill: parent
                            spacing: 0

                            // 1. Tool Stack (320px)
                            StackLayout {
                                id: toolStack
                                Layout.preferredWidth: 320
                                Layout.fillHeight: true
                                currentIndex: developLayout.activeSidebar
                                clip: true

                                // 0: Metadata
                                MetadataPanel {
                                    viewport: rawViewport
                                    viewTopPadding: 10
                                }

                                // 1: Edit (Current Development Tools)
                                DevelopView {
                                    viewport: rawViewport
                                    viewTopPadding: 10
                                }

                                // 2: Crop
                                Rectangle {
                                    color: Theme.background
                                    Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.border }
                                    Text { anchors.centerIn: parent; text: "Crop & Transform (Coming Soon)"; color: Theme.mutedFg }
                                }

                                // 3: Lens
                                Rectangle {
                                    color: Theme.background
                                    Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.border }
                                    Text { anchors.centerIn: parent; text: "Lens Correction (Coming Soon)"; color: Theme.mutedFg }
                                }

                                // 4: Presets
                                PresetPanel {
                                    viewport: rawViewport
                                    viewTopPadding: 10
                                }

                                // 5: Export
                                ExportPanel {
                                    Layout.fillHeight: true
                                }
                            }

                            // 2. Section Switcher (48px)
                            Rectangle {
                                Layout.preferredWidth: 48
                                Layout.fillHeight: true
                                color: Theme.background
                                
                                Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.border }

                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 0
                                    anchors.topMargin: 10

                                    // Mode Buttons
                                    property var modes: [
                                        { icon: "info", index: 0, tooltip: "Metadata" },
                                        { icon: "gear", index: 1, tooltip: "Edit" },
                                        { icon: "crop", index: 2, tooltip: "Crop" },
                                        { icon: "telescope", index: 3, tooltip: "Lens" },
                                        { icon: "bookmark", index: 4, tooltip: "Presets" },
                                        { icon: "download", index: 5, tooltip: "Export" }
                                    ]

                                    Repeater {
                                        model: parent.modes
                                        T.Button {
                                            Layout.preferredWidth: 48
                                            Layout.preferredHeight: 48
                                            flat: true
                                            onClicked: developLayout.activeSidebar = modelData.index
                                            
                                            icon.source: "qrc:/Main/assets/icons/" + modelData.icon + ".svg"
                                            icon.color: Theme.foreground
                                            icon.width: 20
                                            icon.height: 20
                                            display: T.AbstractButton.IconOnly

                                            background: Rectangle {
                                                color: (developLayout.activeSidebar === modelData.index) ? Theme.secondary : "transparent"
                                                Rectangle {
                                                    anchors.right: parent.right; width: 2; height: 24
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    color: Theme.accent
                                                    visible: developLayout.activeSidebar === modelData.index
                                                }
                                            }
                                            
                                            T.ToolTip.visible: hovered
                                            T.ToolTip.text: modelData.tooltip
                                        }
                                    }

                                    Item { Layout.fillHeight: true }

                                    // Global Navigation
                                    T.Button {
                                        Layout.preferredWidth: 48
                                        Layout.preferredHeight: 48
                                        flat: true
                                        onClicked: AppState.setCurrentView(AppState.ViewState.Library)
                                        icon.source: "qrc:/Main/assets/icons/library.svg"
                                        icon.color: Theme.foreground
                                        icon.width: 20
                                        icon.height: 20
                                        display: T.AbstractButton.IconOnly
                                        T.ToolTip.visible: hovered
                                        T.ToolTip.text: "Back to Library"
                                    }

                                    T.Button {
                                        Layout.preferredWidth: 48
                                        Layout.preferredHeight: 48
                                        flat: true
                                        onClicked: AppState.setCurrentView(AppState.ViewState.Settings)
                                        icon.source: "qrc:/Main/assets/icons/settings.svg"
                                        icon.color: Theme.foreground
                                        icon.width: 20
                                        icon.height: 20
                                        display: T.AbstractButton.IconOnly
                                        T.ToolTip.visible: hovered
                                        T.ToolTip.text: "Settings"
                                    }
                                }
                            }
                        }
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
                        id: filmstripList
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
                            
                            property bool isSelected: (model && model.path) ? AppState.selectedImages.indexOf(model.path) !== -1 : false
                            property int itemRating: (model && typeof model.rating !== 'undefined') ? model.rating : 0
                            
                            border.color: AppState.currentImage === (model ? model.path : "") ? Theme.primary : (isSelected ? Theme.accent : "transparent")
                            border.width: (AppState.currentImage === (model ? model.path : "") || isSelected) ? 2 : 0
                            
                            Image {
                                id: filmstripThumbnail
                                anchors.fill: parent
                                anchors.margins: 4
                                fillMode: Image.PreserveAspectFit
                                source: (model && model.path) ? ("image://thumbnail/" + model.path) : ""
                                asynchronous: true
                                visible: status === Image.Ready
                                
                                Text {
                                    anchors.centerIn: parent
                                    text: "RAW"
                                    color: Theme.mutedFg
                                    font: Theme.fontSmall
                                    visible: filmstripThumbnail.status !== Image.Ready
                                }
                            }

                            // Selection overlay
                            Rectangle {
                                anchors.fill: parent
                                color: Theme.accent
                                opacity: 0.1
                                visible: isSelected
                            }

                            // Rating dots
                            Rectangle {
                                anchors.bottom: parent.bottom
                                anchors.left: parent.left
                                anchors.topMargin: 2
                                anchors.bottomMargin: 4
                                anchors.leftMargin: 2
                                radius: 5
                                color: Theme.foreground
                            
                                Row {
                                    spacing: 4
                                    Repeater {
                                        model: itemRating
                                        Text {
                                            text: "★"
                                            color: Theme.accent
                                            font: Theme.fontSmall
                                            width: 4
                                        }
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onClicked: (mouse) => {
                                    if (!model || !model.path) return;
                                    if (mouse.button === Qt.LeftButton) {
                                        if (mouse.modifiers & Qt.ControlModifier) {
                                            AppState.toggleSelection(model.path)
                                        } else if (mouse.modifiers & Qt.ShiftModifier) {
                                            AppState.selectRange(model.path, window.getAllPaths())
                                        } else {
                                            AppState.clearSelection()
                                            AppState.toggleSelection(model.path)
                                            AppState.setCurrentImage(model.path)
                                        }
                                    }
                                }
                            }
                        }
                        leftMargin: 20
                        rightMargin: 20
                    }
                }
            }

            // 3: Settings View
            SettingView {
                viewTopPadding: window.showTopbar ? 80 : 20
            }
        }

        // --- Floating Top Bar ---
        Rectangle {
            id: topbar
            width: parent.width - 32
            height: 64
            anchors.top: parent.top
            anchors.topMargin: window.showTopbar ? 16 : -height - 20
            anchors.horizontalCenter: parent.horizontalCenter
            color: Theme.background // Semi-transparent background
            radius: Theme.radiusLg
            border.color: Theme.border
            border.width: 1
            visible: AppState.currentView !== AppState.ViewState.Welcome && AppState.currentView !== AppState.ViewState.Develop
            z: 1001

            // Hide topbar when mouse leaves it in Develop view
            MouseArea {
                id: topbarMouseArea
                anchors.fill: parent
                hoverEnabled: true
                onExited: {
                    if (AppState.currentView === AppState.ViewState.Develop && !topbarHoverArea.containsMouse) {
                        window.showTopbar = false
                    }
                }
                // Allow events to pass through to buttons
                propagateComposedEvents: true
                onPressed: (mouse) => mouse.accepted = false
                onReleased: (mouse) => mouse.accepted = false
                onClicked: (mouse) => mouse.accepted = false
            }

            // Add a subtle drop shadow
            layer.enabled: true
            
            Behavior on anchors.topMargin {
                NumberAnimation { duration: 300; easing.type: Easing.OutQuint }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 12
                spacing: 20

                // Logo and Name
                RowLayout {
                    spacing: 12
                    Rectangle {
                        width: 32; height: 32; radius: 6
                        // color: Theme.foreground
                        Image {
                            source: "qrc:/Main/assets/icons/photon.png"
                            anchors.fill: parent
                        }
                        // Text { anchors.centerIn: parent; text: "P"; color: Theme.background; font.bold: true }
                    }
                    Text {
                        text: "PHOTON"
                        font.pixelSize: 18
                        font.bold: true
                        font.letterSpacing: 1
                        color: Theme.foreground
                    }
                }

                Item { Layout.fillWidth: true }

                // Navigation Controls
                RowLayout {
                    spacing: 4
                    
                    Button {
                        text: "Library"
                        flat: true
                        font: Theme.fontMedium
                        palette.buttonText: AppState.currentView === AppState.ViewState.Library ? Theme.foreground : Theme.mutedFg
                        onClicked: AppState.setCurrentView(AppState.ViewState.Library)
                        
                        Rectangle {
                            anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
                            width: parent.width * 0.4; height: 2; color: Theme.accent
                            visible: AppState.currentView === AppState.ViewState.Library
                        }
                    }

                    Button {
                        text: "Develop"
                        flat: true
                        font: Theme.fontMedium
                        palette.buttonText: AppState.currentView === AppState.ViewState.Develop ? Theme.foreground : Theme.mutedFg
                        onClicked: AppState.setCurrentView(AppState.ViewState.Develop)

                        Rectangle {
                            anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
                            width: parent.width * 0.4; height: 2; color: Theme.accent
                            visible: AppState.currentView === AppState.ViewState.Develop
                        }
                    }

                    Button {
                        text: "Settings"
                        flat: true
                        font: Theme.fontMedium
                        palette.buttonText: AppState.currentView === AppState.ViewState.Settings ? Theme.foreground : Theme.mutedFg
                        onClicked: AppState.setCurrentView(AppState.ViewState.Settings)

                        Rectangle {
                            anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
                            width: parent.width * 0.4; height: 2; color: Theme.accent
                            visible: AppState.currentView === AppState.ViewState.Settings
                        }
                    }
                }
            }
        }

    }
}
