import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Main
import "../theme"

Item {
    id: root
    property var viewport
    property int activeChannel: 0 // 0=Luma, 1=Red, 2=Green, 3=Blue

    readonly property bool parametricMode: root.viewport
                                           && root.viewport.toneCurveMode === "parametric"
    readonly property var channelNames: ["L", "R", "G", "B"]
    readonly property var channelColors: ["#ffffff", "#ef4444", "#22c55e", "#3b82f6"]
    readonly property var channelKeys: ["toneCurveLuma", "toneCurveRed", "toneCurveGreen", "toneCurveBlue"]

    signal curveChanged(int channel, var points)
    signal editFinished()

    // Debounce timer to throttle curve updates during drag
    property var _pendingPoints: null
    Timer {
        id: debounceTimer
        interval: 30
        repeat: false
        onTriggered: {
            if (root._pendingPoints !== null) {
                root._commitPoints(root._pendingPoints);
                root._pendingPoints = null;
            }
        }
    }

    function _commitPoints(pts) {
        if (!root.viewport) return;
        switch (root.activeChannel) {
            case 0: root.viewport.toneCurveLuma = pts; break;
            case 1: root.viewport.toneCurveRed = pts; break;
            case 2: root.viewport.toneCurveGreen = pts; break;
            case 3: root.viewport.toneCurveBlue = pts; break;
        }
        root.curveChanged(root.activeChannel, pts);
    }

    implicitHeight: channelTabs.height + canvas.height + 8

    // Parametric region curve (matches RawEngine/ImageDeveloper): cosine-windowed
    // amount per region, endpoint-pinned, monotone. Only active in parametric
    // mode; the stored values are stashed while the point curve is used.
    function evalParametric(x) {
        if (!root.viewport || root.viewport.toneCurveMode !== "parametric") return x;
        var s1 = root.viewport.toneCurveSplitShadows / 100.0;
        var s2 = root.viewport.toneCurveSplitMid / 100.0;
        var s3 = root.viewport.toneCurveSplitHighlights / 100.0;
        var amounts = [root.viewport.toneCurveShadows / 100.0,
                       root.viewport.toneCurveDarks / 100.0,
                       root.viewport.toneCurveLights / 100.0,
                       root.viewport.toneCurveHighlights / 100.0];
        var bounds = [0.0, s1, s2, s3, 1.0];
        var d = 0.0;
        for (var r = 0; r < 4; r++) {
            var amt = amounts[r];
            if (amt === 0) continue;
            var a = bounds[r];
            var b = bounds[r + 1];
            var ctr = (a + b) * 0.5;
            var half = (b - a) * 0.75 + 0.05;
            var tt = Math.max(-1, Math.min(1, (x - ctr) / half));
            var win = 0.5 + 0.5 * Math.cos(tt * Math.PI);
            d += amt * 0.22 * win;
        }
        return Math.max(0, Math.min(1, x + d * 4 * x * (1 - x)));
    }

    function getPoints() {
        // Parametric mode: the point curve is inactive (its data stays stored
        // in the edit stack); show the identity curve.
        if (root.parametricMode) return [{x: 0, y: 0}, {x: 1, y: 1}];
        // During drag, return pending points for smooth canvas rendering
        if (root._pendingPoints !== null) return root._pendingPoints;
        if (!root.viewport) return [{x: 0, y: 0}, {x: 1, y: 1}];
        switch (root.activeChannel) {
            case 0: return root.viewport.toneCurveLuma;
            case 1: return root.viewport.toneCurveRed;
            case 2: return root.viewport.toneCurveGreen;
            case 3: return root.viewport.toneCurveBlue;
        }
        return [{x: 0, y: 0}, {x: 1, y: 1}];
    }

    function setPoints(pts) {
        if (!root.viewport) return;
        // Immediate commit (for click to add, release, etc.)
        root._commitPoints(pts);
    }

    function setPointsThrottled(pts) {
        // Throttled commit (for drag moves)
        root._pendingPoints = pts;
        if (!debounceTimer.running)
            debounceTimer.restart();
        canvas.requestPaint();
    }

    // Channel tabs
    RowLayout {
        id: channelTabs
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 4
        visible: !root.parametricMode
        height: visible ? implicitHeight : 0

        Repeater {
            model: root.channelNames
            Rectangle {
                width: 28; height: 28
                radius: 14
                color: root.channelColors[index]
                opacity: root.activeChannel === index ? 1.0 : 0.3
                border.color: root.activeChannel === index ? Theme.foreground : "transparent"
                border.width: 2

                Text {
                    anchors.centerIn: parent
                    text: modelData
                    font: Theme.fontSmall
                    color: index === 0 ? "#000000" : "#ffffff"
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        root.activeChannel = index;
                        canvas.dragIndex = -1;
                        canvas.requestPaint();
                    }
                }
            }
        }
    }

    // Curve canvas
    Canvas {
        id: canvas
        anchors.top: channelTabs.bottom
        anchors.topMargin: 8
        anchors.left: parent.left
        anchors.right: parent.right
        height: width
        property int dragIndex: -1
        property real pointRadius: 6

        onPaint: {
            var ctx = getContext("2d");
            var w = canvas.width;
            var h = canvas.height;
            ctx.clearRect(0, 0, w, h);

            // Background
            ctx.fillStyle = Qt.rgba(0, 0, 0, 0.3);
            ctx.fillRect(0, 0, w, h);

            // Grid lines
            ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.08);
            ctx.lineWidth = 1;
            for (var g = 1; g < 4; g++) {
                var gp = g * w / 4;
                ctx.beginPath(); ctx.moveTo(gp, 0); ctx.lineTo(gp, h); ctx.stroke();
                ctx.beginPath(); ctx.moveTo(0, gp); ctx.lineTo(w, gp); ctx.stroke();
            }

            // Identity line (diagonal)
            ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.15);
            ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.moveTo(0, h);
            ctx.lineTo(w, 0);
            ctx.stroke();

            var pts = root.getPoints();
            if (!pts || pts.length < 2) return;

            // Evaluate spline at many points for smooth curve
            var curvePoints = evalSpline(pts, 200);

            // Compose the parametric region curve before the point curve so
            // the drawn line matches the rendered result.
            var composed = [];
            var composedMax = 0.0;
            for (var s = 0; s < curvePoints.length; s++) {
                var xx = curvePoints[s].x;
                var p = evalParametric(xx);
                var idx = Math.max(0, Math.min(curvePoints.length - 1,
                                               Math.round(p * (curvePoints.length - 1))));
                var yy = Math.max(composedMax, curvePoints[idx].y);
                composedMax = yy;
                composed.push({x: xx, y: yy});
            }

            // Draw curve
            var color = root.channelColors[root.activeChannel];
            ctx.strokeStyle = color;
            ctx.lineWidth = 2;
            ctx.beginPath();
            for (var i = 0; i < composed.length; i++) {
                var px = composed[i].x * w;
                var py = (1.0 - composed[i].y) * h;
                if (i === 0) ctx.moveTo(px, py);
                else ctx.lineTo(px, py);
            }
            ctx.stroke();

            // Draw control points (manual mode only)
            if (root.parametricMode) return;
            for (var j = 0; j < pts.length; j++) {
                var cx = pts[j].x * w;
                var cy = (1.0 - pts[j].y) * h;
                ctx.beginPath();
                ctx.arc(cx, cy, canvas.pointRadius, 0, 2 * Math.PI);
                ctx.fillStyle = (j === canvas.dragIndex) ? color : Theme.background;
                ctx.fill();
                ctx.strokeStyle = color;
                ctx.lineWidth = 2;
                ctx.stroke();
            }
        }

        // Monotonic cubic Hermite spline evaluation (Fritsch-Carlson)
        function evalSpline(pts, numSamples) {
            var n = pts.length;
            if (n < 2) return pts;

            var xs = [], ys = [];
            for (var i = 0; i < n; i++) {
                xs.push(pts[i].x);
                ys.push(pts[i].y);
            }

            // Slopes
            var delta = [];
            for (var i = 0; i < n - 1; i++) {
                var dx = xs[i+1] - xs[i];
                delta.push(dx > 1e-12 ? (ys[i+1] - ys[i]) / dx : 0);
            }

            // Tangents
            var m = new Array(n);
            m[0] = delta[0];
            m[n-1] = delta[n-2];
            for (var i = 1; i < n - 1; i++) {
                m[i] = (delta[i-1] + delta[i]) * 0.5;
            }

            // Fritsch-Carlson monotonicity
            for (var i = 0; i < n - 1; i++) {
                if (Math.abs(delta[i]) < 1e-12) {
                    m[i] = 0;
                    m[i+1] = 0;
                } else {
                    var alpha = m[i] / delta[i];
                    var beta = m[i+1] / delta[i];
                    var r2 = alpha * alpha + beta * beta;
                    if (r2 > 9) {
                        var tau = 3.0 / Math.sqrt(r2);
                        m[i] = tau * alpha * delta[i];
                        m[i+1] = tau * beta * delta[i];
                    }
                }
            }

            // Sample the spline
            var result = [];
            var seg = 0;
            for (var s = 0; s < numSamples; s++) {
                var t = s / (numSamples - 1);
                if (t <= xs[0]) { result.push({x: t, y: ys[0]}); continue; }
                if (t >= xs[n-1]) { result.push({x: t, y: ys[n-1]}); continue; }
                while (seg < n - 2 && t > xs[seg+1]) seg++;
                var dx = xs[seg+1] - xs[seg];
                var tt = (t - xs[seg]) / dx;
                var t2 = tt * tt;
                var t3 = t2 * tt;
                var h00 = 2*t3 - 3*t2 + 1;
                var h10 = t3 - 2*t2 + tt;
                var h01 = -2*t3 + 3*t2;
                var h11 = t3 - t2;
                var val = h00*ys[seg] + h10*dx*m[seg] + h01*ys[seg+1] + h11*dx*m[seg+1];
                result.push({x: t, y: Math.max(0, Math.min(1, val))});
            }
            return result;
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton
            preventStealing: true
            enabled: !root.parametricMode

            onPressed: function(mouse) {
                mouse.accepted = true;
                var pts = root.getPoints();
                var w = canvas.width;
                var h = canvas.height;
                var mx = mouse.x / w;
                var my = 1.0 - mouse.y / h;

                // Check if clicking an existing point
                for (var i = 0; i < pts.length; i++) {
                    var dx = (pts[i].x - mx) * w;
                    var dy = (pts[i].y - my) * h;
                    if (Math.sqrt(dx*dx + dy*dy) < canvas.pointRadius * 2) {
                        canvas.dragIndex = i;
                        canvas.requestPaint();
                        return;
                    }
                }

                // Add new point (sorted by x)
                var newPts = [];
                var inserted = false;
                for (var i = 0; i < pts.length; i++) {
                    if (!inserted && mx < pts[i].x) {
                        newPts.push({x: mx, y: my});
                        canvas.dragIndex = newPts.length - 1;
                        inserted = true;
                    }
                    newPts.push({x: pts[i].x, y: pts[i].y});
                }
                if (!inserted) {
                    newPts.push({x: mx, y: my});
                    canvas.dragIndex = newPts.length - 1;
                }
                root.setPoints(newPts);
                canvas.requestPaint();
            }

            onPositionChanged: function(mouse) {
                if (canvas.dragIndex < 0 || !pressed) return;
                var pts = root.getPoints();
                if (canvas.dragIndex >= pts.length) return;

                var w = canvas.width;
                var h = canvas.height;
                var my = Math.max(0, Math.min(1, 1.0 - mouse.y / h));

                // Points move on both axes: interior points clamp between their
                // neighbours, the endpoints can move inward (black/white point).
                var newPts = [];
                for (var i = 0; i < pts.length; i++) {
                    if (i === canvas.dragIndex) {
                        var lo = (i === 0) ? 0.0 : pts[i-1].x + 0.01;
                        var hi = (i + 1 >= pts.length) ? 1.0 : pts[i+1].x - 0.01;
                        var nx = Math.max(0.0, Math.min(1.0, mouse.x / w));
                        if (i === 0) nx = Math.min(nx, hi);
                        else if (i + 1 >= pts.length) nx = Math.max(nx, lo);
                        else nx = Math.max(lo, Math.min(hi, nx));
                        newPts.push({x: nx, y: my});
                    } else {
                        newPts.push({x: pts[i].x, y: pts[i].y});
                    }
                }
                root.setPointsThrottled(newPts);
            }

            onReleased: {
                if (canvas.dragIndex >= 0) {
                    // Flush any pending throttled update
                    debounceTimer.stop();
                    if (root._pendingPoints !== null) {
                        root._commitPoints(root._pendingPoints);
                        root._pendingPoints = null;
                    }
                    canvas.dragIndex = -1;
                    root.editFinished();
                    canvas.requestPaint();
                }
            }

            onDoubleClicked: function(mouse) {
                // Remove interior point on double-click
                var pts = root.getPoints();
                var w = canvas.width;
                var h = canvas.height;
                var mx = mouse.x / w;
                var my = 1.0 - mouse.y / h;

                for (var i = 1; i < pts.length - 1; i++) {
                    var dx = (pts[i].x - mx) * w;
                    var dy = (pts[i].y - my) * h;
                    if (Math.sqrt(dx*dx + dy*dy) < canvas.pointRadius * 2) {
                        var newPts = [];
                        for (var j = 0; j < pts.length; j++) {
                            if (j !== i) newPts.push({x: pts[j].x, y: pts[j].y});
                        }
                        canvas.dragIndex = -1;
                        root.setPoints(newPts);
                        root.editFinished();
                        canvas.requestPaint();
                        return;
                    }
                }
            }
        }
    }

    // Repaint when viewport curve properties change
    Connections {
        target: root.viewport
        function onToneCurveLumaChanged() { if (root.activeChannel === 0) canvas.requestPaint(); }
        function onToneCurveRedChanged() { if (root.activeChannel === 1) canvas.requestPaint(); }
        function onToneCurveGreenChanged() { if (root.activeChannel === 2) canvas.requestPaint(); }
        function onToneCurveBlueChanged() { if (root.activeChannel === 3) canvas.requestPaint(); }
        function onToneCurveModeChanged() { canvas.requestPaint(); }
        function onToneCurveShadowsChanged() { canvas.requestPaint(); }
        function onToneCurveDarksChanged() { canvas.requestPaint(); }
        function onToneCurveLightsChanged() { canvas.requestPaint(); }
        function onToneCurveHighlightsChanged() { canvas.requestPaint(); }
        function onToneCurveSplitShadowsChanged() { canvas.requestPaint(); }
        function onToneCurveSplitMidChanged() { canvas.requestPaint(); }
        function onToneCurveSplitHighlightsChanged() { canvas.requestPaint(); }
    }

    onActiveChannelChanged: canvas.requestPaint()
}
