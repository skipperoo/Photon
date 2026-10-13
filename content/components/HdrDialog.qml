import QtQuick
import QtQuick.Controls as T
import QtQuick.Layouts
import Main

T.Dialog {
    id: root
    anchors.centerIn: T.Overlay.overlay
    modal: true
    closePolicy: T.Popup.CloseOnEscape
    padding: 0
    width: Math.min(1000, T.Overlay.overlay ? T.Overlay.overlay.width - 40 : 1000)
    height: Math.min(660, T.Overlay.overlay ? T.Overlay.overlay.height - 60 : 660)

    property int photoCount: 0
    property string previewSource: ""
    property int previewRevision: 0
    property string errorMessage: ""

    signal mergeRequested(bool align, int deghost)

    function refreshPreview() {
        if (root.photoCount < 2) return
        root.errorMessage = ""
        HdrMerge.previewAsync(AppState.selectedImages, alignCheck.checked,
                              deghostCombo.currentIndex, 1024)
    }

    onOpened: refreshPreview()

    background: Rectangle {
        radius: Theme.radiusLg
        color: Theme.background
        border.color: Theme.border
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: 0

        Item {
            Layout.fillWidth: true
            implicitHeight: header.implicitHeight + 24

            ColumnLayout {
                id: header
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 12
                spacing: 2

                Text {
                    text: "HDR Merge"
                    color: Theme.foreground
                    font: Theme.fontLarge
                }

                Text {
                    text: "Merge " + root.photoCount + " photos into one 16-bit linear DNG. Preview the alignment and deghosting before merging."
                    color: Theme.mutedFg
                    font: Theme.fontSmall
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            spacing: 12

            // Preview (2/3)
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: Math.round(root.width * 0.55)
                color: Theme.secondary
                radius: Theme.radius
                clip: true

                Image {
                    anchors.fill: parent
                    anchors.margins: 4
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                    source: root.previewSource
                    visible: root.previewSource !== "" && status === Image.Ready
                }

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 40
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    visible: root.previewSource === "" && !HdrMerge.previewBusy
                    text: "Press \"Show Preview\" to test alignment and deghosting."
                    color: Theme.mutedFg
                    font: Theme.fontSmall
                }

                // Preview spinner
                RowLayout {
                    anchors.centerIn: parent
                    spacing: 8
                    visible: HdrMerge.previewBusy

                    Rectangle {
                        width: 16
                        height: 16
                        color: "transparent"

                        Canvas {
                            anchors.fill: parent
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.reset()
                                ctx.lineWidth = 2
                                ctx.strokeStyle = Theme.foreground
                                ctx.beginPath()
                                ctx.arc(8, 8, 6, 0, Math.PI / 2)
                                ctx.stroke()
                            }
                        }
                        RotationAnimation on rotation {
                            from: 0; to: 360; duration: 1000
                            loops: Animation.Infinite
                            running: HdrMerge.previewBusy
                        }
                    }

                    Text {
                        text: "Rendering preview..."
                        color: Theme.foreground
                        font: Theme.fontSmall
                    }
                }

                // Error / hint
                Text {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 8
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    visible: root.errorMessage !== ""
                    text: root.errorMessage
                    color: Theme.destructive
                    font: Theme.fontSmall
                }
            }

            // Options + buttons (1/3)
            ColumnLayout {
                Layout.preferredWidth: Math.round(root.width / 3) - 16
                Layout.maximumWidth: Math.round(root.width / 3) + 8
                Layout.fillHeight: true
                spacing: 12

                PhotonCheckbox {
                    id: alignCheck
                    text: "Align images"
                    checked: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Text {
                        text: "Deghost"
                        color: Theme.foreground
                        font: Theme.fontRegular
                    }

                    PhotonComboBox {
                        id: deghostCombo
                        Layout.fillWidth: true
                        model: ["Off", "Low", "Medium", "High"]
                        currentIndex: 2
                    }
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font: Theme.fontSmall
                    color: Theme.mutedFg
                    text: "The preview renders at reduced resolution; Merge writes <name>.hdr.dng next to the first selected photo."
                }

                Item { Layout.fillHeight: true }

                PhotonButton {
                    Layout.fillWidth: true
                    text: "Show Preview"
                    variantOutline: true
                    enabled: root.photoCount >= 2 && !HdrMerge.previewBusy
                    onClicked: root.refreshPreview()
                }

                PhotonButton {
                    Layout.fillWidth: true
                    text: "Merge"
                    enabled: root.photoCount >= 2 && !HdrMerge.busy
                    onClicked: {
                        root.mergeRequested(alignCheck.checked, deghostCombo.currentIndex)
                        root.close()
                    }
                }

                PhotonButton {
                    Layout.fillWidth: true
                    text: "Cancel"
                    variantOutline: true
                    onClicked: root.close()
                }
            }
        }
    }

    Connections {
        target: HdrMerge
        function onPreviewReady(result) {
            if (result.success) {
                root.previewRevision++
                root.previewSource = "file://" + result.filename + "?v=" + root.previewRevision
                root.errorMessage = ""
            } else {
                root.errorMessage = result.message
            }
        }
    }
}
