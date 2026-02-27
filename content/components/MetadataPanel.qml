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
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            clip: true
            contentWidth: availableWidth
            T.ScrollBar.vertical: PhotonScrollBar {}

            ColumnLayout {
                width: parent.width
                spacing: 24

                Text {
                    text: "EXIF METADATA"
                    color: Theme.mutedFg
                    font.pixelSize: Theme.fontSmall.pixelSize
                    font.bold: true
                }

                // Metadata Grid (Table-like)
                GridLayout {
                    columns: 2
                    columnSpacing: 20
                    rowSpacing: 12
                    Layout.fillWidth: true

                    property var metadataMap: root.viewport ? root.viewport.metadata : {}

                    // Function to safely get value or placeholder
                    function getValue(key) {
                        return (metadataMap && metadataMap[key] !== undefined) ? metadataMap[key].toString() : "-"
                    }

                    // Metadata Rows
                    Text { text: "Make"; color: Theme.mutedFg; font: Theme.fontSmall }
                    Text { text: parent.getValue("make"); color: Theme.foreground; font: Theme.fontSmall; Layout.fillWidth: true; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight }

                    Text { text: "Model"; color: Theme.mutedFg; font: Theme.fontSmall }
                    Text { text: parent.getValue("model"); color: Theme.foreground; font: Theme.fontSmall; Layout.fillWidth: true; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight }

                    Text { text: "Lens"; color: Theme.mutedFg; font: Theme.fontSmall }
                    Text { text: parent.getValue("lensModel"); color: Theme.foreground; font: Theme.fontSmall; Layout.fillWidth: true; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight }

                    Rectangle { Layout.columnSpan: 2; Layout.fillWidth: true; height: 1; color: "#1A1A1C" }

                    Text { text: "ISO"; color: Theme.mutedFg; font: Theme.fontSmall }
                    Text { text: parent.getValue("iso"); color: Theme.foreground; font: Theme.fontSmall; Layout.fillWidth: true; horizontalAlignment: Text.AlignRight }

                    Text { text: "Shutter"; color: Theme.mutedFg; font: Theme.fontSmall }
                    Text { text: parent.getValue("exposureTime"); color: Theme.foreground; font: Theme.fontSmall; Layout.fillWidth: true; horizontalAlignment: Text.AlignRight }

                    Text { text: "Aperture"; color: Theme.mutedFg; font: Theme.fontSmall }
                    Text { text: parent.getValue("aperture"); color: Theme.foreground; font: Theme.fontSmall; Layout.fillWidth: true; horizontalAlignment: Text.AlignRight }

                    Text { text: "Focal Length"; color: Theme.mutedFg; font: Theme.fontSmall }
                    Text { text: parent.getValue("focalLength"); color: Theme.foreground; font: Theme.fontSmall; Layout.fillWidth: true; horizontalAlignment: Text.AlignRight }

                    Rectangle { Layout.columnSpan: 2; Layout.fillWidth: true; height: 1; color: "#1A1A1C" }

                    Text { text: "Date/Time"; color: Theme.mutedFg; font: Theme.fontSmall }
                    Text { text: parent.getValue("timestamp"); color: Theme.foreground; font: Theme.fontSmall; Layout.fillWidth: true; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight }

                    Text { text: "Artist"; color: Theme.mutedFg; font: Theme.fontSmall }
                    Text { text: parent.getValue("artist"); color: Theme.foreground; font: Theme.fontSmall; Layout.fillWidth: true; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight }
                }

                // Denoising Status Indicator
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    visible: root.viewport ? root.viewport.isDenoising : false
                    
                                            Rectangle {
                                                width: 16; height: 16; color: "transparent"
                                                border.color: Theme.accent
                                                border.width: 2
                                                radius: 8
                                                opacity: 0.3
                                            }
                                            
                                            Rectangle {
                                                width: 16; height: 16; color: "transparent"
                                                border.color: "transparent"
                                                border.width: 2
                                                radius: 8
                                                
                                                // Visual workaround for a "top border only" in QML: Use a smaller rectangle clipped or a canvas
                                                // Simple version: rotate a child that has a visible part
                                                Rectangle {
                                                    width: 16; height: 8; color: Theme.foreground; radius: 2; anchors.top: parent.top
                                                    visible: false // fallback if needed
                                                }
                                                
                                                // Better way: use a Canvas for a proper arc
                                                Canvas {
                                                    anchors.fill: parent
                                                    onPaint: {
                                                        var ctx = getContext("2d");
                                                        ctx.reset();
                                                        ctx.lineWidth = 2;
                                                        ctx.strokeStyle = Theme.foreground;
                                                        ctx.beginPath();
                                                        ctx.arc(8, 8, 7, 0, Math.PI / 2);
                                                        ctx.stroke();
                                                    }
                                                }
                                                
                                                RotationAnimation on rotation {
                                                    from: 0; to: 360; duration: 1000; loops: Animation.Infinite; running: parent.parent.visible
                                                }
                                            }
                    Text {
                        text: "Denoising..."
                        color: Theme.foreground
                        font: Theme.fontSmall
                    }
                }
                
                Item { Layout.fillHeight: true }
            }
        }
    }
}
