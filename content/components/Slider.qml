import QtQuick
import QtQuick.Controls
import Main

Slider {
    id: control
    from: 0
    to: 100
    value: 50

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: 200
        implicitHeight: 6
        width: control.availableWidth
        height: implicitHeight
        radius: height / 2
        color: Theme.secondary
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            color: Theme.primary
            radius: parent.radius
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: 16
        implicitHeight: 16
        radius: 8
        color: Theme.background
        border.color: Theme.primary
        border.width: 2
        scale: control.pressed ? 1.2 : (control.hovered ? 1.1 : 1.0)
        Behavior on scale { NumberAnimation { duration: 150 } }
        layer.enabled: true
    }
}
