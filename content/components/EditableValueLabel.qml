import QtQuick
import QtQuick.Controls
import Main

// Numeric value label used next to sliders. Clicking it opens a small input
// field in place; Enter or clicking outside commits the typed value (clamped
// to [from, to] and snapped to stepSize), Escape cancels. The label returns to
// its normal read-only state after editing.
Text {
    id: root
    property real value: 0
    property real from: 0
    property real to: 100
    property real stepSize: 0
    property int decimals: 0
    property string suffix: ""

    signal valueEdited(real value)

    text: Number(root.value).toFixed(root.decimals) + root.suffix
    font: Theme.fontSmall
    color: Theme.foreground

    HoverHandler { cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: editor.open() }

    Popup {
        id: editor
        parent: root
        width: Math.max(72, root.width + 16)
        height: Math.max(26, root.height + 6)
        // Right-align the field to the label so it stays inside the panel card,
        // and never let it run past the window's left edge.
        x: Math.max(6 - root.mapToItem(null, 0, 0).x, root.width - width)
        y: (root.height - height) / 2
        padding: 0
        modal: false
        focus: true
        closePolicy: Popup.CloseOnPressOutside
        property bool cancelOnClose: false

        background: Rectangle {
            color: Theme.secondary
            border.color: field.activeFocus ? Theme.accent : Theme.border
            border.width: 1
            radius: Theme.radiusSm
        }

        contentItem: TextField {
            id: field
            leftPadding: 8
            rightPadding: 8
            topPadding: 2
            bottomPadding: 2
            verticalAlignment: TextInput.AlignVCenter
            horizontalAlignment: TextInput.AlignHCenter
            font: Theme.fontSmall
            color: Theme.foreground
            selectionColor: Theme.accent
            selectedTextColor: Theme.primaryFg
            selectByMouse: true
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            background: null
            onAccepted: editor.close()
            Keys.onEscapePressed: {
                editor.cancelOnClose = true
                editor.close()
            }
        }

        onOpened: {
            field.text = Number(root.value).toFixed(root.decimals)
            field.forceActiveFocus()
            field.selectAll()
        }

        onClosed: {
            if (!cancelOnClose) {
                var parsed = parseFloat(String(field.text).replace(",", ".").trim())
                if (!isNaN(parsed)) {
                    parsed = Math.max(root.from, Math.min(root.to, parsed))
                    if (root.stepSize > 0)
                        parsed = root.from + Math.round((parsed - root.from) / root.stepSize) * root.stepSize
                    // Keep the precision the label displays.
                    var factor = Math.pow(10, root.decimals)
                    parsed = Math.round(parsed * factor) / factor
                    if (Math.abs(parsed - root.value) > 1e-9)
                        root.valueEdited(parsed)
                }
            }
            cancelOnClose = false
        }
    }
}
