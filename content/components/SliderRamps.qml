pragma Singleton
import QtQuick

QtObject {
    function hsv(hueDeg, s, v) {
        var h = (((hueDeg % 360) + 360) % 360) / 60.0
        var i = Math.floor(h)
        var f = h - i
        var p = v * (1.0 - s)
        var q = v * (1.0 - s * f)
        var t = v * (1.0 - s * (1.0 - f))
        var r = 0.0, g = 0.0, b = 0.0
        switch (i % 6) {
        case 0: r = v; g = t; b = p; break
        case 1: r = q; g = v; b = p; break
        case 2: r = p; g = v; b = t; break
        case 3: r = p; g = q; b = v; break
        case 4: r = t; g = p; b = v; break
        default: r = v; g = p; b = q; break
        }
        return Qt.rgba(r, g, b, 1.0)
    }

    function mix(a, b, u) {
        return Qt.rgba(a.r + (b.r - a.r) * u,
                       a.g + (b.g - a.g) * u,
                       a.b + (b.b - a.b) * u, 1.0)
    }

    function whiteBalanceTemperature(t) {
        var u = t * 2.0 - 1.0
        var r = Math.max(0.0, 1.0 + u * 0.20)
        var g = Math.max(0.0, 1.0 + u * 0.05)
        var b = Math.max(0.0, 1.0 - u * 0.20)
        var m = Math.max(r, Math.max(g, b))
        return Qt.rgba(r / m, g / m, b / m, 1.0)
    }

    function whiteBalanceTint(t) {
        var u = t * 2.0 - 1.0
        var r = Math.max(0.0, 1.0 + u * 0.25)
        var g = Math.max(0.0, 1.0 - u * 0.25)
        var b = Math.max(0.0, 1.0 + u * 0.25)
        var m = Math.max(r, Math.max(g, b))
        return Qt.rgba(r / m, g / m, b / m, 1.0)
    }

    function shadowsTint(t) {
        var green = Qt.rgba(0.27, 0.66, 0.36, 1.0)
        var grey = Qt.rgba(0.5, 0.5, 0.5, 1.0)
        var magenta = Qt.rgba(0.70, 0.30, 0.74, 1.0)
        if (t < 0.5)
            return mix(green, grey, t * 2.0)
        return mix(grey, magenta, (t - 0.5) * 2.0)
    }

    function primaryHue(baseHueDeg, t) {
        var shift = (t * 2.0 - 1.0) * 28.6479
        return hsv(baseHueDeg + shift, 1.0, 1.0)
    }

    function primarySaturation(baseHueDeg, t) {
        return hsv(baseHueDeg, 0.40 + 0.60 * t, 1.0)
    }

    function bandHue(bandHueDeg, t) {
        var shift = (t * 2.0 - 1.0) * 36.0
        return hsv(bandHueDeg + shift, 0.95, 0.95)
    }

    function bandSaturation(bandHueDeg, t) {
        return hsv(bandHueDeg, 0.18 + 0.82 * t, 0.95)
    }

    function bandLuminance(bandHueDeg, t) {
        return hsv(bandHueDeg, 0.85, 0.10 + 0.85 * t)
    }

    function rainbow(t) {
        return hsv(t * 360.0, 1.0, 1.0)
    }

    function zoneSaturation(hueDeg, t) {
        var grey = Qt.rgba(0.55, 0.55, 0.55, 1.0)
        return mix(grey, hsv(hueDeg, 1.0, 1.0), t)
    }

    function zoneLuminance(hueDeg, t) {
        return hsv(hueDeg, 0.80, 0.15 + 0.75 * t)
    }
}
