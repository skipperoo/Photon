import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Main

ColumnLayout {
    id: root
    property string title: ""
    property real value: 0
    property real from: 0
    property real to: 100
    property real defaultValue: 0.0
    property real stepSize: 0
    property int snapMode: 0
    property var trackRamp: null
    // Optional non-linear slider scale: toSlider(value) -> position 0..1 and
    // fromSlider(position) -> value. The label and typed values stay in value
    // units (from/to/stepSize); only the slider track uses the mapping.
    property var toSlider: null
    property var fromSlider: null
    readonly property bool mapped: toSlider !== null && fromSlider !== null
    signal moved(real val)
    signal released()

    spacing: 4
    Layout.fillWidth: true

    RowLayout {
        Layout.fillWidth: true
        
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onDoubleTapped: {
                root.moved(root.defaultValue)
                root.released()
            }
        }

        Text { 
            id: titleText
            text: root.title
            font: Theme.fontRegular
            color: Theme.foreground 
        }
        Item { Layout.fillWidth: true }
        EditableValueLabel {
            value: root.value
            from: root.from
            to: root.to
            stepSize: root.stepSize
            decimals: root.title === "Exposure" ? 2 : 0
            opacity: 0.8
            onValueEdited: (v) => {
                root.moved(v)
                root.released()
            }
        }
    }

    function sliderToValue(p) {
        var v = root.fromSlider(p)
        if (root.stepSize > 0) v = Math.round(v / root.stepSize) * root.stepSize
        return Math.max(root.from, Math.min(root.to, v))
    }

    PhotonSlider {
        id: slider
        Layout.fillWidth: true
        from: root.mapped ? 0 : root.from
        to: root.mapped ? 1 : root.to
        value: root.mapped ? root.toSlider(root.value) : root.value
        stepSize: root.mapped ? 0 : root.stepSize
        snapMode: root.snapMode
        defaultValue: root.mapped ? root.toSlider(root.defaultValue) : root.defaultValue
        trackRamp: root.trackRamp
        onMoved: root.moved(root.mapped ? root.sliderToValue(value) : value)
        onReleased: root.released()
        onDoubleClicked: {
            root.moved(root.defaultValue)
            root.released()
        }
        Connections {
            target: root
            function onValueChanged() {
                slider.value = root.mapped ? root.toSlider(root.value) : root.value;
            }
        }
    }
}
