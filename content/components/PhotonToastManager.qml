import QtQuick

Item {
    id: manager
    anchors.fill: parent
    z: 999

    Column {
        id: toastColumn
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: Theme.spacing2xl
        spacing: Theme.spacingSm
    }

    function show(msg, type = "info", duration = 3000) {
        var component = Qt.createComponent("PhotonToast.qml");
        if (component.status === Component.Ready) {
            component.createObject(toastColumn, {
                "message": msg,
                "type": type,
                "duration": duration
            });
        } else {
            console.error("Error loading Toast component:", component.errorString());
        }
    }
}
