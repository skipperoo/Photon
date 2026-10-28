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

    // RAW temperature track: the correction colour at each Kelvin value
    // (2000 K -> blue, 10000 K -> amber), from the Planckian locus.
    function kelvinTemperature(t) {
        var K = Math.max(1667, Math.min(25000, 2000 + Math.max(0, Math.min(1, t)) * 8000))
        var K2 = K * K
        var K3 = K2 * K
        var x
        if (K <= 4000)
            x = -0.2661239e9 / K3 - 0.2343589e6 / K2 + 0.8776956e3 / K + 0.179910
        else
            x = -3.0258469e9 / K3 + 2.1070379e6 / K2 + 0.2226347e3 / K + 0.240390
        var y
        if (K <= 2222)
            y = -1.1063814 * x * x * x - 1.34811020 * x * x + 2.18555832 * x - 0.20219683
        else if (K <= 4000)
            y = -0.9549476 * x * x * x - 1.37418593 * x * x + 2.09137015 * x - 0.16748867
        else
            y = 3.0817580 * x * x * x - 5.87338670 * x * x + 3.75112997 * x - 0.37001483
        y = Math.max(y, 1e-4)
        var X = x / y
        var Z = (1 - x - y) / y
        var r = Math.max(3.2404542 * X - 1.5371385 - 0.4985314 * Z, 1e-4)
        var g = Math.max(-0.9692660 * X + 1.8760108 + 0.0415560 * Z, 1e-4)
        var b = Math.max(0.0556434 * X - 0.2040259 + 1.0572252 * Z, 1e-4)
        var gr = 1.0 / r
        var gg = 1.0 / g
        var gb = 1.0 / b
        var m = Math.max(gr, Math.max(gg, gb))
        return Qt.rgba(Math.pow(gr / m, 1 / 2.2),
                       Math.pow(gg / m, 1 / 2.2),
                       Math.pow(gb / m, 1 / 2.2), 1.0)
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
