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
        function onCurrentViewChanged() {
            // Auto-hide topbar when entering Develop view
            if (AppState.currentView === AppState.ViewState.Develop) {
                window.showTopbar = false
            } else {
                window.showTopbar = true
            }
        }
    }

    // --- Main Layout ---
    Item {
        anchors.fill: parent

        // Hover area to show topbar in Develop view
        MouseArea {
            id: topbarHoverArea
            anchors.top: parent.top
            width: parent.width
            height: 100
            hoverEnabled: true
            enabled: AppState.currentView === AppState.ViewState.Develop
            onEntered: if (AppState.currentView === AppState.ViewState.Develop) window.showTopbar = true
            z: 1000 // Ensure it's above content but below topbar if needed
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
                    AppState.setCurrentView(AppState.ViewState.Library)
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
                spacing: 0
                
                RowLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    // The Viewport
                    Rectangle {
                        id: viewportContainer
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "#000000"
                        clip: true

                        RawViewport {
                            id: rawViewport
                            anchors.fill: parent
                            anchors.margins: 2
                            source: AppState.currentImage
                            visible: false
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
                            
                            fragmentShader: "qrc:/Main/shaders/RawViewport.frag.qsb"
                        }

                        // Bottom Toolbar
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 36
                            color: "#CC09090B"
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
                                    onMoved: rawViewport.zoom = value
                                }

                                Rectangle { width: 1; height: 20; color: Theme.border; Layout.leftMargin: 8; Layout.rightMargin: 8 }

                                T.Button {
                                    id: undoBtn
                                    icon.source: "qrc:/Main/assets/icons/undo.svg"
                                    icon.width: 16
                                    icon.height: 16
                                    icon.color: "white"
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
                                    icon.color: "white"
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

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: AppState.currentImage !== "" ? AppState.currentImage.split('/').pop() : "No file loaded"
                                    color: Theme.mutedFg
                                    font: Theme.fontSmall
                                }
                            }
                        }
                    }

                    // Side Tool Panel
                    DevelopView {
                        id: sidebar
                        Layout.preferredWidth: 320
                        Layout.fillHeight: true
                        viewport: rawViewport
                        viewTopPadding: window.showTopbar ? 70 : 10
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
                            border.width: AppState.currentImage === model.path ? 2 : 0
                            
                            Image {
                                id: filmstripThumbnail
                                anchors.fill: parent
                                anchors.margins: 4
                                fillMode: Image.PreserveAspectFit
                                source: "image://thumbnail/" + model.path
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

                            MouseArea {
                                anchors.fill: parent
                                onClicked: AppState.setCurrentImage(model.path)
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
            height: 56
            anchors.top: parent.top
            anchors.topMargin: window.showTopbar ? 16 : -height - 20
            anchors.horizontalCenter: parent.horizontalCenter
            color: "#E609090B" // Semi-transparent background
            radius: Theme.radiusLg
            border.color: Theme.border
            border.width: 1
            visible: AppState.currentView !== AppState.ViewState.Welcome
            z: 1001

            // Hide topbar when mouse leaves it in Develop view
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onExited: {
                    if (AppState.currentView === AppState.ViewState.Develop) {
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
                        width: 28; height: 28; radius: 6
                        color: Theme.foreground
                        Text { anchors.centerIn: parent; text: "P"; color: Theme.background; font.bold: true }
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
                            width: parent.width * 0.4; height: 2; color: Theme.foreground
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
                            width: parent.width * 0.4; height: 2; color: Theme.foreground
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
                            width: parent.width * 0.4; height: 2; color: Theme.foreground
                            visible: AppState.currentView === AppState.ViewState.Settings
                        }
                    }
                }
                
                // Show/Hide Toggle
                Button {
                    text: window.showTopbar ? "↑" : "↓"
                    flat: true
                    Layout.preferredWidth: 32
                    onClicked: window.showTopbar = !window.showTopbar
                }
            }
        }

    }
}
