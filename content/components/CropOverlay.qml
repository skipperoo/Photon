import QtQuick
import Main

Item {
    id: root

    property var viewport: null
    property rect imageRect: viewport ? viewport.imageRect : Qt.rect(0, 0, 0, 0)
    property bool active: false
    property bool _autoCropUpdate: false
    property bool _autoStraightenCropLinked: false
    property rect _lastAutoStraightenRect: Qt.rect(0, 0, 1, 1)
    property bool _draggingCrop: false
    onActiveChanged: {
        if (active) {
            maybeApplyAutoStraightenCrop();
        }
    }
    property bool straightenToolActive: false
    // When not in active crop mode, show a preview mask if crop is non-default
    readonly property bool hasCrop: {
        var cr = cropRect;
        return cr.x > 0.001 || cr.y > 0.001 || cr.width < 0.999 || cr.height < 0.999;
    }
    readonly property bool showPreview: !active && hasCrop && imageRect.width > 0 &&
                                        (!viewport || !viewport.geometryBaked)

    signal straightenFinished()

    visible: (active || showPreview) && imageRect.width > 0
    z: active ? 10 : (showPreview ? 5 : -1)
    readonly property real domainEps: 0.0005

    // Display rect: the usable image area on screen after QML transforms.
    // For 90° steps + flip: exact rotated/mirrored position.
    // For straighten: axis-aligned bounding box of the rotated image.
    // The default auto-crop for straighten is handled via cropRect updates.
    function rectClose(a, b, eps) {
        return Math.abs(a.x - b.x) <= eps &&
               Math.abs(a.y - b.y) <= eps &&
               Math.abs(a.width - b.width) <= eps &&
               Math.abs(a.height - b.height) <= eps;
    }

    function rotatePoint(px, py, cx, cy, rad) {
        var dx = px - cx;
        var dy = py - cy;
        var cosR = Math.cos(rad);
        var sinR = Math.sin(rad);
        return Qt.point(cx + dx * cosR - dy * sinR,
                        cy + dx * sinR + dy * cosR);
    }

    function computeBaseRect() {
        var ir = imageRect;
        if (!viewport || viewport.geometryBaked) return ir;

        var steps = viewport.orientationSteps % 4;
        var fH = viewport.flipHorizontal || false;
        var fV = viewport.flipVertical || false;

        if (steps === 0 && !fH && !fV) return ir;

        var cx = root.width / 2;
        var cy = root.height / 2;

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

        if (fH) rx = 2 * cx - rx - rw;
        if (fV) ry = 2 * cy - ry - rh;

        return Qt.rect(rx, ry, rw, rh);
    }

    function computeAutoStraightenCropRect() {
        if (!viewport || viewport.geometryBaked) return Qt.rect(0, 0, 1, 1);
        var straighten = Math.abs(viewport.straightenAngle || 0);
        var br = baseRect;
        if (straighten < 0.01 || br.width <= 0 || br.height <= 0) {
            return Qt.rect(0, 0, 1, 1);
        }

        var rw = br.width;
        var rh = br.height;
        var rad = straighten * Math.PI / 180;
        var cosT = Math.cos(rad);
        var sinT = Math.sin(rad);
        var boxW = rw * cosT + rh * sinT;
        var boxH = rw * sinT + rh * cosT;
        var s1 = rw / (rw * cosT + rh * sinT);
        var s2 = rh / (rw * sinT + rh * cosT);
        var s = Math.min(s1, s2);
        var cropW = rw * s;
        var cropH = rh * s;
        var nw = cropW / boxW;
        var nh = cropH / boxH;
        return Qt.rect((1 - nw) / 2, (1 - nh) / 2, nw, nh);
    }

    function computeValidDomain() {
        if (!viewport || viewport.geometryBaked || displayRect.width <= 0 || displayRect.height <= 0) {
            return [Qt.point(0, 0), Qt.point(1, 0), Qt.point(1, 1), Qt.point(0, 1)];
        }

        var straighten = viewport.straightenAngle || 0;
        if (Math.abs(straighten) < 0.01) {
            return [Qt.point(0, 0), Qt.point(1, 0), Qt.point(1, 1), Qt.point(0, 1)];
        }

        var br = baseRect;
        var dr = displayRect;
        var cx = root.width / 2;
        var cy = root.height / 2;
        var rad = straighten * Math.PI / 180;
        var pts = [
            rotatePoint(br.x, br.y, cx, cy, rad),
            rotatePoint(br.x + br.width, br.y, cx, cy, rad),
            rotatePoint(br.x + br.width, br.y + br.height, cx, cy, rad),
            rotatePoint(br.x, br.y + br.height, cx, cy, rad)
        ];

        var out = [];
        for (var i = 0; i < pts.length; ++i) {
            out.push(Qt.point((pts[i].x - dr.x) / dr.width,
                              (pts[i].y - dr.y) / dr.height));
        }
        return out;
    }

    function isPointInsideDomain(p) {
        var poly = validDomain;
        var sign = 0;
        var eps = domainEps;
        for (var i = 0; i < poly.length; ++i) {
            var a = poly[i];
            var b = poly[(i + 1) % poly.length];
            var cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
            if (Math.abs(cross) <= eps) continue;
            var currentSign = cross > 0 ? 1 : -1;
            if (sign === 0) {
                sign = currentSign;
            } else if (currentSign !== sign) {
                return false;
            }
        }
        return true;
    }

    function isRectInDomain(r) {
        var eps = domainEps;
        if (r.width <= 0 || r.height <= 0) return false;
        if (r.x < -eps || r.y < -eps) return false;
        if (r.x + r.width > 1.0 + eps || r.y + r.height > 1.0 + eps) return false;

        var c0 = Qt.point(r.x, r.y);
        var c1 = Qt.point(r.x + r.width, r.y);
        var c2 = Qt.point(r.x + r.width, r.y + r.height);
        var c3 = Qt.point(r.x, r.y + r.height);
        return isPointInsideDomain(c0) &&
               isPointInsideDomain(c1) &&
               isPointInsideDomain(c2) &&
               isPointInsideDomain(c3);
    }

    function lerpRect(a, b, t) {
        return Qt.rect(a.x + (b.x - a.x) * t,
                       a.y + (b.y - a.y) * t,
                       a.width + (b.width - a.width) * t,
                       a.height + (b.height - a.height) * t);
    }

    function projectRectToValidFromStart(startRect, targetRect) {
        var target = Qt.rect(
            Math.max(0, Math.min(targetRect.x, 1 - targetRect.width)),
            Math.max(0, Math.min(targetRect.y, 1 - targetRect.height)),
            Math.min(targetRect.width, 1),
            Math.min(targetRect.height, 1)
        );

        if (isRectInDomain(target)) return target;

        var start = startRect;
        if (!isRectInDomain(start)) {
            var autoRect = computeAutoStraightenCropRect();
            if (isRectInDomain(autoRect)) {
                start = autoRect;
            } else {
                start = Qt.rect(0, 0, 1, 1);
            }
        }

        if (!isRectInDomain(start)) return target;

        var lo = 0.0;
        var hi = 1.0;
        for (var i = 0; i < 24; ++i) {
            var mid = (lo + hi) * 0.5;
            var probe = lerpRect(start, target, mid);
            if (isRectInDomain(probe)) {
                lo = mid;
            } else {
                hi = mid;
            }
        }

        return lerpRect(start, target, lo);
    }

    function ensureCurrentCropRectValid() {
        if (!viewport || viewport.geometryBaked) return;
        var current = viewport.cropRect;
        if (isRectInDomain(current)) return;

        var safeStart = computeAutoStraightenCropRect();
        var fixed = projectRectToValidFromStart(safeStart, current);
        if (!rectClose(current, fixed, 0.0005)) {
            console.info("[CropOverlay] clamp adjust current=(" + current.x.toFixed(4) + "," + current.y.toFixed(4) + "," +
                         current.width.toFixed(4) + "," + current.height.toFixed(4) + ") -> fixed=(" +
                         fixed.x.toFixed(4) + "," + fixed.y.toFixed(4) + "," + fixed.width.toFixed(4) + "," +
                         fixed.height.toFixed(4) + ")");
            _autoCropUpdate = true;
            viewport.cropRect = fixed;
            _autoCropUpdate = false;
        }
    }

    function maybeApplyAutoStraightenCrop() {
        if (!active || !viewport || viewport.geometryBaked) return;

        var angle = Math.abs(viewport.straightenAngle || 0);
        var current = viewport.cropRect;
        var fullRect = Qt.rect(0, 0, 1, 1);

        if (angle < 0.01) {
            if (_autoStraightenCropLinked && rectClose(current, _lastAutoStraightenRect, 0.002)) {
                _autoCropUpdate = true;
                viewport.cropRect = fullRect;
                _autoCropUpdate = false;
            }
            _autoStraightenCropLinked = false;
            return;
        }

        var shouldAuto = _autoStraightenCropLinked
            ? rectClose(current, _lastAutoStraightenRect, 0.002)
            : rectClose(current, fullRect, 0.002);
        if (!shouldAuto) return;

        var autoRect = computeAutoStraightenCropRect();
        _autoCropUpdate = true;
        viewport.cropRect = autoRect;
        _autoCropUpdate = false;
        _lastAutoStraightenRect = autoRect;
        _autoStraightenCropLinked = true;
    }

    readonly property rect baseRect: computeBaseRect()
    readonly property rect displayRect: {
        var br = baseRect;
        if (!viewport || viewport.geometryBaked) return br;

        var straighten = viewport.straightenAngle || 0;
        if (Math.abs(straighten) < 0.01) return br;

        var cx = root.width / 2;
        var cy = root.height / 2;
        var rad = straighten * Math.PI / 180;

        var p0 = rotatePoint(br.x, br.y, cx, cy, rad);
        var p1 = rotatePoint(br.x + br.width, br.y, cx, cy, rad);
        var p2 = rotatePoint(br.x + br.width, br.y + br.height, cx, cy, rad);
        var p3 = rotatePoint(br.x, br.y + br.height, cx, cy, rad);

        var minX = Math.min(p0.x, p1.x, p2.x, p3.x);
        var maxX = Math.max(p0.x, p1.x, p2.x, p3.x);
        var minY = Math.min(p0.y, p1.y, p2.y, p3.y);
        var maxY = Math.max(p0.y, p1.y, p2.y, p3.y);
        return Qt.rect(minX, minY, maxX - minX, maxY - minY);
    }
    readonly property var validDomain: computeValidDomain()

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

    Connections {
        target: root.viewport
        function onStraightenAngleChanged() {
            root.maybeApplyAutoStraightenCrop();
            root.ensureCurrentCropRectValid();
        }
        function onCropRectChanged() {
            if (!root.viewport || root._autoCropUpdate) return;
            if (root._autoStraightenCropLinked &&
                !root.rectClose(root.viewport.cropRect, root._lastAutoStraightenRect, 0.002)) {
                root._autoStraightenCropLinked = false;
            }
        }
    }

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
                property point dragLast

                onPressed: (mouse) => {
                    dragStart = mapToItem(root, mouse.x, mouse.y);
                    dragLast = dragStart;
                    root._draggingCrop = true;
                }
                onPositionChanged: (mouse) => {
                    if (!pressed) return;
                    if (root.imgW <= 0 || root.imgH <= 0) return;
                    var pos = mapToItem(root, mouse.x, mouse.y);
                    var dx = (pos.x - dragLast.x) / root.imgW;
                    var dy = (pos.y - dragLast.y) / root.imgH;
                    var cr = root.viewport.cropRect;
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

                    // Clamp to image bounds, preserving aspect ratio
                    if (lockRatio) {
                        var imgAR = root.imgW / root.imgH;
                        var normR = ratio / imgAR;

                        // Clamp size to fit within [0,1]
                        if (nw > 1) { nw = 1; nh = nw / normR; }
                        if (nh > 1) { nh = 1; nw = nh * normR; }

                        // Clamp position, then re-check if size still fits
                        if (nx < 0) nx = 0;
                        if (ny < 0) ny = 0;
                        if (nx + nw > 1) { nx = 1 - nw; if (nx < 0) { nx = 0; nw = 1; nh = nw / normR; } }
                        if (ny + nh > 1) { ny = 1 - nh; if (ny < 0) { ny = 0; nh = 1; nw = nh * normR; } }
                    } else {
                        nx = Math.max(0, Math.min(nx, 1 - nw));
                        ny = Math.max(0, Math.min(ny, 1 - nh));
                        nw = Math.min(nw, 1 - nx);
                        nh = Math.min(nh, 1 - ny);
                    }

                    var candidate = Qt.rect(nx, ny, nw, nh);
                    candidate = root.projectRectToValidFromStart(cr, candidate);
                    root.viewport.cropRect = candidate;
                    dragLast = pos;
                }
                onReleased: {
                    root._draggingCrop = false;
                    root.ensureCurrentCropRectValid();
                    var cr = root.viewport.cropRect;
                    console.info("[CropOverlay] handle release crop=(" + cr.x.toFixed(4) + "," + cr.y.toFixed(4) + "," +
                                 cr.width.toFixed(4) + "," + cr.height.toFixed(4) + ") display=(" +
                                 root.displayRect.x.toFixed(2) + "," + root.displayRect.y.toFixed(2) + "," +
                                 root.displayRect.width.toFixed(2) + "," + root.displayRect.height.toFixed(2) + ")");
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
        property point dragLast

        onPressed: (mouse) => {
            dragStart = mapToItem(root, mouse.x, mouse.y);
            dragLast = dragStart;
            root._draggingCrop = true;
        }
        onPositionChanged: (mouse) => {
            if (!pressed) return;
            if (root.imgW <= 0 || root.imgH <= 0) return;
            var pos = mapToItem(root, mouse.x, mouse.y);
            var dx = (pos.x - dragLast.x) / root.imgW;
            var dy = (pos.y - dragLast.y) / root.imgH;
            var cr = root.viewport.cropRect;
            var stepRect = cr;
            if (Math.abs(dx) > 0.000001) {
                var nx = Math.max(0, Math.min(stepRect.x + dx, 1 - stepRect.width));
                var candX = Qt.rect(nx, stepRect.y, stepRect.width, stepRect.height);
                stepRect = root.projectRectToValidFromStart(stepRect, candX);
            }
            if (Math.abs(dy) > 0.000001) {
                var ny = Math.max(0, Math.min(stepRect.y + dy, 1 - stepRect.height));
                var candY = Qt.rect(stepRect.x, ny, stepRect.width, stepRect.height);
                stepRect = root.projectRectToValidFromStart(stepRect, candY);
            }
            root.viewport.cropRect = stepRect;
            dragLast = pos;
        }
        onReleased: {
            root._draggingCrop = false;
            root.ensureCurrentCropRectValid();
            var cr = root.viewport.cropRect;
            console.info("[CropOverlay] move release crop=(" + cr.x.toFixed(4) + "," + cr.y.toFixed(4) + "," +
                         cr.width.toFixed(4) + "," + cr.height.toFixed(4) + ") display=(" +
                         root.displayRect.x.toFixed(2) + "," + root.displayRect.y.toFixed(2) + "," +
                         root.displayRect.width.toFixed(2) + "," + root.displayRect.height.toFixed(2) + ")");
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
