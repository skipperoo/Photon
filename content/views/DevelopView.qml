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
    property bool maskAltPressed: false

    readonly property string profileValue: (root.viewport && root.viewport.profile) ? root.viewport.profile : "normal"
    readonly property int profileIndex: root.viewport ? root.viewport.profileIndex : 0
    readonly property bool isBlackAndWhite: root.profileIndex === 2

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
                    text: root.viewport && root.viewport.metadata.model ? root.viewport.metadata.make + " " + root.viewport.metadata.model : "Unknown Camera"
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

        // --- Profile Section ---
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: 12
            spacing: 12

            Text {
                text: "Profile"
                font: Theme.fontRegular
                color: Theme.foreground
                Layout.fillWidth: true
            }

            PhotonComboBox {
                id: profileCombo
                Layout.preferredWidth: 180
                model: root.viewport ? root.viewport.profileOptions : []
                textRole: "label"
                valueRole: "value"
                Binding on currentIndex {
                    value: {
                        var options = root.viewport ? root.viewport.profileOptions : []
                        for (var i = 0; i < options.length; ++i) {
                            if (options[i].value === root.profileValue)
                                return i
                        }
                        return 0
                    }
                }
                onActivated: (index) => {
                    if (!root.viewport) return
                    root.viewport.profile = profileCombo.valueAt(index)
                    root.viewport.commitEdit()
                }
            }
        }
          
          // Background Loading & Denoise Indicators
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 10
            
            // Loading Indicator
            RowLayout {
                spacing: 6
                visible: root.viewport ? root.viewport.isLoading : false
                
                Text {
                    text: "Loading RAW..."
                    color: Theme.foreground
                    font: Theme.fontSmall
                }

                Rectangle {
                    width: 14; height: 14; color: "transparent"
                    radius: 7
                    Canvas {
                        anchors.fill: parent
                        onPaint: {
                            var ctx = getContext("2d");
                            ctx.reset();
                            ctx.lineWidth = 1.5;
                            ctx.strokeStyle = Theme.foreground;
                            ctx.beginPath();
                            ctx.arc(7, 7, 6, 0, Math.PI / 2);
                            ctx.stroke();
                        }
                    }
                    RotationAnimation on rotation {
                        from: 0; to: 360; duration: 1000; loops: Animation.Infinite; running: parent.parent.visible
                    }
                }
            }

            // Denoise Indicator
            RowLayout {
                spacing: 6
                visible: root.viewport ? root.viewport.isDenoising : false
                
                Text {
                    text: "Applying denoise..."
                    color: Theme.foreground
                    font: Theme.fontSmall
                }

                Rectangle {
                    width: 14; height: 14; color: "transparent"
                    radius: 7
                    Canvas {
                        anchors.fill: parent
                        onPaint: {
                            var ctx = getContext("2d");
                            ctx.reset();
                            ctx.lineWidth = 1.5;
                            ctx.strokeStyle = Theme.foreground;
                            ctx.beginPath();
                            ctx.arc(7, 7, 6, 0, Math.PI / 2);
                            ctx.stroke();
                        }
                    }
                    RotationAnimation on rotation {
                        from: 0; to: 360; duration: 1000; loops: Animation.Infinite; running: parent.parent.visible
                    }
                }
            }

            // Empty placeholder to maintain layout height when nothing is happening
            Item {
                Layout.preferredHeight: 16
                visible: root.viewport ? (!root.viewport.isDenoising && !root.viewport.isLoading) : true
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.vertical: PhotonScrollBar {}

            ColumnLayout {
                width: parent.width
                spacing: 0

                // --- Light Section ---
                Collapsible {
                    title: "Light"
                    expanded: true

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        Text { text: "White Balance"; font: Theme.fontSmall; color: Theme.mutedFg; Layout.bottomMargin: -8 }
                        ControlGroup { title: "Temperature"; value: root.viewport ? root.viewport.temperature : 0.0; from: -100; to: 100; defaultValue: 0.0; trackRamp: SliderRamps.whiteBalanceTemperature; onMoved: (v) => { if(root.viewport) root.viewport.temperature = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { title: "Tint"; value: root.viewport ? root.viewport.tint : 0.0; from: -100; to: 100; defaultValue: 0.0; trackRamp: SliderRamps.whiteBalanceTint; onMoved: (v) => { if(root.viewport) root.viewport.tint = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        
                        Rectangle { Layout.fillWidth: true; height: 1; color: "#1A1A1C"; Layout.topMargin: 4; Layout.bottomMargin: 4 }

                        ControlGroup { title: "Exposure"; value: root.viewport ? root.viewport.exposure : 0.0; from: -5; to: 5; defaultValue: 0.0; onMoved: (v) => { if(root.viewport) root.viewport.exposure = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { 
                            title: "Contrast"; 
                            value: root.viewport ? (root.viewport.contrast - 1.0) * 200.0 : 0.0; 
                            from: -100; to: 100; 
                            defaultValue: 0.0;
                            onMoved: (v) => { if(root.viewport) root.viewport.contrast = 1.0 + (v / 200.0) }; 
                            onReleased: if(root.viewport) root.viewport.commitEdit() 
                        }
                        
                        Rectangle { Layout.fillWidth: true; height: 1; color: "#1A1A1C"; Layout.topMargin: 4; Layout.bottomMargin: 4 }

                        ControlGroup { title: "Highlights"; value: root.viewport ? root.viewport.highlights : 0.0; from: -100; to: 100; defaultValue: 0.0; onMoved: (v) => { if(root.viewport) root.viewport.highlights = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { title: "Shadows"; value: root.viewport ? root.viewport.shadows : 0.0; from: -100; to: 100; defaultValue: 0.0; onMoved: (v) => { if(root.viewport) root.viewport.shadows = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { title: "Whites"; value: root.viewport ? root.viewport.whites : 0.0; from: -100; to: 100; defaultValue: 0.0; onMoved: (v) => { if(root.viewport) root.viewport.whites = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { title: "Blacks"; value: root.viewport ? root.viewport.blacks : 0.0; from: -100; to: 100; defaultValue: 0.0; onMoved: (v) => { if(root.viewport) root.viewport.blacks = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        //ControlGroup { title: "Adaptation"; value: root.viewport ? root.viewport.adaptation : 9.0; from: 0; to: 100; defaultValue: 9.0; onMoved: (v) => { if(root.viewport) root.viewport.adaptation = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                    }
                }

                // --- Presence Section ---
                Collapsible {
                    title: "Presence"
                    expanded: false
                    visible: !root.isBlackAndWhite

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        ControlGroup { title: "Vibrance"; value: root.viewport ? root.viewport.vibrance : 0.0; from: -100; to: 100; defaultValue: 0.0; onMoved: (v) => { if(root.viewport) root.viewport.vibrance = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { title: "Saturation"; value: root.viewport ? root.viewport.saturation : 0.0; from: -100; to: 100; defaultValue: 0.0; onMoved: (v) => { if(root.viewport) root.viewport.saturation = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                    }
                }

                // --- Tone Curve Section ---
                Collapsible {
                    title: "Tone Curve"
                    expanded: false

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        property bool parametricMode: root.viewport ? root.viewport.toneCurveMode === "parametric" : false

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 20
                            Layout.alignment: Qt.AlignHCenter
                            Repeater {
                                model: [
                                    { "label": "Points", "value": "manual" },
                                    { "label": "Parametric", "value": "parametric" }
                                ]
                                Text {
                                    required property var modelData
                                    text: modelData.label
                                    font: Theme.fontSmall
                                    color: (root.viewport ? root.viewport.toneCurveMode : "manual") === modelData.value ? Theme.foreground : Theme.mutedFg
                                    opacity: (root.viewport ? root.viewport.toneCurveMode : "manual") === modelData.value ? 1.0 : 0.6
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            if (root.viewport) {
                                                root.viewport.toneCurveMode = modelData.value
                                                root.viewport.commitEdit()
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        ToneCurve {
                            Layout.fillWidth: true
                            viewport: root.viewport
                            onEditFinished: if (root.viewport) root.viewport.commitEdit()
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 16
                            visible: parent.parametricMode

                            Rectangle { Layout.fillWidth: true; height: 1; color: "#1A1A1C"; Layout.topMargin: 4; Layout.bottomMargin: 4 }

                            Text { text: "Parametric"; font: Theme.fontSmall; color: Theme.mutedFg; Layout.bottomMargin: -8 }
                            ControlGroup { title: "Highlights"; value: root.viewport ? root.viewport.toneCurveHighlights : 0.0; from: -100; to: 100; defaultValue: 0.0; onMoved: (v) => { if(root.viewport) root.viewport.toneCurveHighlights = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                            ControlGroup { title: "Lights"; value: root.viewport ? root.viewport.toneCurveLights : 0.0; from: -100; to: 100; defaultValue: 0.0; onMoved: (v) => { if(root.viewport) root.viewport.toneCurveLights = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                            ControlGroup { title: "Darks"; value: root.viewport ? root.viewport.toneCurveDarks : 0.0; from: -100; to: 100; defaultValue: 0.0; onMoved: (v) => { if(root.viewport) root.viewport.toneCurveDarks = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                            ControlGroup { title: "Shadows"; value: root.viewport ? root.viewport.toneCurveShadows : 0.0; from: -100; to: 100; defaultValue: 0.0; onMoved: (v) => { if(root.viewport) root.viewport.toneCurveShadows = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }

                            Rectangle { Layout.fillWidth: true; height: 1; color: "#1A1A1C"; Layout.topMargin: 4; Layout.bottomMargin: 4 }

                            Text { text: "Splits"; font: Theme.fontSmall; color: Theme.mutedFg; Layout.bottomMargin: -8 }
                            ControlGroup { title: "Shadows Split"; value: root.viewport ? root.viewport.toneCurveSplitShadows : 25.0; from: 10; to: 70; defaultValue: 25.0; onMoved: (v) => { if(root.viewport) root.viewport.toneCurveSplitShadows = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                            ControlGroup { title: "Mid Split"; value: root.viewport ? root.viewport.toneCurveSplitMid : 50.0; from: 20; to: 80; defaultValue: 50.0; onMoved: (v) => { if(root.viewport) root.viewport.toneCurveSplitMid = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                            ControlGroup { title: "Highlights Split"; value: root.viewport ? root.viewport.toneCurveSplitHighlights : 75.0; from: 30; to: 90; defaultValue: 75.0; onMoved: (v) => { if(root.viewport) root.viewport.toneCurveSplitHighlights = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        }
                    }
                }

                // --- HSL Section ---
                Collapsible {
                    title: "HSL"
                    expanded: false
                    visible: !root.isBlackAndWhite

                    ColumnLayout {
                        id: colorSection
                        Layout.fillWidth: true
                        spacing: 16

                        property int selectedBand: 0
                        readonly property var bandNames: ["Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple", "Magenta"]
                        readonly property var bandColors: ["#ef4444", "#f97316", "#eab308", "#22c55e", "#06b6d4", "#3b82f6", "#a855f7", "#d946ef"]
                        readonly property var bandHues: [0, 25, 60, 115, 180, 225, 280, 330]

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
                            defaultValue: 0.0
                            trackRamp: function(t) { return SliderRamps.bandHue(colorSection.bandHues[colorSection.selectedBand], t) }
                            onMoved: (v) => { if(root.viewport) root.viewport["hsl" + colorSection.bandNames[colorSection.selectedBand] + "Hue"] = v }
                            onReleased: if(root.viewport) root.viewport.commitEdit()
                        }
                        ControlGroup { 
                            title: "Saturation"
                            value: root.viewport ? root.viewport["hsl" + colorSection.bandNames[colorSection.selectedBand] + "Saturation"] : 0
                            from: -100; to: 100
                            defaultValue: 0.0
                            trackRamp: function(t) { return SliderRamps.bandSaturation(colorSection.bandHues[colorSection.selectedBand], t) }
                            onMoved: (v) => { if(root.viewport) root.viewport["hsl" + colorSection.bandNames[colorSection.selectedBand] + "Saturation"] = v }
                            onReleased: if(root.viewport) root.viewport.commitEdit()
                        }
                        ControlGroup { 
                            title: "Luminance"
                            value: root.viewport ? root.viewport["hsl" + colorSection.bandNames[colorSection.selectedBand] + "Luminance"] : 0
                            from: -100; to: 100
                            defaultValue: 0.0
                            trackRamp: function(t) { return SliderRamps.bandLuminance(colorSection.bandHues[colorSection.selectedBand], t) }
                            onMoved: (v) => { if(root.viewport) root.viewport["hsl" + colorSection.bandNames[colorSection.selectedBand] + "Luminance"] = v }
                            onReleased: if(root.viewport) root.viewport.commitEdit()
                        }
                    }
                }

                // --- Black & White Mix Section ---
                Collapsible {
                    title: "Black & White Mix"
                    expanded: false
                    visible: root.isBlackAndWhite

                    ColumnLayout {
                        id: bwMixSection
                        Layout.fillWidth: true
                        spacing: 16

                        readonly property var bands: [
                            { "name": "Red", "color": "#ef4444", "key": "bwMixRed" },
                            { "name": "Orange", "color": "#f97316", "key": "bwMixOrange" },
                            { "name": "Yellow", "color": "#eab308", "key": "bwMixYellow" },
                            { "name": "Green", "color": "#22c55e", "key": "bwMixGreen" },
                            { "name": "Aqua", "color": "#06b6d4", "key": "bwMixAqua" },
                            { "name": "Blue", "color": "#3b82f6", "key": "bwMixBlue" },
                            { "name": "Purple", "color": "#a855f7", "key": "bwMixPurple" },
                            { "name": "Magenta", "color": "#d946ef", "key": "bwMixMagenta" }
                        ]

                        Repeater {
                            model: bwMixSection.bands

                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 12

                                Rectangle {
                                    width: 24; height: 24; radius: 12
                                    color: modelData.color
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                ControlGroup {
                                    Layout.fillWidth: true
                                    title: modelData.name
                                    value: root.viewport ? root.viewport[modelData.key] : 0
                                    from: -100; to: 100
                                    defaultValue: 0.0
                                    onMoved: (v) => { if(root.viewport) root.viewport[modelData.key] = v }
                                    onReleased: if(root.viewport) root.viewport.commitEdit()
                                }
                            }
                        }
                    }
                }

                // --- Color Grading Section ---
                Collapsible {
                    title: "Color Grading"
                    expanded: false
                    visible: !root.isBlackAndWhite

                    ColumnLayout {
                        id: gradingSection
                        Layout.fillWidth: true
                        spacing: 16

                        property int selectedRegion: 0 // 0: Shadows, 1: Midtones, 2: Highlights
                        readonly property var regionNames: ["Shadows", "Midtones", "Highlights"]

                        function zoneHue() {
                            if (!root.viewport)
                                return 0.0
                            return root.viewport["cg" + regionNames[selectedRegion] + "Hue"]
                        }

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
                            defaultValue: 0.0
                            trackRamp: SliderRamps.rainbow
                            onMoved: (v) => { if(root.viewport) root.viewport["cg" + gradingSection.regionNames[gradingSection.selectedRegion] + "Hue"] = v }
                            onReleased: if(root.viewport) root.viewport.commitEdit()
                        }
                        ControlGroup { 
                            title: "Saturation"
                            value: root.viewport ? root.viewport["cg" + gradingSection.regionNames[gradingSection.selectedRegion] + "Saturation"] : 0
                            from: 0; to: 100
                            defaultValue: 0.0
                            trackRamp: function(t) { return SliderRamps.zoneSaturation(gradingSection.zoneHue(), t) }
                            onMoved: (v) => { if(root.viewport) root.viewport["cg" + gradingSection.regionNames[gradingSection.selectedRegion] + "Saturation"] = v }
                            onReleased: if(root.viewport) root.viewport.commitEdit()
                        }
                        ControlGroup { 
                            title: "Luminance"
                            value: root.viewport ? root.viewport["cg" + gradingSection.regionNames[gradingSection.selectedRegion] + "Luminance"] : 0
                            from: -100; to: 100
                            defaultValue: 0.0
                            trackRamp: function(t) { return SliderRamps.zoneLuminance(gradingSection.zoneHue(), t) }
                            onMoved: (v) => { if(root.viewport) root.viewport["cg" + gradingSection.regionNames[gradingSection.selectedRegion] + "Luminance"] = v }
                            onReleased: if(root.viewport) root.viewport.commitEdit()
                        }

                        Rectangle { Layout.fillWidth: true; height: 1; color: "#1A1A1C"; Layout.topMargin: 4; Layout.bottomMargin: 4 }

                        ControlGroup { 
                            title: "Balance"
                            value: root.viewport ? root.viewport.cgBalance : 0.0
                            from: -100; to: 100
                            defaultValue: 0.0
                            onMoved: (v) => { if(root.viewport) root.viewport.cgBalance = v }
                            onReleased: if(root.viewport) root.viewport.commitEdit()
                        }
                        ControlGroup { 
                            title: "Blending"
                            value: root.viewport ? root.viewport.cgBlending : 50.0
                            from: 0; to: 100
                            defaultValue: 50.0
                            onMoved: (v) => { if(root.viewport) root.viewport.cgBlending = v }
                            onReleased: if(root.viewport) root.viewport.commitEdit()
                        }
                    }
                }

                // --- Detail Section ---
                Collapsible {
                    title: "Detail"
                    expanded: false

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 16
                        
                        ControlGroup { 
                            title: "Sharpening"; 
                            value: root.viewport ? root.viewport.sharpness : 0.0; 
                            from: 0; to: 100;
                            defaultValue: 0.0
                            onMoved: (v) => { if(root.viewport) root.viewport.sharpness = v }; 
                            onReleased: if(root.viewport) root.viewport.commitEdit()
                        }

                        ControlGroup {
                            title: "Masking"
                            value: root.viewport ? root.viewport.sharpenMask : 0.0
                            from: 0; to: 100
                            defaultValue: 0.0
                            onMoved: (v) => {
                                if(root.viewport) {
                                    root.viewport.sharpenMask = v;
                                    root.viewport.showSharpenMask = (v > 0 && root.maskAltPressed);
                                }
                            }
                            onReleased: {
                                if(root.viewport) {
                                    root.viewport.showSharpenMask = false;
                                    root.viewport.commitEdit();
                                }
                            }
                        }

                        ControlGroup {
                            title: "Feather"
                            value: root.viewport ? root.viewport.maskFeather : 0.0
                            from: 0; to: 100
                            defaultValue: 0.0
                            onMoved: (v) => {
                                if(root.viewport) {
                                    root.viewport.maskFeather = v;
                                    root.viewport.showSharpenMask = (root.viewport.sharpenMask > 0 && root.maskAltPressed);
                                }
                            }
                            onReleased: {
                                if(root.viewport) {
                                    root.viewport.showSharpenMask = false;
                                    root.viewport.commitEdit();
                                }
                            }
                        }

                        ControlGroup {
                            title: "Focus"
                            value: root.viewport ? root.viewport.focusDetect : 0.0
                            from: 0; to: 100
                            defaultValue: 0.0
                            onMoved: (v) => {
                                if(root.viewport) {
                                    root.viewport.focusDetect = v;
                                    root.viewport.showSharpenMask = (v > 0 && root.maskAltPressed);
                                }
                            }
                            onReleased: {
                                if(root.viewport) {
                                    root.viewport.showSharpenMask = false;
                                    root.viewport.commitEdit();
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Text { 
                                text: "Denoise"
                                font: Theme.fontRegular
                                color: Theme.foreground
                                Layout.fillWidth: true
                            }
                            PhotonSwitch {
                                checked: root.viewport ? root.viewport.denoiseEnabled : false
                                onClicked: if(root.viewport) { root.viewport.denoiseEnabled = checked; root.viewport.commitEdit(); }
                            }
                        }

                        ControlGroup { 
                            title: "Noise Reduction"; 
                            value: root.viewport ? root.viewport.denoiseAmount : 0.0; 
                            from: 0; to: 100; 
                            defaultValue: 0.0
                            enabled: root.viewport ? root.viewport.denoiseEnabled : false
                            opacity: enabled ? 1.0 : 0.5
                            onMoved: (v) => { 
                                if(root.viewport) root.viewport.denoiseAmount = v;
                            } 
                            onReleased: {
                                if(root.viewport) {
                                    if (root.viewport.denoiseAmount > 0 && root.viewport.denoiseEnabled) {
                                        root.viewport.startAsyncDenoise(AppState.previewDenoiseFull, root.viewport.zoom, root.viewport.visibleImageRect());
                                    }
                                    root.viewport.commitEdit();
                                }
                            }
                        }

                        // Advanced Denoise Parameters
                        Rectangle { Layout.fillWidth: true; height: 1; color: "#1A1A1C"; Layout.topMargin: 4; Layout.bottomMargin: 4 }
                        Text {
                            text: "Advanced"
                            font: Theme.fontSmall
                            color: Theme.mutedFg
                            Layout.fillWidth: true
                        }

                        ControlGroup {
                            title: "Search Window"
                            value: root.viewport ? root.viewport.denoiseSearchWindow : 19
                            from: 9; to: 39; stepSize: 2
                            defaultValue: 19
                            enabled: root.viewport ? root.viewport.denoiseEnabled : false
                            opacity: enabled ? 1.0 : 0.5
                            onMoved: (v) => {
                                if(root.viewport) root.viewport.denoiseSearchWindow = Math.round(v);
                            }
                            onReleased: {
                                if(root.viewport) {
                                    if (root.viewport.denoiseAmount > 0 && root.viewport.denoiseEnabled) {
                                        root.viewport.startAsyncDenoise(AppState.previewDenoiseFull, root.viewport.zoom, root.viewport.visibleImageRect());
                                    }
                                    root.viewport.commitEdit();
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            enabled: root.viewport ? root.viewport.denoiseEnabled : false
                            opacity: enabled ? 1.0 : 0.5

                            Text {
                                text: "Group Size"
                                font: Theme.fontRegular
                                color: Theme.foreground
                            }
                            Item { Layout.fillWidth: true }
                            ComboBox {
                                id: groupSizeCombo
                                model: [4, 8, 16]
                                currentIndex: {
                                    let v = root.viewport ? root.viewport.denoiseGroupSize : 16;
                                    return v === 4 ? 0 : (v === 8 ? 1 : 2);
                                }
                                implicitWidth: 70
                                implicitHeight: 28
                                font: Theme.fontSmall
                                onActivated: (index) => {
                                    if (root.viewport) {
                                        root.viewport.denoiseGroupSize = model[index];
                                        if (root.viewport.denoiseAmount > 0 && root.viewport.denoiseEnabled) {
                                            root.viewport.startAsyncDenoise(AppState.previewDenoiseFull, root.viewport.zoom, root.viewport.visibleImageRect());
                                        }
                                        root.viewport.commitEdit();
                                    }
                                }
                                background: Rectangle {
                                    radius: 4
                                    color: Theme.secondary
                                    border.color: groupSizeCombo.activeFocus ? Theme.primary : Theme.border
                                    border.width: 1
                                }
                                contentItem: Text {
                                    text: groupSizeCombo.displayText
                                    font: Theme.fontSmall
                                    color: Theme.foreground
                                    verticalAlignment: Text.AlignVCenter
                                    horizontalAlignment: Text.AlignHCenter
                                    leftPadding: 8
                                }
                            }
                        }

                        ControlGroup {
                            title: "Chroma Radius"
                            value: root.viewport ? root.viewport.denoiseChromaRadius : 4
                            from: 1; to: 16; stepSize: 1
                            defaultValue: 4
                            enabled: root.viewport ? root.viewport.denoiseEnabled : false
                            opacity: enabled ? 1.0 : 0.5
                            onMoved: (v) => {
                                if(root.viewport) root.viewport.denoiseChromaRadius = Math.round(v);
                            }
                            onReleased: {
                                if(root.viewport) {
                                    if (root.viewport.denoiseAmount > 0 && root.viewport.denoiseEnabled) {
                                        root.viewport.startAsyncDenoise(AppState.previewDenoiseFull, root.viewport.zoom, root.viewport.visibleImageRect());
                                    }
                                    root.viewport.commitEdit();
                                }
                            }
                        }

                        ControlGroup {
                            title: "Chroma Denoise"
                            value: root.viewport ? root.viewport.denoiseChromaAmount : 50.0
                            from: 0; to: 100
                            defaultValue: 50.0
                            enabled: root.viewport ? root.viewport.denoiseEnabled : false
                            opacity: enabled ? 1.0 : 0.5
                            onMoved: (v) => {
                                if(root.viewport) root.viewport.denoiseChromaAmount = v;
                            }
                            onReleased: {
                                if(root.viewport) {
                                    if (root.viewport.denoiseAmount > 0 && root.viewport.denoiseEnabled) {
                                        root.viewport.startAsyncDenoise(AppState.previewDenoiseFull, root.viewport.zoom, root.viewport.visibleImageRect());
                                    }
                                    root.viewport.commitEdit();
                                }
                            }
                        }

                        ControlGroup {
                            title: "Chroma BM3D"
                            value: root.viewport ? root.viewport.denoiseChromaBm3d : 50.0
                            from: 0; to: 100
                            defaultValue: 50.0
                            enabled: root.viewport ? root.viewport.denoiseEnabled : false
                            opacity: enabled ? 1.0 : 0.5
                            onMoved: (v) => {
                                if(root.viewport) root.viewport.denoiseChromaBm3d = v;
                            }
                            onReleased: {
                                if(root.viewport) {
                                    if (root.viewport.denoiseAmount > 0 && root.viewport.denoiseEnabled) {
                                        root.viewport.startAsyncDenoise(AppState.previewDenoiseFull, root.viewport.zoom, root.viewport.visibleImageRect());
                                    }
                                    root.viewport.commitEdit();
                                }
                            }
                        }
                    }
                }
                // --- Creative Section ---
                Collapsible {
                    title: "Creative"
                    expanded: false

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        Text { text: "Film Grain"; font: Theme.fontSmall; color: Theme.mutedFg; Layout.bottomMargin: -8 }
                        ControlGroup { title: "Grain Amount"; value: root.viewport ? root.viewport.grainAmount : 0.0; from: 0; to: 100; defaultValue: 0.0; onMoved: (v) => { if(root.viewport) root.viewport.grainAmount = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { title: "Grain Size"; value: root.viewport ? root.viewport.grainSize : 1.0; from: 0.1; to: 5; defaultValue: 1.0; onMoved: (v) => { if(root.viewport) root.viewport.grainSize = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { title: "Grain Roughness"; value: root.viewport ? root.viewport.grainRoughness : 0.5; from: 0; to: 1; defaultValue: 0.5; onMoved: (v) => { if(root.viewport) root.viewport.grainRoughness = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }

                        Rectangle { Layout.fillWidth: true; height: 1; color: "#1A1A1C"; Layout.topMargin: 4; Layout.bottomMargin: 4 }

                        Text { text: "Vignette"; font: Theme.fontSmall; color: Theme.mutedFg; Layout.bottomMargin: -8 }
                        ControlGroup { title: "Vignette Amount"; value: root.viewport ? root.viewport.vignetteAmount : 0.0; from: -100; to: 100; defaultValue: 0.0; onMoved: (v) => { if(root.viewport) root.viewport.vignetteAmount = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { title: "Vignette Midpoint"; value: root.viewport ? root.viewport.vignetteMidpoint : 50.0; from: 0; to: 100; defaultValue: 50.0; onMoved: (v) => { if(root.viewport) root.viewport.vignetteMidpoint = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { title: "Vignette Roundness"; value: root.viewport ? root.viewport.vignetteRoundness : 0.0; from: -100; to: 100; defaultValue: 0.0; onMoved: (v) => { if(root.viewport) root.viewport.vignetteRoundness = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { title: "Vignette Feather"; value: root.viewport ? root.viewport.vignetteFeather : 50.0; from: 1; to: 100; defaultValue: 50.0; onMoved: (v) => { if(root.viewport) root.viewport.vignetteFeather = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                    }
                }

                // --- Effects Section ---
                Collapsible {
                    title: "Effects"
                    expanded: false

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 16
                        ControlGroup { 
                            title: "Clarity"; 
                            value: root.viewport ? root.viewport.clarity : 0.0; 
                            from: -100; to: 100;
                            defaultValue: 0.0
                            onMoved: (v) => { if(root.viewport) root.viewport.clarity = v }; 
                            onReleased: if(root.viewport) root.viewport.commitEdit()
                        }
                        ControlGroup { 
                            title: "Dehaze"; 
                            value: root.viewport ? root.viewport.dehaze : 0.0; 
                            from: -100; to: 100;
                            defaultValue: 0.0
                            onMoved: (v) => { if(root.viewport) root.viewport.dehaze = v }; 
                            onReleased: if(root.viewport) root.viewport.commitEdit()
                        }
                        ControlGroup { 
                            title: "Structure"; 
                            value: root.viewport ? root.viewport.structure : 0.0; 
                            from: -100; to: 100;
                            defaultValue: 0.0
                            onMoved: (v) => { if(root.viewport) root.viewport.structure = v }; 
                            onReleased: if(root.viewport) root.viewport.commitEdit()
                        }
                        ControlGroup { 
                            title: "Centrè"; 
                            value: root.viewport ? root.viewport.centre : 0.0; 
                            from: -100; to: 100;
                            defaultValue: 0.0
                            onMoved: (v) => { if(root.viewport) root.viewport.centre = v }; 
                            onReleased: if(root.viewport) root.viewport.commitEdit()
                        }
                    }
                }

                // --- Camera Look Section ---
                Collapsible {
                    title: "Camera Look"
                    expanded: false

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        ControlGroup {
                            title: "Amount";
                            value: root.viewport ? root.viewport.cameraLookAmount : 0.0;
                            from: 0; to: 100;
                            defaultValue: 0.0
                            onMoved: (v) => { if(root.viewport) root.viewport.cameraLookAmount = v };
                            onReleased: if(root.viewport) root.viewport.commitEdit()
                        }
                        Text {
                            text: root.viewport && root.viewport.cameraLookAvailable
                                  ? "The camera's tone curve, fitted from this photo's embedded JPEG. Lower the amount to keep more highlight headroom."
                                  : "Raise the amount to fit this photo's camera tone curve from its embedded JPEG; photos without one keep the standard rendering."
                            font: Theme.fontSmall
                            color: Theme.mutedFg
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // --- Calibration Section ---
                Collapsible {
                    title: "Calibration"
                    expanded: false

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        Text { text: "Shadows"; font: Theme.fontSmall; color: Theme.mutedFg; Layout.bottomMargin: -8 }
                        ControlGroup { title: "Tint"; value: root.viewport ? root.viewport.calibShadowsTint : 0.0; from: -100; to: 100; defaultValue: 0.0; trackRamp: SliderRamps.shadowsTint; onMoved: (v) => { if(root.viewport) root.viewport.calibShadowsTint = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }

                        Rectangle { Layout.fillWidth: true; height: 1; color: "#1A1A1C"; Layout.topMargin: 4; Layout.bottomMargin: 4 }

                        Text { text: "Red Primary"; font: Theme.fontSmall; color: Theme.mutedFg; Layout.bottomMargin: -8 }
                        ControlGroup { title: "Hue"; value: root.viewport ? root.viewport.calibRedHue : 0.0; from: -100; to: 100; defaultValue: 0.0; trackRamp: function(t) { return SliderRamps.primaryHue(0, t) }; onMoved: (v) => { if(root.viewport) root.viewport.calibRedHue = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { title: "Saturation"; value: root.viewport ? root.viewport.calibRedSat : 0.0; from: -100; to: 100; defaultValue: 0.0; trackRamp: function(t) { return SliderRamps.primarySaturation(0, t) }; onMoved: (v) => { if(root.viewport) root.viewport.calibRedSat = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }

                        Rectangle { Layout.fillWidth: true; height: 1; color: "#1A1A1C"; Layout.topMargin: 4; Layout.bottomMargin: 4 }

                        Text { text: "Green Primary"; font: Theme.fontSmall; color: Theme.mutedFg; Layout.bottomMargin: -8 }
                        ControlGroup { title: "Hue"; value: root.viewport ? root.viewport.calibGreenHue : 0.0; from: -100; to: 100; defaultValue: 0.0; trackRamp: function(t) { return SliderRamps.primaryHue(120, t) }; onMoved: (v) => { if(root.viewport) root.viewport.calibGreenHue = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { title: "Saturation"; value: root.viewport ? root.viewport.calibGreenSat : 0.0; from: -100; to: 100; defaultValue: 0.0; trackRamp: function(t) { return SliderRamps.primarySaturation(120, t) }; onMoved: (v) => { if(root.viewport) root.viewport.calibGreenSat = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }

                        Rectangle { Layout.fillWidth: true; height: 1; color: "#1A1A1C"; Layout.topMargin: 4; Layout.bottomMargin: 4 }

                        Text { text: "Blue Primary"; font: Theme.fontSmall; color: Theme.mutedFg; Layout.bottomMargin: -8 }
                        ControlGroup { title: "Hue"; value: root.viewport ? root.viewport.calibBlueHue : 0.0; from: -100; to: 100; defaultValue: 0.0; trackRamp: function(t) { return SliderRamps.primaryHue(240, t) }; onMoved: (v) => { if(root.viewport) root.viewport.calibBlueHue = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                        ControlGroup { title: "Saturation"; value: root.viewport ? root.viewport.calibBlueSat : 0.0; from: -100; to: 100; defaultValue: 0.0; trackRamp: function(t) { return SliderRamps.primarySaturation(240, t) }; onMoved: (v) => { if(root.viewport) root.viewport.calibBlueSat = v }; onReleased: if(root.viewport) root.viewport.commitEdit() }
                    }
                }


                Item { Layout.preferredHeight: 40 }
            }
        }
    }
}
