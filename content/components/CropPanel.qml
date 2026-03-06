import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Basic as T
import Main
import "../components"

Control {
    id: root

    property var viewport: null
    property real viewTopPadding: 0
    property bool straightenToolActive: false

    signal cropConfirmed()
    signal cropDiscarded()

    // Saved state when entering crop mode (for ESC revert)
    property rect _savedCropRect: Qt.rect(0, 0, 1, 1)
    property real _savedAspectRatio: -1
    property real _savedStraightenAngle: 0
    property int  _savedOrientationSteps: 0
    property bool _savedFlipH: false
    property bool _savedFlipV: false

    function saveEntryState() {
        if (!root.viewport) return;
        _savedCropRect = root.viewport.cropRect;
        _savedAspectRatio = root.viewport.cropAspectRatio;
        _savedStraightenAngle = root.viewport.straightenAngle;
        _savedOrientationSteps = root.viewport.orientationSteps;
        _savedFlipH = root.viewport.flipHorizontal;
        _savedFlipV = root.viewport.flipVertical;
    }

    readonly property var aspectPresets: [
        { name: "Free",     value: -1,       tooltip: "Freeform crop" },
        { name: "Original", value: 0,        tooltip: "Original aspect ratio" },
        { name: "1:1",      value: 1.0,      tooltip: "Square" },
        { name: "5:4",      value: 5/4,      tooltip: "5:4 — 8×10 prints" },
        { name: "4:3",      value: 4/3,      tooltip: "4:3 — Tablets" },
        { name: "3:2",      value: 3/2,      tooltip: "3:2 — 35mm film" },
        { name: "16:9",     value: 16/9,     tooltip: "16:9 — Widescreen" },
        { name: "21:9",     value: 21/9,     tooltip: "21:9 — Ultrawide" },
        { name: "65:24",    value: 65/24,    tooltip: "65:24 — Panoramic" }
    ]

    function isPresetActive(presetValue) {
        if (!root.viewport) return false;
        var current = root.viewport.cropAspectRatio;
        if (presetValue < 0 && current < 0) return true;
        if (presetValue === 0 && current === 0) return true;
        if (presetValue > 0 && current > 0)
            return Math.abs(current - presetValue) < 0.01 ||
                   Math.abs(current - 1/presetValue) < 0.01;
        return false;
    }

    function selectPreset(presetValue) {
        if (!root.viewport) return;
        // If clicking same preset, toggle orientation
        if (isPresetActive(presetValue) && presetValue > 0 && presetValue !== 1) {
            var cur = root.viewport.cropAspectRatio;
            root.viewport.cropAspectRatio = 1 / cur;
        } else {
            root.viewport.cropAspectRatio = presetValue;
        }
        // Compute a centered crop rect that respects the new aspect ratio
        root.viewport.cropRect = computeCropForRatio(root.viewport.cropAspectRatio);
    }

    function computeCropForRatio(ratio) {
        if (ratio <= 0) return Qt.rect(0, 0, 1, 1);
        // ratio = desired (pixelW / pixelH)
        // In normalized coords: cw/ch * (srcW/srcH) = ratio
        // So cw/ch = ratio * srcH / srcW = ratio / srcAspect
        var srcW = root.viewport ? root.viewport.sourceWidth : 1;
        var srcH = root.viewport ? root.viewport.sourceHeight : 1;
        // Account for orientation steps (90° rotation swaps W/H)
        var steps = root.viewport ? (root.viewport.orientationSteps % 4) : 0;
        if (steps === 1 || steps === 3) {
            var tmp = srcW; srcW = srcH; srcH = tmp;
        }
        var srcAspect = srcW / srcH;

        // When straightened in crop mode, cropRect is normalized against the
        // rotated bounding box aspect.
        var straighten = root.viewport ? Math.abs(root.viewport.straightenAngle || 0) : 0;
        if (straighten > 0.01 && !(root.viewport && root.viewport.geometryBaked)) {
            var rad = straighten * Math.PI / 180;
            var cosT = Math.cos(rad);
            var sinT = Math.sin(rad);
            var boxW = srcW * cosT + srcH * sinT;
            var boxH = srcW * sinT + srcH * cosT;
            if (boxW > 0 && boxH > 0) {
                srcAspect = boxW / boxH;
            }
        }

        var normRatio = ratio / srcAspect; // cw/ch in normalized space
        var cw, ch;
        if (normRatio >= 1) {
            cw = 1; ch = 1 / normRatio;
        } else {
            cw = normRatio; ch = 1;
        }
        return Qt.rect((1 - cw) / 2, (1 - ch) / 2, cw, ch);
    }

    function resetCropGeometry() {
        if (!root.viewport) return;
        root.viewport.cropRect = Qt.rect(0, 0, 1, 1);
        root.viewport.cropAspectRatio = -1;
        root.viewport.straightenAngle = 0;
        root.viewport.orientationSteps = 0;
        root.viewport.flipHorizontal = false;
        root.viewport.flipVertical = false;
        root.viewport.commitEdit();
    }

    function discardCrop() {
        if (!root.viewport) return;
        root.viewport.cropRect = _savedCropRect;
        root.viewport.cropAspectRatio = _savedAspectRatio;
        root.viewport.straightenAngle = _savedStraightenAngle;
        root.viewport.orientationSteps = _savedOrientationSteps;
        root.viewport.flipHorizontal = _savedFlipH;
        root.viewport.flipVertical = _savedFlipV;
        root.cropDiscarded();
    }

    function applyCrop() {
        if (!root.viewport) return;
        root.viewport.commitEdit();
        root.cropConfirmed();
    }

    // Recompute crop rect when orientation changes so it fits the new effective dimensions
    Connections {
        target: root.viewport
        function onOrientationStepsChanged() {
            if (!root.viewport) return;
            var ratio = root.viewport.cropAspectRatio;
            if (ratio > 0) {
                root.viewport.cropRect = root.computeCropForRatio(ratio);
            }
        }
    }

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

        // Scrollable content
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: contentCol.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: PhotonScrollBar {}

            ColumnLayout {
                id: contentCol
                width: parent.width
                spacing: 0

                // Header
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.topMargin: 16
                    Layout.bottomMargin: 8
                    spacing: 8

                    Text {
                        text: "Crop & Geometry"
                        font: Theme.fontLarge
                        color: Theme.foreground
                    }
                    PhotonButton {
                        text: "Reset All"
                        variantOutline: true
                        onClicked: root.resetCropGeometry()
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                    }
                }

                // === Aspect Ratio Section ===
                Collapsible {
                    Layout.fillWidth: true
                    title: "Aspect Ratio"
                    expanded: true

                    ColumnLayout {
                        width: parent.width
                        spacing: 8

                        // Preset grid (3 columns)
                        Grid {
                            columns: 3
                            spacing: 4
                            Layout.fillWidth: true
                            Layout.leftMargin: 0
                            Layout.rightMargin: 0

                            Repeater {
                                model: root.aspectPresets
                                anchors.centerIn: parent
                                PhotonButton {
                                    text: modelData.name
                                    fontSize: Theme.fontSmall
                                    width: (contentCol.width - 70) / 3
                                    height: 32
                                    font.pixelSize: 11
                                    variantOutline: !root.isPresetActive(modelData.value)
                                    onClicked: root.selectPreset(modelData.value)
                                    T.ToolTip.visible: hovered
                                    T.ToolTip.delay: 500
                                    T.ToolTip.text: modelData.tooltip
                                }
                            }
                        }
                    }
                }

                // === Straighten Section ===
                Collapsible {
                    Layout.fillWidth: true
                    title: "Straighten"
                    expanded: true

                    ColumnLayout {
                        width: parent.width
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: root.viewport ? root.viewport.straightenAngle.toFixed(1) + "°" : "0.0°"
                                font: Theme.fontRegular
                                color: Theme.foreground
                            }
                            Item { Layout.fillWidth: true }

                            // Straighten tool button
                            PhotonButton {
                                icon.source: "qrc:/Main/assets/icons/ruler.svg"
                                icon.color: root.straightenToolActive ? Theme.accent : Theme.foreground
                                icon.width: 16; icon.height: 16
                                variantOutline: !root.straightenToolActive
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                                onClicked: root.straightenToolActive = !root.straightenToolActive
                                T.ToolTip.visible: hovered
                                T.ToolTip.delay: 500
                                T.ToolTip.text: "Straighten Tool — draw a reference line"
                            }

                            // Reset straighten
                            PhotonButton {
                                icon.source: "qrc:/Main/assets/icons/rotate-ccw.svg"
                                icon.color: Theme.foreground
                                icon.width: 14; icon.height: 14
                                variantOutline: true
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                                enabled: root.viewport && root.viewport.straightenAngle !== 0
                                opacity: enabled ? 1.0 : 0.4
                                onClicked: {
                                    if (root.viewport) {
                                        root.viewport.straightenAngle = 0;
                                        root.viewport.commitEdit();
                                    }
                                }
                                T.ToolTip.visible: hovered
                                T.ToolTip.delay: 500
                                T.ToolTip.text: "Reset straighten"
                            }
                        }

                        PhotonSlider {
                            id: straightenSlider
                            Layout.fillWidth: true
                            from: -45
                            to: 45
                            value: root.viewport ? root.viewport.straightenAngle : 0
                            stepSize: 0.1
                            defaultValue: 0
                            onMoved: {
                                if (root.viewport) root.viewport.straightenAngle = value;
                            }
                            onReleased: {
                                if (root.viewport) root.viewport.commitEdit();
                            }
                            onDoubleClicked: {
                                if (root.viewport) {
                                    root.viewport.straightenAngle = 0;
                                    root.viewport.commitEdit();
                                }
                            }
                            Connections {
                                target: root.viewport
                                function onStraightenAngleChanged() {
                                    straightenSlider.value = root.viewport.straightenAngle;
                                }
                            }
                        }
                    }
                }

                // === Orientation Section ===
                Collapsible {
                    Layout.fillWidth: true
                    title: "Orientation"
                    expanded: true

                    ColumnLayout {
                        width: parent.width
                        spacing: 8

                        // Rotate + flip in a single row of 4 icon buttons
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            PhotonButton {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 36
                                icon.source: "qrc:/Main/assets/icons/rotate-ccw.svg"
                                icon.color: Theme.foreground
                                icon.width: 18; icon.height: 18
                                display: AbstractButton.IconOnly
                                variantOutline: true
                                onClicked: {
                                    if (root.viewport) {
                                        root.viewport.orientationSteps = (root.viewport.orientationSteps + 3) % 4;
                                        root.viewport.commitEdit();
                                    }
                                }
                                T.ToolTip.visible: hovered
                                T.ToolTip.delay: 500
                                T.ToolTip.text: "Rotate 90° counter-clockwise"
                            }

                            PhotonButton {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 36
                                icon.source: "qrc:/Main/assets/icons/rotate-cw.svg"
                                icon.color: Theme.foreground
                                icon.width: 18; icon.height: 18
                                display: AbstractButton.IconOnly
                                variantOutline: true
                                onClicked: {
                                    if (root.viewport) {
                                        root.viewport.orientationSteps = (root.viewport.orientationSteps + 1) % 4;
                                        root.viewport.commitEdit();
                                    }
                                }
                                T.ToolTip.visible: hovered
                                T.ToolTip.delay: 500
                                T.ToolTip.text: "Rotate 90° clockwise"
                            }

                            PhotonButton {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 36
                                icon.source: "qrc:/Main/assets/icons/flip-horizontal.svg"
                                icon.color: root.viewport && root.viewport.flipHorizontal ? Theme.accent : Theme.foreground
                                icon.width: 18; icon.height: 18
                                display: AbstractButton.IconOnly
                                variantOutline: !(root.viewport && root.viewport.flipHorizontal)
                                onClicked: {
                                    if (root.viewport) {
                                        root.viewport.flipHorizontal = !root.viewport.flipHorizontal;
                                        root.viewport.commitEdit();
                                    }
                                }
                                T.ToolTip.visible: hovered
                                T.ToolTip.delay: 500
                                T.ToolTip.text: "Flip horizontally"
                            }

                            PhotonButton {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 36
                                icon.source: "qrc:/Main/assets/icons/flip-vertical.svg"
                                icon.color: root.viewport && root.viewport.flipVertical ? Theme.accent : Theme.foreground
                                icon.width: 18; icon.height: 18
                                display: AbstractButton.IconOnly
                                variantOutline: !(root.viewport && root.viewport.flipVertical)
                                onClicked: {
                                    if (root.viewport) {
                                        root.viewport.flipVertical = !root.viewport.flipVertical;
                                        root.viewport.commitEdit();
                                    }
                                }
                                T.ToolTip.visible: hovered
                                T.ToolTip.delay: 500
                                T.ToolTip.text: "Flip vertically"
                            }
                        }
                    }
                }

                // Bottom spacer
                Item { Layout.fillHeight: true }
            }
        }
    }
}
