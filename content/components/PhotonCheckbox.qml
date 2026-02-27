import QtQuick
import QtQuick.Controls
import Main

CheckBox {
    id: control
    
    indicator: Rectangle {
        implicitWidth: 20
        implicitHeight: 20
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        radius: 4
        color: control.checked ? Theme.accent : "transparent"
        border.color: control.checked ? Theme.accent : Theme.border
        border.width: 1

        Text {
            width: parent.width
            height: parent.height
            text: "✓"
            font.pixelSize: 14
            color: Theme.primaryFg
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            visible: control.checked
        }
    }

    contentItem: Text {
        text: control.text
        font: Theme.fontRegular
        color: Theme.foreground
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + control.spacing
    }
}
