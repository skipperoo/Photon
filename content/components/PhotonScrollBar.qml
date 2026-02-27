import QtQuick
import QtQuick.Controls
import Main

ScrollBar {
    id: control
    
    padding: 2
    
    contentItem: Rectangle {
        implicitWidth: control.orientation === Qt.Vertical ? 6 : 100
        implicitHeight: control.orientation === Qt.Vertical ? 100 : 6
        radius: (control.orientation === Qt.Vertical ? width : height) / 2
        color: control.pressed ? Theme.mutedFg : (control.hovered ? Theme.mutedFg : Theme.highlight)
        
        // Ensure the scrollbar is visible when active or hovered
        opacity: control.policy === ScrollBar.AlwaysOn || (control.active && control.size < 1.0) ? 0.5 : 0.0
        
        Behavior on opacity {
            NumberAnimation { duration: 200 }
        }
    }
    
    background: Rectangle {
        implicitWidth: control.orientation === Qt.Vertical ? 6 : 100
        implicitHeight: control.orientation === Qt.Vertical ? 100 : 6
        color: "transparent"
    }
}
