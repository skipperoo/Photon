import QtQuick 2.15
import QtQuick.Window 2.15
import Main 1.0

Window {
    width: 800
    height: 600
    visible: true
    title: "RawViewport Test"

    Main.RawViewport {
        id: rawViewport
        anchors.fill: parent
    }
}