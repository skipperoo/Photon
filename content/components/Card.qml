import QtQuick
import QtQuick.Layouts
import Main

Rectangle {
    id: control
    default property alias content: container.data

    implicitWidth: 300
    implicitHeight: container.implicitHeight + 48

    color: Theme.card
    border.color: Theme.border
    border.width: 1
    radius: Theme.radiusLg

    ColumnLayout {
        id: container
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16
    }
}
