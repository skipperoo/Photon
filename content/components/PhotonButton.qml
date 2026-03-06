import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Main

Button {
    id: control
    text: "Button"

    property bool variantOutline: false
    property bool variantDestructive: false
    property font fontType: Theme.fontMedium

    contentItem: RowLayout {
        spacing: 8
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        Image {
            id: iconItem
            source: control.icon.source
            Layout.preferredWidth: control.icon.width > 0 ? control.icon.width : 16
            Layout.preferredHeight: control.icon.height > 0 ? control.icon.height : 16
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            Layout.fillWidth: control.display === AbstractButton.IconOnly
            visible: control.icon.source.toString() !== ""
            fillMode: Image.PreserveAspectFit
            
            layer.enabled: true
            layer.effect: ShaderEffect {
                property color color: control.icon.color
                fragmentShader: "qrc:/Main/shaders/ColorMask.frag.qsb"
            }
        }
        Text {
            text: control.text
            font: fontType
            color: {
                if (control.variantDestructive) return "#ffffff"
                if (control.variantOutline) return Theme.foreground
                return Theme.primaryFg
            }
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            visible: control.display !== AbstractButton.IconOnly && control.text !== ""
            Layout.fillWidth: true
        }
    }

    background: Rectangle {
        implicitWidth: control.display === AbstractButton.IconOnly ? 40 : 100
        implicitHeight: 40
        radius: Theme.radius
        border.width: control.variantOutline ? 1 : 0
        border.color: Theme.border
        color: {
            if (!control.enabled) return Theme.mutedFg
            if (control.variantDestructive) {
                return control.down ? Qt.darker(Theme.destructive, 1.2) :
                       control.hovered ? Qt.lighter(Theme.destructive, 1.2) : Theme.destructive
            }
            if (control.variantOutline) {
                return control.down ? Theme.accent :
                       control.hovered ? Theme.accent : "transparent"
            }
            return control.down ? Qt.darker(Theme.primary, 1.1) :
                   control.hovered ? "#e4e4e7" : Theme.primary
        }
        Behavior on color { ColorAnimation { duration: 150 } }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onPressed: (mouse) => mouse.accepted = false
    }
}
