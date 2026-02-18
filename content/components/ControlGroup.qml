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
    signal moved(real val)
    signal released()

    spacing: 4
    Layout.fillWidth: true

    RowLayout {
        Layout.fillWidth: true
        Text { 
            id: titleText
            text: root.title
            font: Theme.fontRegular
            color: Theme.foreground 
        }
        Item { Layout.fillWidth: true }
        Text { 
            text: root.value.toFixed(root.title === "Exposure" || root.title === "Contrast" ? 2 : 0)
            font: Theme.fontSmall
            color: Theme.foreground 
            opacity: 0.8
        }
    }

    Slider {
        id: slider
        Layout.fillWidth: true
        from: root.from
        to: root.to
        value: root.value
        onMoved: root.moved(value)
        onReleased: root.released()
    }
}
