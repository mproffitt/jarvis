import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt.labs.platform 1.1 as Labs
import org.kde.kirigami 2.20 as Kirigami
import org.kde.plasma.jarvis 1.0

Item {
    id: configChatRoot
    property string title: i18n("Chat")
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    clip: true

    ScrollView {
        id: chatScrollView
        anchors.fill: parent
        contentWidth: availableWidth

    ColumnLayout {
        id: chatPage
        width: chatScrollView.availableWidth
        spacing: 0

        // ════════════════════════════════════════
        // CHAT SETTINGS
        // ════════════════════════════════════════
        Kirigami.FormLayout {
            Component.onCompleted: { for (var i = 0; i < children.length; i++) { if (children[i].hasOwnProperty("columns")) { children[i].anchors.horizontalCenter = undefined; children[i].anchors.left = left; children[i].anchors.leftMargin = Qt.binding(function() { return Kirigami.Units.smallSpacing; }); } } }
            Layout.fillWidth: true

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Chat Settings")
            }

        }

        // Slider outside FormLayout for full width
        Label {
            text: i18n("Conversation memory: %1 message pairs").arg(historySlider.value.toFixed(0))
            Layout.leftMargin: Kirigami.Units.largeSpacing
        }
        Slider {
            id: historySlider
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            from: 5; to: 100; stepSize: 5
            value: JarvisBackend.maxHistoryPairs
            onMoved: JarvisBackend.setMaxHistoryPairs(value)
        }
        Label {
            text: i18n("More pairs = better context memory but slower responses and more RAM usage.")
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            color: Kirigami.Theme.disabledTextColor
            font.pointSize: Kirigami.Theme.smallFont.pointSize
        }

        // Rest of chat settings in a new FormLayout
        Kirigami.FormLayout {
            Layout.fillWidth: true
            Component.onCompleted: { for (var i = 0; i < children.length; i++) { if (children[i].hasOwnProperty("columns")) { children[i].anchors.horizontalCenter = undefined; children[i].anchors.left = left; children[i].anchors.leftMargin = Qt.binding(function() { return Kirigami.Units.smallSpacing; }); } } }

            RowLayout {
                Kirigami.FormData.label: i18n("History:")
                spacing: Kirigami.Units.smallSpacing
                Button {
                    text: i18n("Export")
                    icon.name: "document-save"
                    onClicked: exportDialog.open()
                }
                Button {
                    text: i18n("Import")
                    icon.name: "document-open"
                    onClicked: importDialog.open()
                }
            }
        }

        // File dialogs for export/import (declared at page level)
        Loader {
            id: exportDialogLoader
            active: false
            sourceComponent: Component {
                Labs.FileDialog {
                    id: exportDlg
                    title: i18n("Export Chat History")
                    fileMode: Labs.FileDialog.SaveFile
                    nameFilters: ["JSON files (*.json)"]
                    currentFile: "file:///tmp/jarvis_history.json"
                    onAccepted: { JarvisBackend.exportHistory(selectedFile.toString().replace("file://", "")); exportDialogLoader.active = false }
                    onRejected: exportDialogLoader.active = false
                    Component.onCompleted: open()
                }
            }
        }
        Loader {
            id: importDialogLoader
            active: false
            sourceComponent: Component {
                Labs.FileDialog {
                    id: importDlg
                    title: i18n("Import Chat History")
                    fileMode: Labs.FileDialog.OpenFile
                    nameFilters: ["JSON files (*.json)"]
                    onAccepted: { JarvisBackend.importHistory(selectedFile.toString().replace("file://", "")); importDialogLoader.active = false }
                    onRejected: importDialogLoader.active = false
                    Component.onCompleted: open()
                }
            }
        }

        QtObject {
            id: exportDialog
            function open() { exportDialogLoader.active = true }
        }
        QtObject {
            id: importDialog
            function open() { importDialogLoader.active = true }
        }

        // ════════════════════════════════════════
        // PERSONALITY
        // ════════════════════════════════════════
        Kirigami.FormLayout {
            Component.onCompleted: { for (var i = 0; i < children.length; i++) { if (children[i].hasOwnProperty("columns")) { children[i].anchors.horizontalCenter = undefined; children[i].anchors.left = left; children[i].anchors.leftMargin = Qt.binding(function() { return Kirigami.Units.smallSpacing; }); } } }
            Layout.fillWidth: true

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("AI Personality")
            }

            Label {
                text: i18n("Customize the system prompt to change how J.A.R.V.I.S. behaves. Leave empty for the default J.A.R.V.I.S. personality.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }

            TextArea {
                id: personalityField
                Kirigami.FormData.label: i18n("System prompt:")
                text: JarvisBackend.personalityPrompt
                placeholderText: i18n("Default: J.A.R.V.I.S. from Iron Man — polite, witty, British humor...")
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                wrapMode: TextEdit.Wrap
            }

            Button {
                text: i18n("Save Personality")
                icon.name: "document-save"
                onClicked: JarvisBackend.setPersonalityPrompt(personalityField.text)
            }
        }

        // Bottom spacer
        Item { Layout.preferredHeight: Kirigami.Units.largeSpacing }
    }
    }
}
