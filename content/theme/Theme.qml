pragma Singleton
import "."
import QtQuick
import Main

QtObject {
    // Theme switching logic
    readonly property bool isDark: AppState.isDarkMode

    // Palette Definitions
    readonly property color background: isDark ? "#09090b" : "#ffffff"
    readonly property color foreground: isDark ? "#fafafa" : "#09090b"
    readonly property color mutedFg:    isDark ? "#a1a1aa" : "#71717a"
    readonly property color primary:    isDark ? "#fafafa" : "#18181b"
    readonly property color primaryFg:  isDark ? "#18181b" : "#fafafa"
    readonly property color border:     isDark ? "#27272a" : "#e4e4e7"
    readonly property color input:      isDark ? "#27272a" : "#ffffff"
    readonly property color secondary:  isDark ? "#27272a" : "#f4f4f5"
    readonly property color accent:     AppState.accentColor
    readonly property color highlight:  isDark ? "#3f3f46" : "#f4f4f5"
    readonly property color destructive: "#7f1d1d"
    readonly property color ring:       isDark ? "#d4d4d8" : "#a1a1aa"
    readonly property color card:       isDark ? "#09090b" : "#ffffff"

    readonly property int radius: 6
    readonly property int radiusSm: 4
    readonly property int radiusLg: 8

    readonly property font fontRegular: Qt.font({ family: "sans-serif", pixelSize: 14 })
    readonly property font fontMedium: Qt.font({ family: "sans-serif", pixelSize: 14, weight: Font.Medium })
    readonly property font fontLarge: Qt.font({ family: "sans-serif", pixelSize: 18, weight: Font.Bold })
    readonly property font fontSmall: Qt.font({ family: "sans-serif", pixelSize: 12 })

    // Spacing
    readonly property int spacingXs: 4
    readonly property int spacingSm: 8
    readonly property int spacingMd: 12
    readonly property int spacingLg: 16
    readonly property int spacingXl: 24
    readonly property int spacing2xl: 32
}
