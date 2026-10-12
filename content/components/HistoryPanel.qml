import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Main

Control {
    id: root
    property var viewport: null
    property real viewTopPadding: 0

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 40 + root.viewTopPadding

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 10
                text: "History"
                font: Theme.fontMedium
                color: Theme.foreground
            }

            Text {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 12
                text: root.viewport ? "Process v" + root.viewport.processVersion : ""
                font: Theme.fontSmall
                color: Theme.mutedFg
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.border
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 4
            clip: true
            model: root.viewport ? root.viewport.historySteps : []

            // The model is replaced whenever history changes; restore the
            // scroll position so clicking a step doesn't jump to the top.
            property real pendingContentY: -1
            onModelChanged: {
                if (pendingContentY < 0)
                    return
                Qt.callLater(function() {
                    if (list.pendingContentY >= 0) {
                        var maxY = Math.max(0, list.contentHeight - list.height)
                        list.contentY = Math.max(0, Math.min(list.pendingContentY, maxY))
                        list.pendingContentY = -1
                    }
                })
            }

            delegate: Rectangle {
                id: historyRow
                required property var modelData
                width: ListView.view.width
                height: 30
                color: modelData.current ? Theme.secondary : (hoverArea.containsMouse ? Theme.highlight : "transparent")

                Rectangle {
                    width: 3
                    height: parent.height
                    color: Theme.accent
                    visible: modelData.current
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.right: deltaText.left
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    text: modelData.label
                    font: Theme.fontSmall
                    color: modelData.future ? Theme.mutedFg : Theme.foreground
                    opacity: modelData.future ? 0.5 : 1.0
                }

                Text {
                    id: deltaText
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.delta !== undefined ? modelData.delta : ""
                    font: Theme.fontSmall
                    color: Theme.mutedFg
                    opacity: modelData.future ? 0.4 : 0.9
                }

                MouseArea {
                    id: hoverArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if (root.viewport) {
                            list.pendingContentY = list.contentY
                            root.viewport.jumpToHistory(modelData.index)
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.border
        }

        PhotonButton {
            Layout.fillWidth: true
            Layout.margins: 12
            text: "Clear History"
            variantOutline: true
            enabled: root.viewport ? root.viewport.historySteps.length > 2 : false
            onClicked: {
                if (root.viewport)
                    root.viewport.clearHistory()
            }
        }
    }
}
