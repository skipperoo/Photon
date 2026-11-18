import QtQuick
import QtQuick.Controls as T
import QtQuick.Layouts
import Main

// Edit video: preview the clip of one photo's edit and choose its format, the
// pill's style / shape / position and which edits appear one by one before
// exporting it.
T.Dialog {
    id: root
    anchors.centerIn: T.Overlay.overlay
    modal: true
    closePolicy: T.Popup.CloseOnEscape
    padding: 0
    width: Math.min(1100, T.Overlay.overlay ? T.Overlay.overlay.width - 40 : 1100)
    height: Math.min(760, T.Overlay.overlay ? T.Overlay.overlay.height - 60 : 760)

    property string photoPath: ""
    property string outputFolder: ""
    property int frame: 0
    property bool playing: true

    readonly property var aspectOptions: [
        { label: "Original", value: "original" },
        { label: "4:5 (portrait)", value: "4:5" },
        { label: "5:4 (landscape)", value: "5:4" },
        { label: "16:9 (landscape)", value: "16:9" },
        { label: "9:16 (stories, reels)", value: "9:16" }
    ]
    readonly property var resolutionOptions: [
        { label: "Full HD (1080p)", value: 1080 },
        { label: "4K (2160p)", value: 2160 }
    ]
    readonly property var styleOptions: [
        { label: "Frosted glass", value: "frosted" },
        { label: "Liquid glass", value: "liquid" },
        { label: "White", value: "white" },
        { label: "Black", value: "black" }
    ]
    readonly property var shapeOptions: [
        { label: "Rounded rectangle", value: "rounded" },
        { label: "Capsule", value: "capsule" }
    ]
    readonly property var positionOptions: [
        { label: "Top", value: "top" },
        { label: "Center", value: "center" },
        { label: "Bottom", value: "bottom" }
    ]

    function indexOfValue(options, value) {
        for (var i = 0; i < options.length; ++i) {
            if (options[i].value === value)
                return i
        }
        return 0
    }

    function timeText(frames) {
        var fps = Math.max(1, EditVideo.fps)
        var seconds = Math.floor(frames / fps)
        var m = Math.floor(seconds / 60)
        var s = seconds % 60
        return m + ":" + (s < 10 ? "0" : "") + s
    }

    onOpened: {
        root.frame = 0
        root.playing = true
        EditVideo.open(root.photoPath)
    }
    onClosed: {
        root.playing = false
        EditVideo.close()
    }

    Connections {
        target: EditVideo
        function onPreviewChanged() {
            if (root.frame >= EditVideo.frameCount)
                root.frame = 0
        }
    }

    Timer {
        interval: Math.round(1000 / Math.max(1, EditVideo.fps))
        repeat: true
        running: root.visible && root.playing && EditVideo.ready && EditVideo.frameCount > 0
        onTriggered: root.frame = (root.frame + 1) % EditVideo.frameCount
    }

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
                    text: "Edit Video"
                    color: Theme.foreground
                    font: Theme.fontLarge
                }

                Text {
                    text: "The edit applied step by step, then a before/after slider. The preview is rendered at reduced resolution; the export uses the full photo."
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

            // Preview and playback
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.secondary
                    radius: Theme.radius
                    clip: true

                    EditVideoPreview {
                        anchors.fill: parent
                        anchors.margins: 6
                        controller: EditVideo
                        frame: root.frame
                        visible: EditVideo.frameCount > 0
                    }

                    // Busy overlay
                    Rectangle {
                        anchors.centerIn: parent
                        visible: EditVideo.loading
                        width: busyRow.implicitWidth + 24
                        height: busyRow.implicitHeight + 16
                        radius: Theme.radius
                        color: Theme.background
                        opacity: 0.92

                        RowLayout {
                            id: busyRow
                            anchors.centerIn: parent
                            spacing: 8

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
                                    running: EditVideo.loading
                                }
                            }

                            Text {
                                text: EditVideo.status
                                color: Theme.foreground
                                font: Theme.fontSmall
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: !EditVideo.loading && EditVideo.frameCount === 0 && EditVideo.status !== ""
                        text: EditVideo.status
                        color: Theme.destructive
                        font: Theme.fontSmall
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    PhotonButton {
                        text: root.playing ? "Pause" : "Play"
                        variantOutline: true
                        enabled: EditVideo.frameCount > 0
                        Layout.preferredWidth: 72
                        onClicked: root.playing = !root.playing
                    }

                    PhotonSlider {
                        id: scrubber
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(1, EditVideo.frameCount - 1)
                        stepSize: 1
                        enabled: EditVideo.frameCount > 0
                        value: root.frame
                        onMoved: {
                            root.playing = false
                            root.frame = Math.round(value)
                        }
                    }

                    Text {
                        text: root.timeText(root.frame) + " / " + root.timeText(EditVideo.frameCount)
                        color: Theme.mutedFg
                        font: Theme.fontSmall
                        Layout.preferredWidth: 72
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }

            // Options
            ColumnLayout {
                Layout.preferredWidth: 300
                Layout.maximumWidth: 300
                Layout.fillHeight: true
                spacing: 12

                T.ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true

                    ColumnLayout {
                        width: parent.width
                        spacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            Text { text: "Aspect ratio"; color: Theme.mutedFg; font: Theme.fontSmall }
                            PhotonComboBox {
                                Layout.fillWidth: true
                                model: root.aspectOptions
                                textRole: "label"
                                valueRole: "value"
                                currentIndex: root.indexOfValue(root.aspectOptions, EditVideo.aspect)
                                onActivated: (index) => EditVideo.aspect = valueAt(index)
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            Text { text: "Resolution"; color: Theme.mutedFg; font: Theme.fontSmall }
                            PhotonComboBox {
                                Layout.fillWidth: true
                                model: root.resolutionOptions
                                textRole: "label"
                                valueRole: "value"
                                currentIndex: root.indexOfValue(root.resolutionOptions, EditVideo.resolution)
                                onActivated: (index) => EditVideo.resolution = valueAt(index)
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            Text { text: "Label style"; color: Theme.mutedFg; font: Theme.fontSmall }
                            PhotonComboBox {
                                Layout.fillWidth: true
                                model: root.styleOptions
                                textRole: "label"
                                valueRole: "value"
                                currentIndex: root.indexOfValue(root.styleOptions, EditVideo.pillStyle)
                                onActivated: (index) => EditVideo.pillStyle = valueAt(index)
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            Text { text: "Label shape"; color: Theme.mutedFg; font: Theme.fontSmall }
                            PhotonComboBox {
                                Layout.fillWidth: true
                                model: root.shapeOptions
                                textRole: "label"
                                valueRole: "value"
                                currentIndex: root.indexOfValue(root.shapeOptions, EditVideo.pillShape)
                                onActivated: (index) => EditVideo.pillShape = valueAt(index)
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            Text { text: "Label position"; color: Theme.mutedFg; font: Theme.fontSmall }
                            PhotonComboBox {
                                Layout.fillWidth: true
                                model: root.positionOptions
                                textRole: "label"
                                valueRole: "value"
                                currentIndex: root.indexOfValue(root.positionOptions, EditVideo.pillPosition)
                                onActivated: (index) => EditVideo.pillPosition = valueAt(index)
                            }
                        }

                        PhotonCheckbox {
                            text: "Show slider line"
                            checked: EditVideo.showLine
                            onToggled: EditVideo.showLine = checked
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text { text: "Edits shown one by one"; color: Theme.mutedFg; font: Theme.fontSmall }

                            Repeater {
                                model: EditVideo.groups
                                delegate: PhotonCheckbox {
                                    required property var modelData
                                    text: modelData.name
                                    checked: modelData.shown
                                    onToggled: EditVideo.setGroupShown(modelData.group, checked)
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                font: Theme.fontSmall
                                color: Theme.mutedFg
                                text: EditVideo.groups.length === 0 && !EditVideo.loading
                                      ? "This photo has no edits to show."
                                      : "Unchecked edits are applied together as “Tweaks” at the end, before the before/after slider."
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Item { Layout.fillWidth: true }

                    PhotonButton {
                        text: "Cancel"
                        variantOutline: true
                        onClicked: root.close()
                    }

                    PhotonButton {
                        text: "Export"
                        enabled: EditVideo.ready && !ExportManager.isExporting
                        onClicked: {
                            EditVideo.startExport(root.outputFolder)
                            root.close()
                        }
                    }
                }
            }
        }
    }
}
