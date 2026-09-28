import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import Main

ComboBox {
    id: control

    implicitWidth: 180
    implicitHeight: 36
    leftPadding: 12
    rightPadding: 32

    contentItem: Text {
        text: control.displayText
        font: Theme.fontRegular
        color: control.enabled ? Theme.foreground : Theme.mutedFg
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Text {
        x: control.width - width - 12
        y: (control.height - height) / 2
        text: "\u25BE"
        font: Theme.fontSmall
        color: Theme.mutedFg
    }

    background: Rectangle {
        radius: Theme.radiusLg
        color: control.pressed || control.popup.visible ? Theme.highlight : Theme.secondary
        border.color: control.activeFocus ? Theme.accent : Theme.border
        border.width: 1
    }

    delegate: ItemDelegate {
        id: optionItem
        width: ListView.view ? ListView.view.width : control.width
        implicitHeight: 32
        highlighted: control.highlightedIndex === index
        text: control.textRole
              ? (typeof modelData === "object" ? modelData[control.textRole] : modelData)
              : modelData

        contentItem: Text {
            text: optionItem.text
            font: Theme.fontRegular
            color: Theme.foreground
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: Theme.radius
            color: optionItem.highlighted ? Theme.highlight : "transparent"
        }
    }

    popup: Popup {
        y: control.height + 8
        width: control.width
        implicitHeight: contentItem.implicitHeight + 8
        padding: 4

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
        }

        background: Item {
            Rectangle {
                id: popupBackground
                anchors.fill: parent
                radius: Theme.radiusLg
                color: Theme.secondary
                border.color: Theme.border
                border.width: 1
            }

            MultiEffect {
                source: popupBackground
                anchors.left: popupBackground.left
                anchors.right: popupBackground.right
                anchors.top: popupBackground.top
                anchors.bottom: popupBackground.bottom
                anchors.leftMargin: -10
                anchors.rightMargin: -10
                anchors.topMargin: -6
                anchors.bottomMargin: -14
                shadowEnabled: true
                shadowColor: Qt.rgba(0, 0, 0, 0.55)
                shadowBlur: 0.8
                shadowVerticalOffset: 6
            }
        }
    }
}
