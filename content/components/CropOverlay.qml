import QtQuick
import Main

Item {
    id: root

    property var viewport: null
    property rect imageRect: viewport ? viewport.imageRect : Qt.rect(0, 0, 0, 0)
    property bool active: false
    property bool straightenToolActive: false
    // When not in active crop mode, show a preview mask if crop is non-default
    readonly property bool hasCrop: {
        var cr = cropRect;
        return cr.x > 0.001 || cr.y > 0.001 || cr.width < 0.999 || cr.height < 0.999;
    }
    readonly property bool showPreview: !active && hasCrop && imageRect.width > 0

    signal straightenFinished()

    visible: (active || showPreview) && imageRect.width > 0
    z: active ? 10 : (showPreview ? 5 : -1)

    // Display rect: the usable image area on screen after QML transforms.
    // For 90° steps + flip: exact rotated/mirrored position.
    // For straighten: shrinks by the auto-crop factor (inscribed rectangle),
    // matching the bake pipeline's cos(θ)+sin(θ) formula.
    readonly property rect displayRect: {
        var ir = imageRect;
        if (!viewport || viewport.geometryBaked) return ir;

        var steps = viewport.orientationSteps % 4;
        var fH = viewport.flipHorizontal || false;
        var fV = viewport.flipVertical || false;
        var straighten = viewport.straightenAngle || 0;

        if (steps === 0 && !fH && !fV && Math.abs(straighten) < 0.01) return ir;

        var cx = root.width / 2;
        var cy = root.height / 2;

        // Step 1: Apply 90° rotation steps to imageRect
        var rx = ir.x, ry = ir.y, rw = ir.width, rh = ir.height;
        if (steps === 1) { // 90° CW
            rx = cx + cy - ir.y - ir.height;
            ry = ir.x - cx + cy;
            rw = ir.height; rh = ir.width;
        } else if (steps === 2) { // 180°
            rx = 2*cx - ir.x - ir.width;
            ry = 2*cy - ir.y - ir.height;
        } else if (steps === 3) { // 270° CW
            rx = cx + ir.y - cy;
            ry = cx + cy - ir.x - ir.width;
            rw = ir.height; rh = ir.width;
        }

        // Step 2: Apply flip around viewport center
        if (fH) rx = 2 * cx - rx - rw;
        if (fV) ry = 2 * cy - ry - rh;

        // Step 3: Shrink for straighten auto-crop (inscribed rectangle)
        if (Math.abs(straighten) > 0.01) {
            var rad = Math.abs(straighten) * Math.PI / 180;
            var factor = Math.cos(rad) + Math.sin(rad);
            var newW = rw / factor;
            var newH = rh / factor;
            rx += (rw - newW) / 2;
            ry += (rh - newH) / 2;
            rw = newW;
            rh = newH;
        }

        return Qt.rect(rx, ry, rw, rh);
    }

    // Map normalized crop (0–1) to pixel coords within displayRect
    readonly property real imgX: displayRect.x
    readonly property real imgY: displayRect.y
    readonly property real imgW: displayRect.width
    readonly property real imgH: displayRect.height

    readonly property rect cropRect: viewport ? viewport.cropRect : Qt.rect(0, 0, 1, 1)
    readonly property real cropX: imgX + cropRect.x * imgW
    readonly property real cropY: imgY + cropRect.y * imgH
    readonly property real cropW: cropRect.width * imgW
    readonly property real cropH: cropRect.height * imgH

    readonly property real handleSize: 10
    readonly property real minCropPx: 30

    readonly property color maskColor: root.active ? "#80000000" : Theme.background

    // Dark mask (4 rectangles around crop — covers entire viewport)
    Rectangle { // Top
        x: 0; y: 0
        width: root.width; height: root.cropY
        color: root.maskColor
    }
    Rectangle { // Bottom
        x: 0; y: root.cropY + root.cropH
        width: root.width; height: root.height - (root.cropY + root.cropH)
        color: root.maskColor
    }
    Rectangle { // Left
        x: 0; y: root.cropY
        width: root.cropX; height: root.cropH
        color: root.maskColor
    }
    Rectangle { // Right
        x: root.cropX + root.cropW; y: root.cropY
        width: root.width - (root.cropX + root.cropW); height: root.cropH
        color: root.maskColor
    }

    // Crop border
    Rectangle {
        x: root.cropX; y: root.cropY
        width: root.cropW; height: root.cropH
        color: "transparent"
        border.color: "white"
        border.width: 1.5
        visible: root.active
    }

    // Rule of Thirds lines
    Repeater {
        model: 2
        Rectangle {
            x: root.cropX + (index + 1) * root.cropW / 3
            y: root.cropY
            width: 1; height: root.cropH
            color: "#60ffffff"
            visible: root.active && root.cropW > 60
        }
    }
    Repeater {
        model: 2
        Rectangle {
            x: root.cropX
            y: root.cropY + (index + 1) * root.cropH / 3
            width: root.cropW; height: 1
            color: "#60ffffff"
            visible: root.active && root.cropH > 60
        }
    }

    // Drag handles (only in active edit mode)
    // IDs: 0=TL, 1=T, 2=TR, 3=L, 4=R, 5=BL, 6=B, 7=BR
    Repeater {
        id: handleRepeater
        model: root.active ? 8 : 0

        Rectangle {
            id: handle
            readonly property int hid: index
            readonly property bool isCorner: hid === 0 || hid === 2 || hid === 5 || hid === 7

            x: {
                switch(hid) {
                    case 0: case 3: case 5: return root.cropX - root.handleSize/2;
                    case 1: case 6:         return root.cropX + root.cropW/2 - root.handleSize/2;
                    case 2: case 4: case 7: return root.cropX + root.cropW - root.handleSize/2;
                }
            }
            y: {
                switch(hid) {
                    case 0: case 1: case 2: return root.cropY - root.handleSize/2;
                    case 3: case 4:         return root.cropY + root.cropH/2 - root.handleSize/2;
                    case 5: case 6: case 7: return root.cropY + root.cropH - root.handleSize/2;
                }
            }
            width: root.handleSize; height: root.handleSize
            radius: isCorner ? 0 : root.handleSize / 2
            color: "white"
            border.color: "#333"
            border.width: 1

            MouseArea {
                anchors.fill: parent
                anchors.margins: -6
                cursorShape: {
                    switch(handle.hid) {
                        case 0: case 7: return Qt.SizeFDiagCursor;
                        case 2: case 5: return Qt.SizeBDiagCursor;
                        case 1: case 6: return Qt.SizeVerCursor;
                        case 3: case 4: return Qt.SizeHorCursor;
                    }
                }
                property point dragStart
                property rect cropStart

                onPressed: (mouse) => {
                    dragStart = mapToItem(root, mouse.x, mouse.y);
                    cropStart = root.viewport.cropRect;
                }
                onPositionChanged: (mouse) => {
                    if (!pressed) return;
                    var pos = mapToItem(root, mouse.x, mouse.y);
                    var dx = (pos.x - dragStart.x) / root.imgW;
                    var dy = (pos.y - dragStart.y) / root.imgH;
                    var cr = cropStart;
                    var ratio = root.viewport.cropAspectRatio;
                    var lockRatio = ratio > 0;

                    var nx = cr.x, ny = cr.y, nw = cr.width, nh = cr.height;

                    // Apply delta based on handle
                    switch(handle.hid) {
                        case 0: nx = cr.x + dx; ny = cr.y + dy; nw = cr.width - dx; nh = cr.height - dy; break;
                        case 1: ny = cr.y + dy; nh = cr.height - dy; break;
                        case 2: ny = cr.y + dy; nw = cr.width + dx; nh = cr.height - dy; break;
                        case 3: nx = cr.x + dx; nw = cr.width - dx; break;
                        case 4: nw = cr.width + dx; break;
                        case 5: nx = cr.x + dx; nw = cr.width - dx; nh = cr.height + dy; break;
                        case 6: nh = cr.height + dy; break;
                        case 7: nw = cr.width + dx; nh = cr.height + dy; break;
                    }

                    // Enforce minimum size
                    var minN = root.minCropPx / root.imgW;
                    var minNH = root.minCropPx / root.imgH;
                    if (nw < minN) { nw = minN; if (handle.hid === 0 || handle.hid === 3 || handle.hid === 5) nx = cr.x + cr.width - nw; }
                    if (nh < minNH) { nh = minNH; if (handle.hid === 0 || handle.hid === 1 || handle.hid === 2) ny = cr.y + cr.height - nh; }

                    // Lock aspect ratio
                    if (lockRatio) {
                        var effectiveRatio = ratio;
                        // Aspect ratio is W/H in image space
                        var imgAspect = root.imgW / root.imgH;
                        var normRatio = effectiveRatio / imgAspect;

                        if (handle.hid === 1 || handle.hid === 6) {
                            nw = nh * normRatio;
                            nx = cr.x + (cr.width - nw) / 2;
                        } else if (handle.hid === 3 || handle.hid === 4) {
                            nh = nw / normRatio;
                            ny = cr.y + (cr.height - nh) / 2;
                        } else {
                            // Corner: use the larger delta
                            var newNh = nw / normRatio;
                            if (handle.hid === 0 || handle.hid === 2) {
                                ny = cr.y + cr.height - newNh;
                            }
                            nh = newNh;
                        }
                    }

                    // Clamp to image bounds
                    nx = Math.max(0, Math.min(nx, 1 - nw));
                    ny = Math.max(0, Math.min(ny, 1 - nh));
                    nw = Math.min(nw, 1 - nx);
                    nh = Math.min(nh, 1 - ny);

                    root.viewport.cropRect = Qt.rect(nx, ny, nw, nh);
                }
                onReleased: {
                }
            }
        }
    }

    // Center drag (move crop) — only in active mode
    MouseArea {
        x: root.cropX + root.handleSize
        y: root.cropY + root.handleSize
        width: root.cropW - root.handleSize * 2
        height: root.cropH - root.handleSize * 2
        cursorShape: Qt.SizeAllCursor
        visible: root.active
        enabled: root.active

        property point dragStart
        property rect cropStart

        onPressed: (mouse) => {
            dragStart = mapToItem(root, mouse.x, mouse.y);
            cropStart = root.viewport.cropRect;
        }
        onPositionChanged: (mouse) => {
            if (!pressed) return;
            var pos = mapToItem(root, mouse.x, mouse.y);
            var dx = (pos.x - dragStart.x) / root.imgW;
            var dy = (pos.y - dragStart.y) / root.imgH;
            var cr = cropStart;
            var nx = Math.max(0, Math.min(cr.x + dx, 1 - cr.width));
            var ny = Math.max(0, Math.min(cr.y + dy, 1 - cr.height));
            root.viewport.cropRect = Qt.rect(nx, ny, cr.width, cr.height);
        }
        onReleased: {
        }
    }

    // Straighten tool overlay — only in active mode
    Canvas {
        id: straightenCanvas
        anchors.fill: parent
        visible: root.active && root.straightenToolActive

        property point lineStart: Qt.point(0, 0)
        property point lineEnd: Qt.point(0, 0)
        property bool drawing: false

        onPaint: {
            var ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);
            if (!drawing) return;
            ctx.strokeStyle = "#00aaff";
            ctx.lineWidth = 2;
            ctx.setLineDash([6, 4]);
            ctx.beginPath();
            ctx.moveTo(lineStart.x, lineStart.y);
            ctx.lineTo(lineEnd.x, lineEnd.y);
            ctx.stroke();
        }

        MouseArea {
            anchors.fill: parent
            enabled: root.straightenToolActive
            cursorShape: Qt.CrossCursor

            onPressed: (mouse) => {
                straightenCanvas.lineStart = Qt.point(mouse.x, mouse.y);
                straightenCanvas.lineEnd = Qt.point(mouse.x, mouse.y);
                straightenCanvas.drawing = true;
                straightenCanvas.requestPaint();
            }
            onPositionChanged: (mouse) => {
                if (!pressed) return;
                straightenCanvas.lineEnd = Qt.point(mouse.x, mouse.y);
                straightenCanvas.requestPaint();
            }
            onReleased: (mouse) => {
                straightenCanvas.drawing = false;
                straightenCanvas.requestPaint();

                // Compute angle
                var dx = straightenCanvas.lineEnd.x - straightenCanvas.lineStart.x;
                var dy = straightenCanvas.lineEnd.y - straightenCanvas.lineStart.y;
                if (Math.abs(dx) < 5 && Math.abs(dy) < 5) return; // Too short

                var angleDeg = Math.atan2(dy, dx) * 180 / Math.PI;

                // If within ±45° of horizontal, align to horizontal
                // Otherwise align to vertical
                var correction;
                if (Math.abs(angleDeg) <= 45) {
                    correction = -angleDeg;
                } else {
                    correction = -(angleDeg - Math.sign(angleDeg) * 90);
                }

                // Clamp to ±45°
                correction = Math.max(-45, Math.min(45, correction));

                if (root.viewport) {
                    root.viewport.straightenAngle = correction;
                }
                root.straightenFinished();
            }
        }
    }
}
