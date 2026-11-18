import QtQuick
import QtQuick.Layouts
import Main 1.0
import "../components"

Window {
    id: appWindow
    width: 1280
    height: 800
    visible: true
    title: "Photon"
    color: Theme.background

    // The UI runs in one place at a time: here, or in the remote session's
    // offscreen window while a browser is connected.
    Loader {
        anchors.fill: parent
        active: !RemoteSession.active
        sourceComponent: AppContent {}
    }

    Loader {
        anchors.centerIn: parent
        active: RemoteSession.active
        sourceComponent: ColumnLayout {
            spacing: 16
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "Photon is being used remotely"
                color: Theme.foreground
                font: Theme.fontLarge
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: RemoteServer.clientConnected ? "Connected from " + RemoteServer.clientAddress
                                                   : "Waiting for the device to reconnect\u2026"
                color: Theme.mutedFg
                font: Theme.fontMedium
            }
            PhotonButton {
                Layout.alignment: Qt.AlignHCenter
                text: "Use Photon here"
                onClicked: RemoteServer.endSession()
            }
        }
    }
}
