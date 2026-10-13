import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as T
import Main

Rectangle {
    id: root
    implicitWidth: 320
    color: Theme.background
    border.color: Theme.border
    border.width: 0

    // Left border
    Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.border }

    property var viewport: null
    property real viewTopPadding: 0

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: root.viewTopPadding
        spacing: 0

        T.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 20
            clip: true
            contentWidth: availableWidth
            T.ScrollBar.vertical: PhotonScrollBar {}

            ColumnLayout {
                width: parent.width
                spacing: 20

                Text {
                    text: "LENS CORRECTION"
                    font: Theme.fontSmall
                    color: Theme.mutedFg
                    Layout.fillWidth: true
                }

                PhotonSwitch {
                    id: enableSwitch
                    text: "Enable Lens Corrections"
                    checked: root.viewport ? root.viewport.lensProfileEnabled : false
                    onClicked: {
                        if (root.viewport)
                            root.viewport.lensProfileEnabled = checked
                    }
                }

                Text {
                    Layout.fillWidth: true
                    font: Theme.fontSmall
                    color: Theme.mutedFg
                    text: "Lens Profile"
                }

                PhotonComboBox {
                    id: brandCombo
                    Layout.fillWidth: true
                    model: {
                        var list = root.viewport ? root.viewport.lensProfileMakers : []
                        return ["Automatic"].concat(list)
                    }
                    Binding on currentIndex {
                        value: {
                            if (!root.viewport)
                                return 0
                            const maker = root.viewport.lensProfileEffectiveMaker
                            if (!maker)
                                return 0
                            const idx = root.viewport.lensProfileMakers.indexOf(maker)
                            return idx < 0 ? 0 : idx + 1
                        }
                    }
                    onActivated: (index) => {
                        if (!root.viewport) return
                        root.viewport.lensProfileMaker = index === 0 ? "" : brandCombo.textAt(index)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    PhotonComboBox {
                        id: lensCombo
                        Layout.fillWidth: true
                        model: {
                            var list = root.viewport ? root.viewport.lensProfileLenses : []
                            return ["Automatic"].concat(list)
                        }
                        Binding on currentIndex {
                            value: {
                                if (!root.viewport)
                                    return 0
                                const lens = root.viewport.lensProfileEffectiveLens
                                if (!lens)
                                    return 0
                                const idx = root.viewport.lensProfileLenses.indexOf(lens)
                                return idx < 0 ? 0 : idx + 1
                            }
                        }
                        onActivated: (index) => {
                            if (!root.viewport) return
                            root.viewport.lensProfileLens = index === 0 ? "" : lensCombo.textAt(index)
                        }
                    }

                    PhotonButton {
                        text: "Auto"
                        variantOutline: true
                        implicitWidth: 56
                        onClicked: if (root.viewport) root.viewport.autoDetectLensProfile()
                    }
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font: Theme.fontSmall
                    color: Theme.mutedFg
                    text: {
                        if (!root.viewport)
                            return ""
                        const lens = root.viewport.metadata ? root.viewport.metadata.lensModel : ""
                        if (root.viewport.lensProfileName !== "")
                            return "Matched: " + root.viewport.lensProfileName
                        return lens && lens !== "Unknown Lens"
                               ? "No matching profile for \"" + lens + "\""
                               : "No lens information in this photo."
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#1A1A1C" }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 16

                    ControlGroup {
                        title: "Distortion"
                        value: root.viewport ? root.viewport.lensProfileDistortion : 50.0
                        from: 0; to: 100
                        defaultValue: 50.0
                        onMoved: (v) => { if (root.viewport) root.viewport.lensProfileDistortion = v }
                        onReleased: if (root.viewport) root.viewport.commitEdit()
                    }

                    ControlGroup {
                        title: "Chromatic Aberration"
                        value: root.viewport ? root.viewport.lensProfileCA : 50.0
                        from: 0; to: 100
                        defaultValue: 50.0
                        onMoved: (v) => { if (root.viewport) root.viewport.lensProfileCA = v }
                        onReleased: if (root.viewport) root.viewport.commitEdit()
                    }

                    ControlGroup {
                        title: "Vignetting"
                        value: root.viewport ? root.viewport.lensProfileVignetting : 50.0
                        from: 0; to: 100
                        defaultValue: 50.0
                        onMoved: (v) => { if (root.viewport) root.viewport.lensProfileVignetting = v }
                        onReleased: if (root.viewport) root.viewport.commitEdit()
                    }
                }

                Item { Layout.fillHeight: true }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font: Theme.fontSmall
                    color: Theme.mutedFg
                    opacity: 0.7
                    text: "Lens data © lensfun contributors — CC BY-SA 3.0"
                }
            }
        }
    }
}
