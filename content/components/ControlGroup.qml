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
            decimals: (root.title === "Exposure" || root.title === "Contrast") ? 2 : 0
            opacity: 0.8
            onValueEdited: (v) => {
                root.moved(v)
                root.released()
            }
        }
    }

    PhotonSlider {
        id: slider
        Layout.fillWidth: true
        from: root.from
        to: root.to
        value: root.value
        stepSize: root.stepSize
        snapMode: root.snapMode
        defaultValue: root.defaultValue
        trackRamp: root.trackRamp
        onMoved: root.moved(value)
        onReleased: root.released()
        onDoubleClicked: {
            root.moved(root.defaultValue)
            root.released()
        }
        Connections {
            target: root
            function onValueChanged() { slider.value = root.value; }
        }
    }
}
