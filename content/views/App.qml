import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Main

Window {
    id: window
    width: 1280
    height: 800
    visible: true
    title: "Photon"
    color: Theme.background

    property bool showTopbar: true

    // File scanner for background tasks
    FileScanner {
        id: fileScanner
    }

    // List model to hold the RAW files globally
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
    }

    // Global keyboard shortcuts for rating
    Item {
        Shortcut { sequence: "0"; context: Qt.WindowShortcut; onActivated: AppState.setRatingForSelected(0) }
        Shortcut { sequence: "1"; context: Qt.WindowShortcut; onActivated: AppState.setRatingForSelected(1) }
        Shortcut { sequence: "2"; context: Qt.WindowShortcut; onActivated: AppState.setRatingForSelected(2) }
        Shortcut { sequence: "3"; context: Qt.WindowShortcut; onActivated: AppState.setRatingForSelected(3) }
        Shortcut { sequence: "4"; context: Qt.WindowShortcut; onActivated: AppState.setRatingForSelected(4) }
        Shortcut { sequence: "5"; context: Qt.WindowShortcut; onActivated: AppState.setRatingForSelected(5) }
    }

    // Timer to trigger denoise after interaction stops
    Timer {
        id: interactionDenoiseTimer
        interval: 500
        repeat: false
        onTriggered: {
            if (rawViewport.denoiseEnabled && rawViewport.denoiseAmount > 0) {
                rawViewport.startAsyncDenoise(AppState.previewDenoiseFull, rawViewport.zoom, rawViewport.visibleImageRect());
            }
        }
    }

    // Timer to delay double-click zoom
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

    // --- Main Layout ---
    Item {
        anchors.fill: parent

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // --- Top Navigation Bar ---
            Rectangle {
                id: topBar
                Layout.fillWidth: true
                height: window.showTopbar ? 64 : 0
                color: Theme.background
                visible: window.showTopbar
                z: 1001
                clip: true

                Behavior on height {
                    NumberAnimation { duration: 250; easing.type: Easing.OutCubic }
                }

                // Shadow/Border
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Theme.border
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 24
                    anchors.rightMargin: 24
                    spacing: 24

                    // Logo / App Name
                    Text {
                        text: "PHOTON"
                        color: Theme.foreground
                        font.pixelSize: 18
                        font.bold: true
                        font.letterSpacing: 2
                    }

                    Item { Layout.fillWidth: true }

                    // Navigation Links
                    RowLayout {
                        spacing: 8
                        Button {
                            text: "Library"
                            variantOutline: AppState.currentView !== AppState.ViewState.Library
                            onClicked: AppState.setCurrentView(AppState.ViewState.Library)
                        }
                        Button {
                            text: "Develop"
                            enabled: AppState.currentImage !== ""
                            variantOutline: AppState.currentView !== AppState.ViewState.Develop
                            onClicked: AppState.setCurrentView(AppState.ViewState.Develop)
                        }
                        Button {
                            text: "Settings"
                            variantOutline: AppState.currentView !== AppState.ViewState.Settings
                            onClicked: AppState.setCurrentView(AppState.ViewState.Settings)
                        }
                    }
                }
            }

            // --- View Container ---
            Item {
                id: viewportContainer
                Layout.fillWidth: true
                Layout.fillHeight: true

                StackLayout {
                    anchors.fill: parent
                    currentIndex: {
                        switch(AppState.currentView) {
                            case AppState.ViewState.Welcome: return 0
                            case AppState.ViewState.Library: return 1
                            case AppState.ViewState.Develop: return 2
                            case AppState.ViewState.Settings: return 3
                            default: return 0
                        }
                    }

                    // 0: Welcome View
                    WelcomeView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        hasLastSession: AppState.hasLastSession
                        onContinueSessionRequested: AppState.continueSession()
                        onSettingsRequested: AppState.setCurrentView(AppState.ViewState.Settings)
                    }

                    // 1: Library View
                    LibraryView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        viewTopPadding: 0
                    }

                    // 2: Develop View Orchestration
                    RowLayout {
                        id: developOrchestrator
                        spacing: 0
                        
                        property int activeSidebar: 1 // 0: Metadata, 1: Edit, 2: Crop, 3: Lens, 4: Presets, 5: Export

                        // The Viewport Area
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: Theme.background
                            clip: true

                            RawViewport {
                                id: rawViewport
                                anchors.fill: parent
                                source: AppState.currentImage
                                visible: true
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
                                    if (delta === 0) delta = wheel.pixelDelta.y * 5;
                                    if (delta !== 0) {
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
                                    if (rawViewport.isPanning) rawViewport.isPanning = false;
                                    if (isDragging) interactionDenoiseTimer.restart();
                                    isDragging = false;
                                }
                                
                                onDoubleClicked: (mouse) => {
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
                                        if (dist > 5) doubleClickZoomTimer.stop();
                                        if (dist > 10) {
                                            isDragging = true;
                                            if (rawViewport.zoom > 1.0) rawViewport.isPanning = true;
                                        }
                                        if (isDragging && rawViewport.zoom > 1.0) {
                                            var delta = Qt.point(mouse.x - lastPos.x, mouse.y - lastPos.y)
                                            rawViewport.pan = Qt.point(rawViewport.pan.x + delta.x, rawViewport.pan.y + delta.y)
                                            lastPos = Qt.point(mouse.x, mouse.y)
                                        }
                                    }
                                }
                            }

                            // Bottom Toolbar
                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: 36
                                color: Theme.background
                                opacity: 0.8
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
                                    
                                    Button {
                                        text: "Undo"
                                        enabled: rawViewport.canUndo
                                        onClicked: rawViewport.undo()
                                        variantOutline: true
                                    }
                                    Button {
                                        text: "Redo"
                                        enabled: rawViewport.canRedo
                                        onClicked: rawViewport.redo()
                                        variantOutline: true
                                    }
                                    Button {
                                        text: "Restore"
                                        enabled: !rawViewport.isDefault
                                        onClicked: rawViewport.resetToOriginal()
                                        variantOutline: true
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

                        // Right Side Tool Stack & Switcher
                        RowLayout {
                            spacing: 0
                            Layout.preferredWidth: 320 + 48
                            Layout.fillHeight: true

                            // 1. Tool Stack
                            StackLayout {
                                id: toolStack
                                Layout.preferredWidth: 320
                                Layout.fillHeight: true
                                currentIndex: developOrchestrator.activeSidebar
                                clip: true

                                MetadataPanel { viewport: rawViewport }
                                DevelopView { viewport: rawViewport }
                                Rectangle { color: Theme.background; Text { anchors.centerIn: parent; text: "Crop (Soon)"; color: Theme.mutedFg } }
                                Rectangle { color: Theme.background; Text { anchors.centerIn: parent; text: "Lens (Soon)"; color: Theme.mutedFg } }
                                PresetPanel { viewport: rawViewport }
                                ExportPanel { Layout.fillHeight: true }
                            }

                            // 2. Section Switcher
                            Rectangle {
                                Layout.preferredWidth: 48
                                Layout.fillHeight: true
                                color: Theme.background
                                Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.border }

                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 0
                                    anchors.topMargin: 10

                                    property var modes: [
                                        { icon: "info", index: 0, tooltip: "Metadata" },
                                        { icon: "settings", index: 1, tooltip: "Edit" },
                                        { icon: "crop", index: 2, tooltip: "Crop" },
                                        { icon: "telescope", index: 3, tooltip: "Lens" },
                                        { icon: "bookmark", index: 4, tooltip: "Presets" },
                                        { icon: "download", index: 5, tooltip: "Export" }
                                    ]

                                    Repeater {
                                        model: parent.modes
                                        Button {
                                            Layout.preferredWidth: 48
                                            Layout.preferredHeight: 48
                                            onClicked: developOrchestrator.activeSidebar = modelData.index
                                            variantOutline: developOrchestrator.activeSidebar !== modelData.index
                                            icon.source: "qrc:/Main/assets/icons/" + modelData.icon + ".svg"
                                            icon.color: Theme.foreground
                                            icon.width: 20
                                            icon.height: 20
                                            display: AbstractButton.IconOnly
                                        }
                                    }

                                    Item { Layout.fillHeight: true }

                                    Button {
                                        Layout.preferredWidth: 48
                                        Layout.preferredHeight: 48
                                        onClicked: AppState.setCurrentView(AppState.ViewState.Library)
                                        icon.source: "qrc:/Main/assets/icons/library.svg"
                                        icon.color: Theme.foreground
                                        icon.width: 20
                                        icon.height: 20
                                        display: AbstractButton.IconOnly
                                    }
                                    Button {
                                        Layout.preferredWidth: 48
                                        Layout.preferredHeight: 48
                                        onClicked: AppState.setCurrentView(AppState.ViewState.Settings)
                                        icon.source: "qrc:/Main/assets/icons/gear.svg"
                                        icon.color: Theme.foreground
                                        icon.width: 20
                                        icon.height: 20
                                        display: AbstractButton.IconOnly
                                    }
                                }
                            }
                        }
                    }

                    // 3: Settings View
                    SettingView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        viewTopPadding: 0
                    }
                }
            }

            // --- Bottom Filmstrip (Only in Develop View) ---
            Rectangle {
                id: filmstripContainer
                Layout.fillWidth: true
                height: AppState.currentView === AppState.ViewState.Develop ? 120 : 0
                color: Theme.background
                border.color: Theme.border
                border.width: 1
                visible: height > 0
                clip: true

                Behavior on height {
                    NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
                }

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
                        Row {
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottomMargin: 4
                            spacing: 2
                            Repeater {
                                model: itemRating
                                Rectangle {
                                    width: 4; height: 4; radius: 2
                                    color: "#eab308"
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
    }
}
