import QtQuick
import QtQuick.Shapes
import Main

Item {
    id: root
    
    property var histogramRed: []
    property var histogramGreen: []
    property var histogramBlue: []
    property var histogramLuma: []

    implicitWidth: 300
    implicitHeight: 150

    Rectangle {
        anchors.fill: parent
        color: Theme.secondary
        radius: Theme.radius
        border.color: Theme.border
        clip: true

        // Grid lines
        Row {
            anchors.fill: parent
            Repeater {
                model: 4
                Rectangle {
                    width: parent.width / 4
                    height: parent.height
                    color: "transparent"
                    border.color: Theme.border
                    opacity: 0.2
                }
            }
        }

        // --- Luma Channel ---
        Shape {
            anchors.fill: parent
            opacity: 0.5
            layer.enabled: true
            layer.samples: 4
            ShapePath {
                fillColor: Theme.foreground
                strokeWidth: 1
                strokeColor: Theme.foreground
                startX: 0; startY: root.height
                PathPolyline {
                    path: {
                        var res = []
                        if (root.histogramLuma.length < 256) return res
                        var step = root.width / 255
                        for (var i = 0; i < 256; i++) {
                            res.push(Qt.point(i * step, root.height - (root.histogramLuma[i] * root.height)))
                        }
                        res.push(Qt.point(root.width, root.height))
                        return res
                    }
                }
            }
        }

        // --- Red Channel ---
        Shape {
            anchors.fill: parent
            opacity: 0.6
            layer.enabled: true
            layer.samples: 4
            ShapePath {
                fillColor: "#ef4444"
                strokeWidth: 1
                strokeColor: "#ef4444"
                startX: 0; startY: root.height
                PathPolyline {
                    path: {
                        var res = []
                        if (root.histogramRed.length < 256) return res
                        var step = root.width / 255
                        for (var i = 0; i < 256; i++) {
                            res.push(Qt.point(i * step, root.height - (root.histogramRed[i] * root.height)))
                        }
                        res.push(Qt.point(root.width, root.height))
                        return res
                    }
                }
            }
        }

        // --- Green Channel ---
        Shape {
            anchors.fill: parent
            opacity: 0.6
            layer.enabled: true
            layer.samples: 4
            ShapePath {
                fillColor: "#22c55e"
                strokeWidth: 1
                strokeColor: "#22c55e"
                startX: 0; startY: root.height
                PathPolyline {
                    path: {
                        var res = []
                        if (root.histogramGreen.length < 256) return res
                        var step = root.width / 255
                        for (var i = 0; i < 256; i++) {
                            res.push(Qt.point(i * step, root.height - (root.histogramGreen[i] * root.height)))
                        }
                        res.push(Qt.point(root.width, root.height))
                        return res
                    }
                }
            }
        }

        // --- Blue Channel ---
        Shape {
            anchors.fill: parent
            opacity: 0.6
            layer.enabled: true
            layer.samples: 4
            ShapePath {
                fillColor: "#3b82f6"
                strokeWidth: 1
                strokeColor: "#3b82f6"
                startX: 0; startY: root.height
                PathPolyline {
                    path: {
                        var res = []
                        if (root.histogramBlue.length < 256) return res
                        var step = root.width / 255
                        for (var i = 0; i < 256; i++) {
                            res.push(Qt.point(i * step, root.height - (root.histogramBlue[i] * root.height)))
                        }
                        res.push(Qt.point(root.width, root.height))
                        return res
                    }
                }
            }
        }
    }
}
