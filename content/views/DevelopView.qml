import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Main
import "../components"

Control {
    id: root

    // Reference to the viewport being controlled (optional, but useful)
    property var viewport: null
    property real viewTopPadding: 0

    background: Rectangle {
        color: Theme.background
        border.color: Theme.border
        border.width: 0
        Rectangle { width: 1; height: parent.height; color: Theme.border }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: root.viewTopPadding
        spacing: 0

        // Pinned Histogram at the top
        Histogram {
            id: headerHistogram
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            Layout.margins: 12
            histogramRed: root.viewport ? root.viewport.histogramRed : []
            histogramGreen: root.viewport ? root.viewport.histogramGreen : []
            histogramBlue: root.viewport ? root.viewport.histogramBlue : []
            histogramLuma: root.viewport ? root.viewport.histogramLuma : []
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: 0

                // --- Metadata Section ---
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 80
                    Layout.margins: 12
                    Layout.topMargin: 0
                    Layout.bottomMargin: 20
                    color: "transparent"

                    GridLayout {
                        anchors.fill: parent
                        columns: 2
                        columnSpacing: 20
                        rowSpacing: 4

                        Text { 
                            text: root.viewport && root.viewport.metadata.model ? root.viewport.metadata.model : "Unknown Camera"
                            font: Theme.fontMedium
                            color: Theme.foreground
                            Layout.columnSpan: 2 
                        }
                        
                        Row {
                            spacing: 8
                            Text { text: "ISO"; font: Theme.fontSmall; color: Theme.mutedFg }
                            Text { text: root.viewport && root.viewport.metadata.iso ? root.viewport.metadata.iso : "-"; font: Theme.fontSmall; color: Theme.foreground }
                        }
                        Row {
                            spacing: 8
                            Text { text: "Exp"; font: Theme.fontSmall; color: Theme.mutedFg }
                            Text { text: root.viewport && root.viewport.metadata.exposureTime ? root.viewport.metadata.exposureTime : "-"; font: Theme.fontSmall; color: Theme.foreground }
                        }
                        Row {
                            spacing: 8
                            Text { text: "Ap"; font: Theme.fontSmall; color: Theme.mutedFg }
                            Text { text: root.viewport && root.viewport.metadata.aperture ? root.viewport.metadata.aperture : "-"; font: Theme.fontSmall; color: Theme.foreground }
                        }
                        Row {
                            spacing: 8
                            Text { text: "Focal"; font: Theme.fontSmall; color: Theme.mutedFg }
                            Text { text: root.viewport && root.viewport.metadata.focalLength ? root.viewport.metadata.focalLength : "-"; font: Theme.fontSmall; color: Theme.foreground }
                        }
                    }
                }

                // --- Light Section ---
                Collapsible {
                    title: "Light"
                    expanded: true

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        Text { text: "White Balance"; font: Theme.fontSmall; color: Theme.mutedFg; Layout.bottomMargin: -8 }
                        ControlGroup { title: "Temperature"; value: root.viewport ? root.viewport.temperature : 0.0; from: -100; to: 100; onMoved: (v) => { if(root.viewport) root.viewport.temperature = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { title: "Tint"; value: root.viewport ? root.viewport.tint : 0.0; from: -100; to: 100; onMoved: (v) => { if(root.viewport) root.viewport.tint = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        
                        Rectangle { Layout.fillWidth: true; height: 1; color: "#1A1A1C"; Layout.topMargin: 4; Layout.bottomMargin: 4 }

                        ControlGroup { title: "Exposure"; value: root.viewport ? root.viewport.exposure : 0.0; from: -5; to: 5; onMoved: (v) => { if(root.viewport) root.viewport.exposure = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { title: "Contrast"; value: root.viewport ? root.viewport.contrast : 1.0; from: 0.5; to: 1.5; onMoved: (v) => { if(root.viewport) root.viewport.contrast = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        
                        Rectangle { Layout.fillWidth: true; height: 1; color: "#1A1A1C"; Layout.topMargin: 4; Layout.bottomMargin: 4 }

                        ControlGroup { title: "Highlights"; value: root.viewport ? root.viewport.highlights : 0.0; from: -100; to: 100; onMoved: (v) => { if(root.viewport) root.viewport.highlights = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { title: "Shadows"; value: root.viewport ? root.viewport.shadows : 0.0; from: -100; to: 100; onMoved: (v) => { if(root.viewport) root.viewport.shadows = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { title: "Whites"; value: root.viewport ? root.viewport.whites : 0.0; from: -100; to: 100; onMoved: (v) => { if(root.viewport) root.viewport.whites = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { title: "Blacks"; value: root.viewport ? root.viewport.blacks : 0.0; from: -100; to: 100; onMoved: (v) => { if(root.viewport) root.viewport.blacks = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }

                        Rectangle { Layout.fillWidth: true; height: 1; color: "#1A1A1C"; Layout.topMargin: 4; Layout.bottomMargin: 4 }

                        RowLayout {
                            Layout.fillWidth: true
                            Text { 
                                text: "AgX Tonemapping"
                                font: Theme.fontRegular
                                color: Theme.foreground
                                Layout.fillWidth: true
                            }
                            Switch { 
                                checked: root.viewport ? root.viewport.tonemappingEnabled : false
                                onToggled: if(root.viewport) { root.viewport.tonemappingEnabled = checked; root.viewport.commitEdit(); }
                            }
                        }
                    }
                }

                // --- Presence Section ---
                Collapsible {
                    title: "Presence"
                    expanded: true

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        ControlGroup { title: "Vibrance"; value: root.viewport ? root.viewport.vibrance : 0.0; from: -100; to: 100; onMoved: (v) => { if(root.viewport) root.viewport.vibrance = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { title: "Saturation"; value: root.viewport ? root.viewport.saturation : 0.0; from: -100; to: 100; onMoved: (v) => { if(root.viewport) root.viewport.saturation = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                    }
                }

                // --- Color Section (HSL) ---
                Collapsible {
                    title: "Color"
                    expanded: true

                    ColumnLayout {
                        id: colorSection
                        Layout.fillWidth: true
                        spacing: 16

                        property int selectedBand: 0
                        readonly property var bandNames: ["Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple", "Magenta"]
                        readonly property var bandColors: ["#ef4444", "#f97316", "#eab308", "#22c55e", "#06b6d4", "#3b82f6", "#a855f7", "#d946ef"]

                        Text { text: "HSL Panel"; font: Theme.fontSmall; color: Theme.mutedFg; Layout.bottomMargin: -8 }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Repeater {
                                model: 8
                                Rectangle {
                                    width: 24; height: 24; radius: 12
                                    color: colorSection.bandColors[index]
                                    border.color: colorSection.selectedBand === index ? Theme.foreground : "transparent"
                                    border.width: 2
                                    opacity: colorSection.selectedBand === index ? 1.0 : 0.6
                                    
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: colorSection.selectedBand = index
                                    }
                                }
                            }
                        }

                        Text { 
                            text: colorSection.bandNames[colorSection.selectedBand]
                            font: Theme.fontMedium
                            color: Theme.foreground
                            Layout.alignment: Qt.AlignHCenter
                        }

                        // Dynamic sliders based on selection
                        ControlGroup { 
                            title: "Hue"
                            value: root.viewport ? root.viewport["hsl" + colorSection.bandNames[colorSection.selectedBand] + "Hue"] : 0
                            from: -100; to: 100
                            onMoved: (v) => { if(root.viewport) root.viewport["hsl" + colorSection.bandNames[colorSection.selectedBand] + "Hue"] = v }
                            onReleased: if(root.viewport) root.viewport.commitEdit()
                        }
                        ControlGroup { 
                            title: "Saturation"
                            value: root.viewport ? root.viewport["hsl" + colorSection.bandNames[colorSection.selectedBand] + "Saturation"] : 0
                            from: -100; to: 100
                            onMoved: (v) => { if(root.viewport) root.viewport["hsl" + colorSection.bandNames[colorSection.selectedBand] + "Saturation"] = v }
                            onReleased: if(root.viewport) root.viewport.commitEdit()
                        }
                        ControlGroup { 
                            title: "Luminance"
                            value: root.viewport ? root.viewport["hsl" + colorSection.bandNames[colorSection.selectedBand] + "Luminance"] : 0
                            from: -100; to: 100
                            onMoved: (v) => { if(root.viewport) root.viewport["hsl" + colorSection.bandNames[colorSection.selectedBand] + "Luminance"] = v }
                            onReleased: if(root.viewport) root.viewport.commitEdit()
                        }
                    }
                }

                // --- Color Grading Section ---
                Collapsible {
                    title: "Color Grading"
                    expanded: true

                    ColumnLayout {
                        id: gradingSection
                        Layout.fillWidth: true
                        spacing: 16

                        property int selectedRegion: 0 // 0: Shadows, 1: Midtones, 2: Highlights
                        readonly property var regionNames: ["Shadows", "Midtones", "Highlights"]

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 20
                            Layout.alignment: Qt.AlignHCenter
                            Repeater {
                                model: 3
                                Text {
                                    text: gradingSection.regionNames[index]
                                    font: Theme.fontSmall
                                    color: gradingSection.selectedRegion === index ? Theme.foreground : Theme.mutedFg
                                    opacity: gradingSection.selectedRegion === index ? 1.0 : 0.6
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: gradingSection.selectedRegion = index
                                    }
                                }
                            }
                        }

                        // Dynamic sliders for selected region
                        ControlGroup { 
                            title: "Hue"
                            value: root.viewport ? root.viewport["cg" + gradingSection.regionNames[gradingSection.selectedRegion] + "Hue"] : 0
                            from: 0; to: 360
                            onMoved: (v) => { if(root.viewport) root.viewport["cg" + gradingSection.regionNames[gradingSection.selectedRegion] + "Hue"] = v }
                            onReleased: if(root.viewport) root.viewport.commitEdit()
                        }
                        ControlGroup { 
                            title: "Saturation"
                            value: root.viewport ? root.viewport["cg" + gradingSection.regionNames[gradingSection.selectedRegion] + "Saturation"] : 0
                            from: 0; to: 100
                            onMoved: (v) => { if(root.viewport) root.viewport["cg" + gradingSection.regionNames[gradingSection.selectedRegion] + "Saturation"] = v }
                            onReleased: if(root.viewport) root.viewport.commitEdit()
                        }
                        ControlGroup { 
                            title: "Luminance"
                            value: root.viewport ? root.viewport["cg" + gradingSection.regionNames[gradingSection.selectedRegion] + "Luminance"] : 0
                            from: -100; to: 100
                            onMoved: (v) => { if(root.viewport) root.viewport["cg" + gradingSection.regionNames[gradingSection.selectedRegion] + "Luminance"] = v }
                            onReleased: if(root.viewport) root.viewport.commitEdit()
                        }

                        Rectangle { Layout.fillWidth: true; height: 1; color: "#1A1A1C"; Layout.topMargin: 4; Layout.bottomMargin: 4 }

                        ControlGroup { 
                            title: "Balance"
                            value: root.viewport ? root.viewport.cgBalance : 0.0
                            from: -100; to: 100
                            onMoved: (v) => { if(root.viewport) root.viewport.cgBalance = v }
                            onReleased: if(root.viewport) root.viewport.commitEdit()
                        }
                        ControlGroup { 
                            title: "Blending"
                            value: root.viewport ? root.viewport.cgBlending : 50.0
                            from: 0; to: 100
                            onMoved: (v) => { if(root.viewport) root.viewport.cgBlending = v }
                            onReleased: if(root.viewport) root.viewport.commitEdit()
                        }
                    }
                }

                // --- Effects Section ---
                Collapsible {
                    title: "Effects"
                    expanded: false

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 16
                        ControlGroup { title: "Clarity"; value: 0; from: -100; to: 100 }
                        ControlGroup { title: "Dehaze"; value: 0; from: -100; to: 100 }
                        ControlGroup { title: "Structure"; value: 0; from: -100; to: 100 }
                    }
                }

                // --- Creative Section ---
                Collapsible {
                    title: "Creative"
                    expanded: true

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        Text { text: "Film Grain"; font: Theme.fontSmall; color: Theme.mutedFg; Layout.bottomMargin: -8 }
                        ControlGroup { title: "Grain Amount"; value: root.viewport ? root.viewport.grainAmount : 0.0; from: 0; to: 100; onMoved: (v) => { if(root.viewport) root.viewport.grainAmount = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { title: "Grain Size"; value: root.viewport ? root.viewport.grainSize : 1.0; from: 0.1; to: 5; onMoved: (v) => { if(root.viewport) root.viewport.grainSize = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { title: "Grain Roughness"; value: root.viewport ? root.viewport.grainRoughness : 0.5; from: 0; to: 1; onMoved: (v) => { if(root.viewport) root.viewport.grainRoughness = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }

                        Rectangle { Layout.fillWidth: true; height: 1; color: "#1A1A1C"; Layout.topMargin: 4; Layout.bottomMargin: 4 }

                        Text { text: "Vignette"; font: Theme.fontSmall; color: Theme.mutedFg; Layout.bottomMargin: -8 }
                        ControlGroup { title: "Vignette Amount"; value: root.viewport ? root.viewport.vignetteAmount : 0.0; from: -100; to: 100; onMoved: (v) => { if(root.viewport) root.viewport.vignetteAmount = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { title: "Vignette Midpoint"; value: root.viewport ? root.viewport.vignetteMidpoint : 50.0; from: 0; to: 100; onMoved: (v) => { if(root.viewport) root.viewport.vignetteMidpoint = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { title: "Vignette Roundness"; value: root.viewport ? root.viewport.vignetteRoundness : 0.0; from: -100; to: 100; onMoved: (v) => { if(root.viewport) root.viewport.vignetteRoundness = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { title: "Vignette Feather"; value: root.viewport ? root.viewport.vignetteFeather : 50.0; from: 1; to: 100; onMoved: (v) => { if(root.viewport) root.viewport.vignetteFeather = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                    }
                }

                // --- Detail Section ---
                Collapsible {
                    title: "Detail"
                    expanded: false

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 16
                        ControlGroup { title: "Sharpening"; value: 0; from: 0; to: 100 }
                        ControlGroup { title: "Noise Reduction"; value: 0; from: 0; to: 100 }
                    }
                }

                Item { Layout.preferredHeight: 40 }
            }
        }
    }
}
