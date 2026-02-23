import QtQuick
import QtQuick.Controls
import Main

Switch {
    id: control
    property color backgroundColor: Theme.accent
    
    indicator: Rectangle {
        implicitWidth: 44
        implicitHeight: 24
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        radius: height / 2
        color: control.checked ? control.backgroundColor : Theme.secondary
        border.width: 1
        border.color: control.checked ? control.backgroundColor : Theme.border

        Behavior on color { ColorAnimation { duration: 200 } }

        Rectangle {
            id: knob
            x: control.checked ? parent.width - width - 2 : 2
            width: 20
            height: 20
            radius: height / 2
            color: "white"
            anchors.verticalCenter: parent.verticalCenter

            Behavior on x {
                NumberAnimation { duration: 200; easing.type: Easing.OutQuint }
            }
        }
    }

    contentItem: Label {
        color: Theme.foreground
        text: control.text
        font: Theme.fontRegular
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + control.spacing
    }
}
