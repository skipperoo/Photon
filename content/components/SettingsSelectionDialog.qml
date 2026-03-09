import QtQuick
import QtQuick.Controls as T
import QtQuick.Layouts
import Main

T.Dialog {
    id: root
    anchors.centerIn: T.Overlay.overlay
    modal: true
    closePolicy: T.Popup.CloseOnEscape
    padding: 0
    width: Math.min(1120, T.Overlay.overlay ? T.Overlay.overlay.width - 40 : 1120)
    height: Math.min(760, T.Overlay.overlay ? T.Overlay.overlay.height - 40 : 760)

    property var sourceSettings: ({})
    property var selectedState: ({})
    property var sectionColumns: []
    property int minSectionColumnWidth: 250
    property string dialogTitle: "Select Settings"
    property string dialogDescription: "Choose which settings to include."
    property string confirmButtonText: "Continue"

    readonly property var defaultSectionDefinitions: [
        {
            "label": "Light",
            "items": [
                { "key": "exposure", "label": "Exposure" },
                { "key": "contrast", "label": "Contrast" },
                { "key": "highlights", "label": "Highlights" },
                { "key": "shadows", "label": "Shadows" },
                { "key": "whites", "label": "Whites" },
                { "key": "blacks", "label": "Blacks" },
                { "key": "adaptation", "label": "Adaptation" },
                { "key": "tonemappingEnabled", "label": "AgX Tonemapping" }
            ]
        },
        {
            "label": "Presence",
            "items": [
                { "key": "vibrance", "label": "Vibrance" },
                { "key": "saturation", "label": "Saturation" }
            ]
        },
        {
            "label": "Color",
            "items": [
                { "key": "temperature", "label": "Temperature" },
                { "key": "tint", "label": "Tint" }
            ]
        },
        {
            "label": "HSL/Color",
            "items": [
                {
                    "key": "colorCorrection",
                    "label": "Color Correction",
                    "keys": [
                        "hslRedHue", "hslRedSaturation", "hslRedLuminance",
                        "hslOrangeHue", "hslOrangeSaturation", "hslOrangeLuminance",
                        "hslYellowHue", "hslYellowSaturation", "hslYellowLuminance",
                        "hslGreenHue", "hslGreenSaturation", "hslGreenLuminance",
                        "hslAquaHue", "hslAquaSaturation", "hslAquaLuminance",
                        "hslBlueHue", "hslBlueSaturation", "hslBlueLuminance",
                        "hslPurpleHue", "hslPurpleSaturation", "hslPurpleLuminance",
                        "hslMagentaHue", "hslMagentaSaturation", "hslMagentaLuminance"
                    ]
                }
            ]
        },
        {
            "label": "Color Grading",
            "items": [
                { "key": "cgShadowsHue", "label": "Shadows Hue" },
                { "key": "cgShadowsSaturation", "label": "Shadows Saturation" },
                { "key": "cgShadowsLuminance", "label": "Shadows Luminance" },
                { "key": "cgMidtonesHue", "label": "Midtones Hue" },
                { "key": "cgMidtonesSaturation", "label": "Midtones Saturation" },
                { "key": "cgMidtonesLuminance", "label": "Midtones Luminance" },
                { "key": "cgHighlightsHue", "label": "Highlights Hue" },
                { "key": "cgHighlightsSaturation", "label": "Highlights Saturation" },
                { "key": "cgHighlightsLuminance", "label": "Highlights Luminance" },
                { "key": "cgBalance", "label": "Balance" },
                { "key": "cgBlending", "label": "Blending" }
            ]
        },
        {
            "label": "Effects",
            "items": [
                { "key": "grainAmount", "label": "Grain Amount" },
                { "key": "grainSize", "label": "Grain Size" },
                { "key": "grainRoughness", "label": "Grain Roughness" },
                { "key": "vignetteAmount", "label": "Vignette Amount" },
                { "key": "vignetteMidpoint", "label": "Vignette Midpoint" },
                { "key": "vignetteRoundness", "label": "Vignette Roundness" },
                { "key": "vignetteFeather", "label": "Vignette Feather" }
            ]
        },
        {
            "label": "Creative",
            "items": [
                { "key": "clarity", "label": "Clarity" },
                { "key": "dehaze", "label": "Dehaze" },
                { "key": "structure", "label": "Structure" },
                { "key": "centre", "label": "Centre" }
            ]
        },
        {
            "label": "Detail",
            "items": [
                { "key": "sharpness", "label": "Sharpness" },
                { "key": "sharpenMask", "label": "Masking" },
                { "key": "maskFeather", "label": "Feather" },
                { "key": "focusDetect", "label": "Focus" }
            ]
        },
        {
            "label": "Denoise",
            "items": [
                { "key": "denoiseEnabled", "label": "Enable Denoise" },
                { "key": "denoiseAmount", "label": "Denoise Amount" },
                { "key": "denoiseSearchWindow", "label": "Search Window" },
                { "key": "denoiseGroupSize", "label": "Group Size" },
                { "key": "denoiseChromaRadius", "label": "Chroma Radius" },
                { "key": "denoiseChromaAmount", "label": "Chroma Amount" },
                { "key": "denoiseChromaBm3d", "label": "Chroma BM3D Weight" }
            ]
        },
        {
            "label": "Tone Curve",
            "items": [
                {
                    "key": "toneCurve",
                    "label": "Tone Curve",
                    "keys": [
                        "toneCurveLuma",
                        "toneCurveRed",
                        "toneCurveGreen",
                        "toneCurveBlue"
                    ]
                }
            ]
        },
        {
            "label": "Geometry",
            "items": [
                { "key": "cropRect", "label": "Crop Rect" },
                { "key": "cropAspectRatio", "label": "Crop Aspect Ratio" },
                { "key": "straightenAngle", "label": "Straighten Angle" },
                { "key": "orientationSteps", "label": "Orientation Steps" },
                { "key": "flipHorizontal", "label": "Flip Horizontal" },
                { "key": "flipVertical", "label": "Flip Vertical" }
            ]
        }
    ]

    property var sectionDefinitions: defaultSectionDefinitions

    signal selectionAccepted(var filteredSettings, var selectedKeys)

    function itemSettingKeys(item) {
        if (item && item.keys && item.keys.length > 0)
            return item.keys
        if (item && item.key)
            return [item.key]
        return []
    }

    function allSettingKeys() {
        var keys = []
        for (var i = 0; i < sectionDefinitions.length; i++) {
            var section = sectionDefinitions[i]
            for (var j = 0; j < section.items.length; j++) {
                keys.push(section.items[j].key)
            }
        }
        return keys
    }

    function resetSelection(selectAll) {
        var next = {}
        var keys = allSettingKeys()
        for (var i = 0; i < keys.length; i++) {
            next[keys[i]] = selectAll
        }
        selectedState = next
    }

    function openForSettings(settings) {
        sourceSettings = settings || ({})
        resetSelection(true)
        open()
    }

    function isSelected(key) {
        return selectedState[key] === true
    }

    function setSelected(key, checked) {
        var next = Object.assign({}, selectedState)
        next[key] = checked
        selectedState = next
    }

    function sectionSelected(section) {
        for (var i = 0; i < section.items.length; i++) {
            if (!isSelected(section.items[i].key))
                return false
        }
        return section.items.length > 0
    }

    function setSectionSelected(section, checked) {
        var next = Object.assign({}, selectedState)
        for (var i = 0; i < section.items.length; i++) {
            next[section.items[i].key] = checked
        }
        selectedState = next
    }

    function selectedCount() {
        var count = 0
        var keys = allSettingKeys()
        for (var i = 0; i < keys.length; i++) {
            if (isSelected(keys[i]))
                count++
        }
        return count
    }

    function selectedKeys() {
        var keys = []
        var keySet = {}
        var allKeys = allSettingKeys()
        for (var i = 0; i < allKeys.length; i++) {
            var itemKey = allKeys[i]
            if (!isSelected(itemKey))
                continue

            var itemDef = null
            for (var s = 0; s < sectionDefinitions.length && !itemDef; s++) {
                var section = sectionDefinitions[s]
                for (var j = 0; j < section.items.length; j++) {
                    if (section.items[j].key === itemKey) {
                        itemDef = section.items[j]
                        break
                    }
                }
            }

            var mappedKeys = itemSettingKeys(itemDef)
            for (var m = 0; m < mappedKeys.length; m++) {
                var mappedKey = mappedKeys[m]
                if (!keySet[mappedKey]) {
                    keySet[mappedKey] = true
                    keys.push(mappedKey)
                }
            }
        }
        return keys
    }

    function buildFilteredSettings() {
        var result = {}
        var keys = selectedKeys()
        for (var i = 0; i < keys.length; i++) {
            var key = keys[i]
            if (sourceSettings && sourceSettings[key] !== undefined)
                result[key] = sourceSettings[key]
        }
        return result
    }

    function estimateSectionHeight(section) {
        return 26 + (section.items.length * 24)
    }

    function rebalanceColumns() {
        if (!scroll || scroll.height <= 0) {
            sectionColumns = [sectionDefinitions]
            return
        }

        var maxColumnHeight = Math.max(180, scroll.height - 8)
        var cols = []
        var currentCol = []
        var currentHeight = 0

        for (var i = 0; i < sectionDefinitions.length; i++) {
            var section = sectionDefinitions[i]
            var sectionHeight = estimateSectionHeight(section)
            if (currentCol.length > 0 && currentHeight + sectionHeight > maxColumnHeight) {
                cols.push(currentCol)
                currentCol = []
                currentHeight = 0
            }
            currentCol.push(section)
            currentHeight += sectionHeight
        }

        if (currentCol.length > 0)
            cols.push(currentCol)

        sectionColumns = cols
    }

    function sectionColumnWidth() {
        if (!sectionColumns || sectionColumns.length === 0 || !scroll)
            return minSectionColumnWidth
        var visibleFit = Math.floor((scroll.availableWidth - (sectionColumns.length - 1) * 18) / sectionColumns.length)
        return Math.max(minSectionColumnWidth, visibleFit)
    }

    onSectionDefinitionsChanged: Qt.callLater(rebalanceColumns)
    onOpened: Qt.callLater(rebalanceColumns)

    background: Rectangle {
        radius: Theme.radiusLg
        color: Theme.background
        border.color: Theme.border
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: 0

        Item {
            Layout.fillWidth: true
            implicitHeight: headerContent.implicitHeight + 24

            ColumnLayout {
                id: headerContent
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 12
                spacing: 2

                Text {
                    text: root.dialogTitle
                    color: Theme.foreground
                    font: Theme.fontLarge
                }

                Text {
                    text: root.dialogDescription
                    color: Theme.mutedFg
                    font: Theme.fontSmall
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            spacing: 8

            PhotonButton {
                text: "Select All"
                variantOutline: true
                onClicked: root.resetSelection(true)
            }

            PhotonButton {
                text: "Clear All"
                variantOutline: true
                onClicked: root.resetSelection(false)
            }

            Item { Layout.fillWidth: true }

            Text {
                text: root.selectedCount() + " selected"
                color: Theme.mutedFg
                font: Theme.fontSmall
                Layout.alignment: Qt.AlignVCenter
            }
        }

        T.ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.bottomMargin: 10
            clip: true
            onHeightChanged: Qt.callLater(root.rebalanceColumns)
            onWidthChanged: Qt.callLater(root.rebalanceColumns)

            T.ScrollBar.vertical: PhotonScrollBar {}

            Item {
                implicitWidth: columnsRow.implicitWidth
                implicitHeight: columnsRow.implicitHeight
                width: Math.max(scroll.availableWidth, implicitWidth)
                height: implicitHeight

                Row {
                    id: columnsRow
                    spacing: 18

                    Repeater {
                        model: root.sectionColumns

                        delegate: Item {
                            required property var modelData
                            width: root.sectionColumnWidth()
                            implicitHeight: sectionColumn.implicitHeight

                            ColumnLayout {
                                id: sectionColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                spacing: 2

                                Repeater {
                                    model: modelData

                                    delegate: ColumnLayout {
                                        required property var modelData
                                        readonly property var sectionData: modelData
                                        Layout.fillWidth: true
                                        spacing: 2

                                        PhotonCheckbox {
                                            text: sectionData.label
                                            checked: root.sectionSelected(sectionData)
                                            onClicked: root.setSectionSelected(sectionData, checked)
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.leftMargin: 24
                                            spacing: 2

                                            Repeater {
                                                model: sectionData.items

                                                delegate: PhotonCheckbox {
                                                    required property var modelData
                                                    text: modelData.label
                                                    checked: root.isSelected(modelData.key)
                                                    onClicked: root.setSelected(modelData.key, checked)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 8

            Item { Layout.fillWidth: true }

            PhotonButton {
                text: "Cancel"
                variantOutline: true
                onClicked: root.close()
            }

            PhotonButton {
                text: root.confirmButtonText
                enabled: root.selectedCount() > 0
                onClicked: {
                    root.selectionAccepted(root.buildFilteredSettings(), root.selectedKeys())
                    root.close()
                }
            }
        }
    }
}
