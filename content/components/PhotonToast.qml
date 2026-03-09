import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Main

Rectangle {
    id: root
    
    property string message: ""
    property string type: "info" // info, warning, error
    property int duration: 3000

    width: layout.implicitWidth + (Theme.spacingLg * 2)
    height: 50
    color: Theme.card
    radius: Theme.radius
    border.color: Theme.border
    border.width: 1

    // Shadow/Glow effect using a simple drop shadow logic
    layer.enabled: true
    
    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingLg
        anchors.rightMargin: Theme.spacingLg
        spacing: Theme.spacingMd

        // Icon Logic
        Text {
            text: {
                if (root.type === "error") return "󰅙" 
                if (root.type === "warning") return ""
                return "" // Info default
            }
            font.pixelSize: 15
            color: {
                if (root.type === "error") return Theme.destructive
                if (root.type === "warning") return "#f59e0b" // Amber
                return Theme.accent
            }
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignVCenter
        }

        Text {
            text: root.message
            font: Theme.fontMedium
            color: Theme.foreground
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignVCenter
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
    }

    // Entrance and Exit animations
    SequentialAnimation on opacity {
        id: fadeAnim
        running: true
        NumberAnimation { from: 0; to: 1; duration: 200; easing.type: Easing.OutCubic }
        PauseAnimation { duration: root.duration }
        NumberAnimation { from: 1; to: 0; duration: 250; easing.type: Easing.InCubic }
        onStopped: root.destroy()
    }

    NumberAnimation on y {
        from: root.y + 10
        to: root.y
        duration: 200
        easing.type: Easing.OutBack
    }
}
