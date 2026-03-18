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

    ColumnLayout {
        id: chatPage
        anchors.fill: parent
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
        // SYSTEM PROMPT
        // ════════════════════════════════════════
        Kirigami.FormLayout {
            Component.onCompleted: { for (var i = 0; i < children.length; i++) { if (children[i].hasOwnProperty("columns")) { children[i].anchors.horizontalCenter = undefined; children[i].anchors.left = left; children[i].anchors.leftMargin = Qt.binding(function() { return Kirigami.Units.smallSpacing; }); } } }
            Layout.fillWidth: true

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("System Prompt")
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Mode:")
                spacing: Kirigami.Units.largeSpacing

                ButtonGroup { id: promptModeGroup }

                RadioButton {
                    text: i18n("Default")
                    ButtonGroup.group: promptModeGroup
                    checked: JarvisBackend.systemPromptMode === "default"
                    onClicked: JarvisBackend.setSystemPromptMode("default")
                }
                RadioButton {
                    text: i18n("None")
                    ButtonGroup.group: promptModeGroup
                    checked: JarvisBackend.systemPromptMode === "none"
                    onClicked: JarvisBackend.setSystemPromptMode("none")
                }
                RadioButton {
                    text: i18n("Custom")
                    ButtonGroup.group: promptModeGroup
                    checked: JarvisBackend.systemPromptMode === "custom"
                    onClicked: JarvisBackend.setSystemPromptMode("custom")
                }
            }

            Label {
                visible: JarvisBackend.systemPromptMode === "default"
                text: i18n("Uses the built-in J.A.R.V.I.S. personality — polite, witty, British humor.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }

            Label {
                visible: JarvisBackend.systemPromptMode === "none"
                text: i18n("No personality prompt is sent. The model uses its own default behavior.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }

        }

        // TextArea outside FormLayout so Layout.fillHeight works
        ScrollView {
            id: promptScrollView
            property bool isCustom: JarvisBackend.systemPromptMode === "custom"
            visible: isCustom
            Layout.fillWidth: true
            Layout.fillHeight: isCustom
            Layout.preferredHeight: isCustom ? -1 : 0
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing

            TextArea {
                id: personalityField
                text: JarvisBackend.personalityPrompt
                placeholderText: i18n("You are a helpful assistant...")
                wrapMode: TextEdit.Wrap
            }
        }

        Button {
            visible: JarvisBackend.systemPromptMode === "custom"
            text: i18n("Save Prompt")
            icon.name: "document-save"
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.bottomMargin: Kirigami.Units.largeSpacing
            onClicked: JarvisBackend.setPersonalityPrompt(personalityField.text)
        }

        // Push content up when textarea is hidden
        Item {
            visible: JarvisBackend.systemPromptMode !== "custom"
            Layout.fillHeight: true
        }
    }
}
