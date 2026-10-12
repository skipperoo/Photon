import QtQuick
import QtQuick.Controls
import Main

Slider {
    id: control
    from: 0
    to: 100
    value: 50

    property real defaultValue: 0.0
    property var trackRamp: null
    property bool showNeutralTick: true
    property var lastReleaseTime: 0
    signal doubleClicked()

    signal released()

    readonly property bool bipolar: defaultValue > from && defaultValue < to &&
                                    Math.abs(defaultValue - (from + to) / 2.0) < 1e-6
    readonly property real neutralPosition: normalized(defaultValue)
    readonly property real fillStartPosition: bipolar ? neutralPosition : 0.0
    readonly property color handleFill: trackRamp ? rampColor(normalized(value)) : Theme.background

    function normalized(t) {
        return Math.max(0.0, Math.min(1.0, (t - from) / Math.max(to - from, 1e-9)))
    }

    function rampColor(t) {
        return trackRamp ? trackRamp(Math.max(0.0, Math.min(1.0, t))) : "transparent"
    }

    onPressedChanged: {
        if (!pressed) {
            var currentTime = Date.now()
            if (currentTime - lastReleaseTime < 300) {
                control.value = control.defaultValue
                control.moved()
                control.doubleClicked()
                lastReleaseTime = 0 // Reset to prevent triple-click double-reset
            } else {
                lastReleaseTime = currentTime
            }
            released()
        }
    }

    background: Item {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: 200
        implicitHeight: 6
        width: control.availableWidth
        height: implicitHeight

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: Theme.secondary
        }

        Rectangle {
            anchors.fill: parent
            radius: parent.height / 2
            visible: control.trackRamp !== null
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.000000; color: control.rampColor(0.000000) }
                GradientStop { position: 0.041667; color: control.rampColor(0.041667) }
                GradientStop { position: 0.083333; color: control.rampColor(0.083333) }
                GradientStop { position: 0.125000; color: control.rampColor(0.125000) }
                GradientStop { position: 0.166667; color: control.rampColor(0.166667) }
                GradientStop { position: 0.208333; color: control.rampColor(0.208333) }
                GradientStop { position: 0.250000; color: control.rampColor(0.250000) }
                GradientStop { position: 0.291667; color: control.rampColor(0.291667) }
                GradientStop { position: 0.333333; color: control.rampColor(0.333333) }
                GradientStop { position: 0.375000; color: control.rampColor(0.375000) }
                GradientStop { position: 0.416667; color: control.rampColor(0.416667) }
                GradientStop { position: 0.458333; color: control.rampColor(0.458333) }
                GradientStop { position: 0.500000; color: control.rampColor(0.500000) }
                GradientStop { position: 0.541667; color: control.rampColor(0.541667) }
                GradientStop { position: 0.583333; color: control.rampColor(0.583333) }
                GradientStop { position: 0.625000; color: control.rampColor(0.625000) }
                GradientStop { position: 0.666667; color: control.rampColor(0.666667) }
                GradientStop { position: 0.708333; color: control.rampColor(0.708333) }
                GradientStop { position: 0.750000; color: control.rampColor(0.750000) }
                GradientStop { position: 0.791667; color: control.rampColor(0.791667) }
                GradientStop { position: 0.833333; color: control.rampColor(0.833333) }
                GradientStop { position: 0.875000; color: control.rampColor(0.875000) }
                GradientStop { position: 0.916667; color: control.rampColor(0.916667) }
                GradientStop { position: 0.958333; color: control.rampColor(0.958333) }
                GradientStop { position: 1.000000; color: control.rampColor(1.000000) }
            }
        }

        Rectangle {
            x: Math.min(control.fillStartPosition, control.visualPosition) * parent.width
            width: Math.abs(control.visualPosition - control.fillStartPosition) * parent.width
            height: parent.height
            radius: height / 2
            color: Theme.primary
            visible: control.trackRamp === null && width > 0.5
        }

        Rectangle {
            visible: control.bipolar && control.showNeutralTick
            x: Math.round(control.neutralPosition * parent.width) - 1
            width: 2
            height: parent.height
            radius: 1
            color: Theme.foreground
            opacity: 0.3
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: 16
        implicitHeight: 16
        radius: 8
        color: control.handleFill
        border.color: control.trackRamp ? Theme.background : Theme.primary
        border.width: 2
        scale: control.pressed ? 1.2 : (control.hovered ? 1.1 : 1.0)
        Behavior on scale { NumberAnimation { duration: 150 } }
        layer.enabled: true
    }
}
