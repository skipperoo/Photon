import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as T
import QtQuick.Controls.Basic as B
import QtQuick.Dialogs
import Main

// Develop presets: favorites, the open folder's presets ("This folder") and
// all presets, each with one level of folders. Click applies; the star pins
// a global preset to Favorites; drag a preset onto a folder (or a section
// title) to move it; right-click or "..." for more.
Rectangle {
    id: root
    implicitWidth: 320
    color: Theme.background
    border.color: Theme.border
    border.width: 0

    // Left border
    Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.border }

    property var viewport: null
    property real viewTopPadding: 0
    property var pendingPresetSettings: ({})
    property var collapsed: ({})  // "scope/folder" -> true when collapsed

    // The preset or folder a context menu acts on.
    property string menuScope: ""
    property string menuFolder: ""
    property string menuName: ""
    property var menuFolders: []

    signal notify(string message, string type)

    readonly property string scopeGlobal: "global"
    readonly property string scopeProject: "project"

    function applyPreset(scope, folder, name) {
        var settings = PresetManager.loadPreset(scope, folder, name)
        if (Object.keys(settings).length > 0 && root.viewport)
            root.viewport.applySettings(settings)
    }

    function isCollapsed(scope, folder) {
        return root.collapsed[scope + "/" + folder] === true
    }

    function toggleCollapsed(scope, folder) {
        var c = Object.assign({}, root.collapsed)
        var key = scope + "/" + folder
        c[key] = !(c[key] === true)
        root.collapsed = c
    }

    function scopeLabel(scope) {
        return scope === root.scopeProject ? "this folder" : "all presets"
    }

    function moveTo(scope, folder, name, toScope, toFolder) {
        if (scope === toScope && folder === toFolder)
            return
        if (!PresetManager.movePreset(scope, folder, name, toScope, toFolder))
            root.notify("“" + name + "” already exists there", "error")
    }

    function askText(title, label, initial, onAccepted) {
        textDialog.title = title
        textDialog.label = label
        textInput.text = initial
        textDialog.callback = onAccepted
        textDialog.open()
        textInput.selectAll()
        textInput.forceActiveFocus()
    }

    function confirm(title, text, onYes) {
        confirmDialog.title = title
        confirmDialog.text = text
        confirmDialog.callback = onYes
        confirmDialog.open()
    }

    function openPresetMenu(scope, folder, name) {
        root.menuScope = scope
        root.menuFolder = folder
        root.menuName = name
        root.menuFolders = PresetManager.folders(scope)
        presetMenu.popup()
    }

    function openFolderMenu(scope, folder) {
        root.menuScope = scope
        root.menuFolder = folder
        root.menuName = ""
        folderMenu.popup()
    }

    function startImport(scope) {
        importDialog.scope = scope
        importDialog.open()
    }

    // kind: "preset" | "folder" | "favorites"
    function startExport(kind, scope, folder, name) {
        exportDialog.kind = kind
        exportDialog.scope = scope
        exportDialog.folder = folder
        exportDialog.name = name
        exportDialog.nameFilters = kind === "preset" ? ["Photon preset (*.json)"] : ["Photon presets (*.zip)"]
        exportDialog.defaultSuffix = kind === "preset" ? "json" : "zip"
        var base = kind === "preset" ? name
                 : kind === "favorites" ? "Favorite presets"
                 : folder !== "" ? folder
                 : scope === root.scopeProject ? PresetManager.projectName + " presets" : "Presets"
        exportDialog.selectedFile = exportDialog.currentFolder + "/" + base + "." + exportDialog.defaultSuffix
        exportDialog.open()
    }

    // ─── Styled menus (the house context menu look) ──────────────────────────

    component MenuBg: Rectangle {
        color: Theme.secondary
        border.color: Theme.border
        border.width: 1
        radius: Theme.radius
    }

    component StyledMenuItem: B.MenuItem {
        id: item
        implicitWidth: 220
        implicitHeight: visible ? 34 : 0
        leftPadding: 12
        rightPadding: 12
        topPadding: 0
        bottomPadding: 0
        background: Rectangle {
            color: item.highlighted ? Theme.accent : "transparent"
            radius: Theme.radius
            anchors.fill: parent
            anchors.margins: 2
        }
        contentItem: Text {
            text: item.text
            font: Theme.fontSmall
            color: item.enabled ? Theme.foreground : Theme.mutedFg
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        arrow: Text {
            x: item.width - width - 10
            y: (item.height - height) / 2
            visible: item.subMenu !== null
            text: "▸"
            color: Theme.mutedFg
            font: Theme.fontSmall
        }
    }

    component StyledMenu: B.Menu {
        implicitWidth: 220
        topPadding: 4
        bottomPadding: 4
        background: MenuBg {}
        delegate: StyledMenuItem {}
    }

    component StyledSeparator: B.MenuSeparator {
        topPadding: 4
        bottomPadding: 4
        contentItem: Rectangle {
            implicitWidth: 200
            implicitHeight: 1
            color: Theme.border
        }
    }

    // ─── Rows ─────────────────────────────────────────────────────────────────

    component HeaderButton: T.Button {
        id: hb
        property string tip: ""
        property bool active: false
        implicitWidth: 26
        implicitHeight: 26
        icon.width: 15
        icon.height: 15
        icon.color: hb.active ? Theme.accent : Theme.foreground
        display: icon.source != "" ? T.AbstractButton.IconOnly : T.AbstractButton.TextOnly
        T.ToolTip.visible: hovered && tip !== ""
        T.ToolTip.text: tip
        contentItem: Item {
            Image {
                anchors.centerIn: parent
                visible: hb.icon.source != ""
                source: hb.icon.source
                sourceSize.width: hb.icon.width
                sourceSize.height: hb.icon.height
            }
            Text {
                anchors.centerIn: parent
                visible: hb.icon.source == ""
                text: hb.text
                font: Theme.fontMedium
                color: hb.active ? Theme.accent : Theme.foreground
            }
        }
        background: Rectangle {
            color: hb.hovered || hb.active ? Theme.secondary : "transparent"
            radius: 4
        }
    }

    // A section title; dropping a preset on it moves the preset out of its
    // folder into this section ("dropScope" "" = not a drop target).
    component SectionTitle: Rectangle {
        id: st
        property var panel
        property string title: ""
        property string dropScope: ""
        Layout.fillWidth: true
        Layout.topMargin: 8
        implicitHeight: 24
        radius: 4
        color: titleDrop.containsDrag ? Theme.highlight : "transparent"
        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: 4
            width: parent.width - 8
            text: st.title
            color: Theme.mutedFg
            font.pixelSize: Theme.fontSmall.pixelSize
            font.bold: true
            elide: Text.ElideRight
        }
        DropArea {
            id: titleDrop
            anchors.fill: parent
            enabled: st.dropScope !== ""
            keys: ["photon-preset"]
            onDropped: (drop) => st.panel.moveTo(drop.source.scope, drop.source.folder,
                                                 drop.source.name, st.dropScope, "")
        }
    }

    component FolderHeader: Rectangle {
        id: fh
        property var panel
        property string scope: ""
        property string folder: ""
        property int count: 0
        Layout.fillWidth: true
        implicitHeight: 32
        radius: 4
        color: folderDrop.containsDrag ? Theme.highlight
             : folderHover.hovered ? Theme.secondary : "transparent"
        HoverHandler { id: folderHover }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: (mouse) => {
                if (mouse.button === Qt.RightButton)
                    fh.panel.openFolderMenu(fh.scope, fh.folder)
                else
                    fh.panel.toggleCollapsed(fh.scope, fh.folder)
            }
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 4
            spacing: 6
            Text {
                text: fh.panel.isCollapsed(fh.scope, fh.folder) ? "▸" : "▾"
                color: Theme.mutedFg
                font: Theme.fontSmall
            }
            Text {
                text: fh.folder
                color: Theme.foreground
                font: Theme.fontMedium
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                text: fh.count
                color: Theme.mutedFg
                font: Theme.fontSmall
                visible: !folderHover.hovered
            }
            HeaderButton {
                visible: folderHover.hovered
                text: "⋯"
                tip: "Folder options"
                onClicked: fh.panel.openFolderMenu(fh.scope, fh.folder)
            }
        }
        DropArea {
            id: folderDrop
            anchors.fill: parent
            keys: ["photon-preset"]
            onDropped: (drop) => fh.panel.moveTo(drop.source.scope, drop.source.folder,
                                                 drop.source.name, fh.scope, fh.folder)
        }
    }

    component PresetRow: Rectangle {
        id: row
        property var panel
        property string scope: ""
        property string folder: ""
        property string name: ""
        property bool favorite: false
        property bool showFolder: false
        property bool indented: false
        property bool dragged: false
        Layout.fillWidth: true
        implicitHeight: showFolder && folder !== "" ? 40 : 32
        radius: 4
        color: rowHover.hovered ? Theme.secondary : "transparent"
        HoverHandler { id: rowHover }

        MouseArea {
            id: rowMouse
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            preventStealing: true
            drag.target: ghost
            drag.threshold: 10
            onPressed: {
                row.dragged = false
                var p = row.mapToItem(ghost.parent, 0, 0)
                ghost.x = p.x
                ghost.y = p.y
            }
            onPositionChanged: if (drag.active) row.dragged = true
            onReleased: {
                if (ghost.Drag.active)
                    ghost.Drag.drop()
            }
            onClicked: (mouse) => {
                if (mouse.button === Qt.RightButton)
                    row.panel.openPresetMenu(row.scope, row.folder, row.name)
                else if (!row.dragged)
                    row.panel.applyPreset(row.scope, row.folder, row.name)
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: row.indented ? 24 : 8
            anchors.rightMargin: 4
            spacing: 2
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    text: row.name
                    color: Theme.foreground
                    font: Theme.fontMedium
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    visible: row.showFolder && row.folder !== ""
                    text: row.folder
                    color: Theme.mutedFg
                    font: Theme.fontSmall
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
            HeaderButton {
                visible: row.scope === "global" && (row.favorite || rowHover.hovered)
                icon.source: "qrc:/Main/assets/icons/star.svg"
                active: row.favorite
                tip: row.favorite ? "Remove from favorites" : "Add to favorites"
                onClicked: PresetManager.setFavorite(row.folder, row.name, !row.favorite)
            }
            HeaderButton {
                visible: rowHover.hovered
                text: "⋯"
                tip: "Preset options"
                onClicked: row.panel.openPresetMenu(row.scope, row.folder, row.name)
            }
        }

        // What follows the pointer while dragging (drop source); parented to
        // the panel so it paints above every row.
        Rectangle {
            id: ghost
            parent: row.panel
            property string scope: row.scope
            property string folder: row.folder
            property string name: row.name
            width: row.width
            height: row.height
            radius: 4
            z: 10
            visible: Drag.active
            color: Theme.highlight
            border.color: Theme.accent
            opacity: 0.9
            Drag.active: rowMouse.drag.active
            Drag.keys: ["photon-preset"]
            Drag.hotSpot.x: width / 2
            Drag.hotSpot.y: height / 2
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 10
                text: row.name
                color: Theme.foreground
                font: Theme.fontMedium
            }
        }
    }

    // One library group: unfiled presets, or a folder with its presets.
    component FolderGroup: ColumnLayout {
        id: fg
        property var panel
        property string scope: ""
        property var group: ({ folder: "", presets: [] })
        readonly property string folder: group.folder
        Layout.fillWidth: true
        spacing: 2
        FolderHeader {
            visible: fg.folder !== ""
            panel: fg.panel
            scope: fg.scope
            folder: fg.folder
            count: fg.group.presets.length
        }
        Repeater {
            model: (fg.folder === "" || !fg.panel.isCollapsed(fg.scope, fg.folder)) ? fg.group.presets : []
            delegate: PresetRow {
                panel: fg.panel
                scope: fg.scope
                folder: fg.folder
                name: modelData.name
                favorite: modelData.favorite
                indented: fg.folder !== ""
            }
        }
    }

    // ─── Layout ───────────────────────────────────────────────────────────────

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: root.viewTopPadding
        spacing: 0

        // Pinned Histogram at the top
        Histogram {
            id: headerHistogram
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            Layout.margins: 12
            histogramRed: root.viewport ? root.viewport.histogramRed : []
            histogramGreen: root.viewport ? root.viewport.histogramGreen : []
            histogramBlue: root.viewport ? root.viewport.histogramBlue : []
            histogramLuma: root.viewport ? root.viewport.histogramLuma : []
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: 12
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: "PRESETS"
                    color: Theme.mutedFg
                    font.pixelSize: Theme.fontSmall.pixelSize
                    font.bold: true
                    Layout.fillWidth: true
                }
                HeaderButton {
                    icon.source: "qrc:/Main/assets/icons/star.svg"
                    active: PresetManager.favoritesOnly
                    tip: PresetManager.favoritesOnly ? "Show all presets" : "Show favorites only"
                    onClicked: PresetManager.favoritesOnly = !PresetManager.favoritesOnly
                }
                HeaderButton {
                    icon.source: "qrc:/Main/assets/icons/folder.svg"
                    tip: "New folder"
                    onClicked: root.askText("New Folder", "Folder name:", "", function(name) {
                        if (!PresetManager.createFolder(root.scopeGlobal, name))
                            root.notify("A folder named “" + name + "” already exists", "error")
                    })
                }
                HeaderButton {
                    text: "⋯"
                    tip: "Import / export"
                    onClicked: moreMenu.popup()
                }
                HeaderButton {
                    text: "+"
                    tip: "Save current settings as a preset"
                    enabled: root.viewport !== null
                    onClicked: {
                        root.pendingPresetSettings = ({})
                        settingsSelectionDialog.openForSettings(root.viewport.currentSettings())
                    }
                }
            }

            T.ScrollView {
                id: scroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                T.ScrollBar.vertical: PhotonScrollBar {}

                ColumnLayout {
                    width: scroll.availableWidth
                    spacing: 2

                    // Favorites
                    SectionTitle {
                        panel: root
                        title: "FAVORITES"
                        visible: PresetManager.favorites.length > 0
                    }
                    Repeater {
                        model: PresetManager.favorites
                        delegate: PresetRow {
                            panel: root
                            scope: "global"
                            folder: modelData.folder
                            name: modelData.name
                            favorite: true
                            showFolder: true
                        }
                    }
                    Text {
                        visible: PresetManager.favoritesOnly && PresetManager.favorites.length === 0
                        Layout.fillWidth: true
                        Layout.topMargin: 12
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        text: "No favorites yet: star a preset to pin it here."
                        color: Theme.mutedFg
                        font: Theme.fontSmall
                    }

                    // This folder (project presets)
                    SectionTitle {
                        panel: root
                        title: "THIS FOLDER · " + PresetManager.projectName.toUpperCase()
                        dropScope: "project"
                        visible: PresetManager.projectAvailable && !PresetManager.favoritesOnly
                    }
                    Repeater {
                        model: PresetManager.projectAvailable && !PresetManager.favoritesOnly
                               ? PresetManager.projectLibrary : []
                        delegate: FolderGroup {
                            panel: root
                            scope: "project"
                            group: modelData
                        }
                    }
                    Text {
                        visible: PresetManager.projectAvailable && !PresetManager.favoritesOnly
                                 && PresetManager.projectLibrary.length <= 1
                                 && (PresetManager.projectLibrary.length === 0
                                     || PresetManager.projectLibrary[0].presets.length === 0)
                        Layout.fillWidth: true
                        Layout.leftMargin: 4
                        wrapMode: Text.WordWrap
                        text: "Presets saved to this folder stay with its photos."
                        color: Theme.mutedFg
                        font: Theme.fontSmall
                    }

                    // All presets
                    SectionTitle {
                        panel: root
                        title: "ALL PRESETS"
                        dropScope: "global"
                        visible: !PresetManager.favoritesOnly
                    }
                    Repeater {
                        model: !PresetManager.favoritesOnly ? PresetManager.globalLibrary : []
                        delegate: FolderGroup {
                            panel: root
                            scope: "global"
                            group: modelData
                        }
                    }
                    Text {
                        visible: !PresetManager.favoritesOnly && PresetManager.globalLibrary.length <= 1
                                 && (PresetManager.globalLibrary.length === 0
                                     || PresetManager.globalLibrary[0].presets.length === 0)
                        Layout.fillWidth: true
                        Layout.leftMargin: 4
                        wrapMode: Text.WordWrap
                        text: "No presets saved"
                        color: Theme.mutedFg
                        font: Theme.fontSmall
                    }
                }
            }
        }
    }

    // ─── Menus ────────────────────────────────────────────────────────────────

    StyledMenu {
        id: presetMenu
        StyledMenuItem {
            text: "Apply"
            onTriggered: root.applyPreset(root.menuScope, root.menuFolder, root.menuName)
        }
        StyledMenuItem {
            text: "Rename…"
            onTriggered: {
                var scope = root.menuScope, folder = root.menuFolder, name = root.menuName
                root.askText("Rename Preset", "Preset name:", name, function(newName) {
                    if (newName !== name && !PresetManager.renamePreset(scope, folder, name, newName))
                        root.notify("“" + newName + "” already exists", "error")
                })
            }
        }
        StyledMenu {
            id: moveMenu
            title: "Move to folder"
            StyledMenuItem {
                text: "No folder"
                enabled: root.menuFolder !== ""
                onTriggered: root.moveTo(root.menuScope, root.menuFolder, root.menuName, root.menuScope, "")
            }
            Instantiator {
                model: root.menuFolders
                delegate: StyledMenuItem {
                    required property var modelData
                    text: modelData
                    enabled: modelData !== root.menuFolder
                    onTriggered: root.moveTo(root.menuScope, root.menuFolder, root.menuName,
                                             root.menuScope, modelData)
                }
                onObjectAdded: (index, object) => moveMenu.insertItem(index + 1, object)
                onObjectRemoved: (index, object) => moveMenu.removeItem(object)
            }
            StyledSeparator {}
            StyledMenuItem {
                text: "New folder…"
                onTriggered: {
                    var scope = root.menuScope, folder = root.menuFolder, name = root.menuName
                    root.askText("New Folder", "Folder name:", "", function(newFolder) {
                        PresetManager.createFolder(scope, newFolder)
                        root.moveTo(scope, folder, name, scope, newFolder)
                    })
                }
            }
        }
        StyledMenuItem {
            text: root.menuScope === root.scopeProject ? "Copy to all presets" : "Copy to this folder"
            visible: root.menuScope === root.scopeProject || PresetManager.projectAvailable
            onTriggered: PresetManager.copyPreset(root.menuScope, root.menuFolder, root.menuName,
                                                  root.menuScope === root.scopeProject ? root.scopeGlobal : root.scopeProject,
                                                  root.menuFolder)
        }
        StyledMenuItem {
            text: root.menuScope === root.scopeProject ? "Move to all presets" : "Move to this folder"
            visible: root.menuScope === root.scopeProject || PresetManager.projectAvailable
            onTriggered: root.moveTo(root.menuScope, root.menuFolder, root.menuName,
                                     root.menuScope === root.scopeProject ? root.scopeGlobal : root.scopeProject,
                                     root.menuFolder)
        }
        StyledMenuItem {
            visible: root.menuScope === root.scopeGlobal
            text: PresetManager.isFavorite(root.menuFolder, root.menuName) ? "Remove from favorites" : "Add to favorites"
            onTriggered: PresetManager.setFavorite(root.menuFolder, root.menuName,
                                                   !PresetManager.isFavorite(root.menuFolder, root.menuName))
        }
        StyledMenuItem {
            text: "Export…"
            onTriggered: root.startExport("preset", root.menuScope, root.menuFolder, root.menuName)
        }
        StyledSeparator {}
        StyledMenuItem {
            text: "Delete…"
            onTriggered: {
                var scope = root.menuScope, folder = root.menuFolder, name = root.menuName
                root.confirm("Delete Preset", "Delete the preset “" + name + "”?", function() {
                    PresetManager.deletePreset(scope, folder, name)
                })
            }
        }
    }

    StyledMenu {
        id: folderMenu
        StyledMenuItem {
            text: "Rename folder…"
            onTriggered: {
                var scope = root.menuScope, folder = root.menuFolder
                root.askText("Rename Folder", "Folder name:", folder, function(newName) {
                    if (newName !== folder && !PresetManager.renameFolder(scope, folder, newName))
                        root.notify("A folder named “" + newName + "” already exists", "error")
                })
            }
        }
        StyledMenuItem {
            text: "Export folder…"
            onTriggered: root.startExport("folder", root.menuScope, root.menuFolder, "")
        }
        StyledSeparator {}
        StyledMenuItem {
            text: "Delete folder…"
            onTriggered: {
                var scope = root.menuScope, folder = root.menuFolder
                var count = PresetManager.presetCount(scope, folder)
                root.confirm("Delete Folder",
                             count > 0 ? "Delete the folder “" + folder + "” and its " + count
                                         + (count === 1 ? " preset?" : " presets?")
                                       : "Delete the empty folder “" + folder + "”?",
                             function() { PresetManager.deleteFolder(scope, folder) })
            }
        }
    }

    StyledMenu {
        id: moreMenu
        StyledMenuItem {
            text: "Import presets…"
            onTriggered: root.startImport(root.scopeGlobal)
        }
        StyledMenuItem {
            text: "Import into this folder…"
            visible: PresetManager.projectAvailable
            onTriggered: root.startImport(root.scopeProject)
        }
        StyledSeparator {}
        StyledMenuItem {
            text: "Export all presets…"
            onTriggered: root.startExport("folder", root.scopeGlobal, "", "")
        }
        StyledMenuItem {
            text: "Export favorites…"
            enabled: PresetManager.favorites.length > 0
            onTriggered: root.startExport("favorites", root.scopeGlobal, "", "")
        }
        StyledMenuItem {
            text: "Export this folder’s presets…"
            visible: PresetManager.projectAvailable
            onTriggered: root.startExport("folder", root.scopeProject, "", "")
        }
    }

    // ─── Dialogs ──────────────────────────────────────────────────────────────

    FileDialog {
        id: importDialog
        property string scope: "global"
        title: scope === "project" ? "Import Presets into This Folder" : "Import Presets"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Photon presets (*.json *.zip)"]
        onAccepted: {
            var files = []
            for (var i = 0; i < selectedFiles.length; ++i)
                files.push(selectedFiles[i].toString())
            var result = PresetManager.importPresets(files, importDialog.scope, "")
            root.notify(result.message, result.imported > 0 ? "info" : "error")
        }
    }

    FileDialog {
        id: exportDialog
        property string kind: "preset"
        property string scope: "global"
        property string folder: ""
        property string name: ""
        title: "Export Presets"
        fileMode: FileDialog.SaveFile
        onAccepted: {
            var file = selectedFile.toString()
            var ok = kind === "preset" ? PresetManager.exportPreset(scope, folder, name, file)
                   : kind === "favorites" ? PresetManager.exportFavorites(file)
                   : PresetManager.exportFolder(scope, folder, file)
            root.notify(ok ? "Presets exported" : "Nothing was exported", ok ? "info" : "error")
        }
    }

    MessageDialog {
        id: confirmDialog
        property var callback: null
        buttons: MessageDialog.Yes | MessageDialog.No
        onButtonClicked: (button, role) => {
            if (button === MessageDialog.Yes && confirmDialog.callback)
                confirmDialog.callback()
        }
    }

    T.Dialog {
        id: textDialog
        property string label: ""
        property var callback: null
        anchors.centerIn: T.Overlay.overlay
        modal: true
        standardButtons: T.Dialog.Ok | T.Dialog.Cancel
        ColumnLayout {
            spacing: 10
            Text { text: textDialog.label; color: Theme.foreground; font: Theme.fontRegular }
            T.TextField {
                id: textInput
                Layout.preferredWidth: 260
                color: Theme.foreground
                background: Rectangle { color: Theme.secondary; radius: 4; border.color: Theme.border }
                onAccepted: textDialog.accept()
            }
        }
        onAccepted: {
            var value = textInput.text.trim()
            if (value.length > 0 && textDialog.callback)
                textDialog.callback(value)
        }
    }

    SettingsSelectionDialog {
        id: settingsSelectionDialog
        dialogTitle: "Select Settings to Save"
        dialogDescription: "Choose which adjustments to include in this preset."
        confirmButtonText: "Continue"
        onSelectionAccepted: (filteredSettings, selectedKeys) => {
            root.pendingPresetSettings = filteredSettings
            saveScope.currentIndex = 0
            saveFolder.currentIndex = 0
            presetNameInput.text = ""
            saveDialog.open()
            presetNameInput.forceActiveFocus()
        }
    }

    T.Dialog {
        id: saveDialog
        title: "Save Preset"
        anchors.centerIn: T.Overlay.overlay
        modal: true
        standardButtons: T.Dialog.Save | T.Dialog.Cancel

        readonly property string scope: saveScope.currentIndex === 1 ? "project" : "global"
        readonly property string folder: saveFolder.currentIndex > 0 ? saveFolder.currentText : ""
        readonly property bool replaces: presetNameInput.text.trim().length > 0
                                         && PresetManager.presetExists(scope, folder, presetNameInput.text.trim())

        ColumnLayout {
            spacing: 10
            implicitWidth: 300
            Text { text: "Preset name"; color: Theme.mutedFg; font: Theme.fontSmall }
            T.TextField {
                id: presetNameInput
                Layout.fillWidth: true
                placeholderText: "Portrait Moody…"
                color: Theme.foreground
                background: Rectangle { color: Theme.secondary; radius: 4; border.color: Theme.border }
                onAccepted: saveDialog.accept()
            }
            Text { text: "Save to"; color: Theme.mutedFg; font: Theme.fontSmall }
            PhotonComboBox {
                id: saveScope
                Layout.fillWidth: true
                model: PresetManager.projectAvailable
                       ? ["All presets", "This folder (" + PresetManager.projectName + ")"]
                       : ["All presets"]
                onActivated: saveFolder.currentIndex = 0
            }
            Text { text: "Folder"; color: Theme.mutedFg; font: Theme.fontSmall }
            PhotonComboBox {
                id: saveFolder
                Layout.fillWidth: true
                // Re-read when the libraries change.
                model: {
                    PresetManager.globalLibrary
                    PresetManager.projectLibrary
                    return ["No folder"].concat(PresetManager.folders(saveDialog.scope))
                }
            }
            Text {
                visible: saveDialog.replaces
                text: "Replaces the existing preset with this name."
                color: Theme.mutedFg
                font: Theme.fontSmall
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        onAccepted: {
            var presetName = presetNameInput.text.trim()
            if (presetName.length > 0 && Object.keys(root.pendingPresetSettings).length > 0) {
                if (!PresetManager.savePreset(saveDialog.scope, saveDialog.folder, presetName, root.pendingPresetSettings))
                    root.notify("The preset could not be saved", "error")
            }
            root.pendingPresetSettings = ({})
        }

        onRejected: root.pendingPresetSettings = ({})
    }
}
