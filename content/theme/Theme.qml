pragma Singleton
import "."
import QtQuick

QtObject {
    readonly property color background: "#09090b"
    readonly property color foreground: "#fafafa"
    readonly property color mutedFg:    "#a1a1aa"
    readonly property color primary:    "#fafafa"
    readonly property color primaryFg:  "#18181b"
    readonly property color border:     "#27272a"
    readonly property color input:      "#27272a"
    readonly property color secondary:  "#27272a"
    readonly property color accent:     "#27272a"
    readonly property color destructive: "#7f1d1d"
    readonly property color ring:       "#d4d4d8"
    readonly property color card:       "#09090b"

    readonly property int radius: 6
    readonly property int radiusSm: 4
    readonly property int radiusLg: 8

    readonly property font fontRegular: Qt.font({ family: "sans-serif", pixelSize: 14 })
    readonly property font fontMedium: Qt.font({ family: "sans-serif", pixelSize: 14, weight: Font.Medium })
    readonly property font fontLarge: Qt.font({ family: "sans-serif", pixelSize: 18, weight: Font.Bold })
    readonly property font fontSmall: Qt.font({ family: "sans-serif", pixelSize: 12 })
}
