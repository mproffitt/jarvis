import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.kde.kirigami 2.20 as Kirigami
import org.kde.plasma.jarvis 1.0

Item {
    id: configRootItem
    property string title: i18n("General")
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0

    ScrollView {
        id: configRoot
        anchors.fill: parent
        contentWidth: availableWidth

    ColumnLayout {
        id: configPage
        width: configRoot.availableWidth
        spacing: 0

        // ════════════════════════════════════════
        // DOWNLOAD PROGRESS — always visible at top when downloading
        // ════════════════════════════════════════
        Kirigami.InlineMessage {
            id: downloadBanner
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: JarvisBackend.downloading
            type: Kirigami.MessageType.Information
            text: JarvisBackend.downloadStatus

            actions: [
                Kirigami.Action {
                    icon.name: "dialog-cancel"
                    text: i18n("Cancel")
                    onTriggered: JarvisBackend.cancelDownload()
                }
            ]
        }

        ProgressBar {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            visible: JarvisBackend.downloading
            from: 0; to: 1.0
            value: JarvisBackend.downloadProgress
        }

        // ════════════════════════════════════════
        // LLM PROVIDER
        // ════════════════════════════════════════
        Kirigami.FormLayout {
            Layout.fillWidth: true

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("LLM Provider")
            }

            ComboBox {
                id: providerCombo
                Kirigami.FormData.label: i18n("Provider:")
                model: [
                    { value: "llamacpp", text: "llama.cpp (local)" },
                    { value: "ollama",   text: "Ollama (local)" }
                ]
                textRole: "text"
                valueRole: "value"
                currentIndex: {
                    var p = JarvisBackend.llmProvider
                    for (var i = 0; i < model.length; i++) {
                        if (model[i].value === p) return i
                    }
                    return 0
                }
                onActivated: JarvisBackend.setLlmProvider(currentValue)
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Server URL:")
                spacing: Kirigami.Units.smallSpacing
                TextField {
                    id: serverUrlField
                    text: JarvisBackend.llmServerUrl
                    placeholderText: {
                        var p = JarvisBackend.llmProvider
                        if (p === "ollama") return "http://127.0.0.1:11434"
                        return "http://127.0.0.1:8080"
                    }
                    Layout.fillWidth: true
                    onAccepted: JarvisBackend.setLlmServerUrl(text)
                }
                Button {
                    text: i18n("Apply")
                    icon.name: "dialog-ok-apply"
                    onClicked: JarvisBackend.setLlmServerUrl(serverUrlField.text)
                }
            }

            // Model selector for cloud providers (dropdown)
            ComboBox {
                id: cloudModelCombo
                Kirigami.FormData.label: i18n("Model:")
                visible: JarvisBackend.llmProvider !== "llamacpp" && JarvisBackend.llmProvider !== "ollama"
                Layout.fillWidth: true

                property var choices: JarvisBackend.cloudModelChoices
                property bool customMode: false

                model: {
                    var items = []
                    var choices = JarvisBackend.cloudModelChoices
                    for (var i = 0; i < choices.length; i++)
                        items.push(choices[i].name + "  (" + choices[i].id + ")")
                    items.push(i18n("Custom..."))
                    return items
                }

                currentIndex: {
                    var id = JarvisBackend.llmModelId
                    var ch = JarvisBackend.cloudModelChoices
                    for (var i = 0; i < ch.length; i++) {
                        if (ch[i].id === id) return i
                    }
                    // Current model not in list — show Custom
                    return ch.length
                }

                onActivated: function(index) {
                    var ch = JarvisBackend.cloudModelChoices
                    if (index >= 0 && index < ch.length) {
                        customMode = false
                        JarvisBackend.setLlmModelId(ch[index].id)
                    } else {
                        customMode = true
                    }
                }
            }

            // Custom model text field (shown when "Custom..." selected or model not in list)
            RowLayout {
                Kirigami.FormData.label: i18n("Custom model:")
                visible: JarvisBackend.llmProvider !== "llamacpp" && JarvisBackend.llmProvider !== "ollama"
                         && (cloudModelCombo.customMode || cloudModelCombo.currentIndex >= JarvisBackend.cloudModelChoices.length)
                spacing: Kirigami.Units.smallSpacing
                TextField {
                    id: customModelField
                    text: JarvisBackend.llmModelId
                    placeholderText: i18n("Enter model ID...")
                    Layout.fillWidth: true
                    onAccepted: JarvisBackend.setLlmModelId(text)
                }
                Button {
                    text: i18n("Apply")
                    icon.name: "dialog-ok-apply"
                    onClicked: JarvisBackend.setLlmModelId(customModelField.text)
                }
            }

            // Model ID for Ollama (text field — models listed separately below)
            RowLayout {
                Kirigami.FormData.label: i18n("Model:")
                visible: JarvisBackend.llmProvider === "ollama"
                spacing: Kirigami.Units.smallSpacing
                TextField {
                    id: modelIdField
                    text: JarvisBackend.llmModelId
                    placeholderText: "llama3.2"
                    Layout.fillWidth: true
                    onAccepted: JarvisBackend.setLlmModelId(text)
                }
                Button {
                    text: i18n("Apply")
                    icon.name: "dialog-ok-apply"
                    onClicked: JarvisBackend.setLlmModelId(modelIdField.text)
                }
            }

            Label {
                visible: JarvisBackend.llmProvider === "openai" || JarvisBackend.llmProvider === "gemini" || JarvisBackend.llmProvider === "claude"
                text: i18n("Cloud provider selected. API key configuration coming in a future update.")
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Status:")
                spacing: Kirigami.Units.smallSpacing
                Kirigami.Icon {
                    source: JarvisBackend.connected ? "network-connect" : "network-disconnect"
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                }
                Label {
                    text: JarvisBackend.connected ? i18n("Connected") : i18n("Disconnected")
                    color: JarvisBackend.connected ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor
                    font.bold: true
                }
            }

            Label {
                Kirigami.FormData.label: i18n("Active model:")
                visible: JarvisBackend.llmProvider === "llamacpp"
                text: JarvisBackend.currentModelName || i18n("None selected")
                font.bold: true
            }
        }

        // ════════════════════════════════════════
        // LLM MODELS (llama.cpp GGUF models)
        // ════════════════════════════════════════
        Kirigami.FormLayout {
            Layout.fillWidth: true
            visible: JarvisBackend.llmProvider === "llamacpp"

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("GGUF Models (llama.cpp)")
            }

            Label {
                text: i18n("Download GGUF models for your local llama.cpp server. Smaller models are faster but less capable.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }
        }

        Repeater {
            model: JarvisBackend.llmProvider === "llamacpp" ? JarvisBackend.availableLlmModels : []
            delegate: Kirigami.AbstractCard {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.smallSpacing
                Layout.rightMargin: Kirigami.Units.smallSpacing
                contentItem: RowLayout {
                    spacing: Kirigami.Units.largeSpacing
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        RowLayout {
                            spacing: Kirigami.Units.smallSpacing
                            Label {
                                text: modelData.name
                                font.bold: true
                            }
                            Label {
                                text: modelData.size
                                color: Kirigami.Theme.disabledTextColor
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                            }
                            Kirigami.Icon {
                                visible: modelData.active
                                source: "emblem-default"
                                implicitWidth: Kirigami.Units.iconSizes.small
                                implicitHeight: Kirigami.Units.iconSizes.small
                            }
                        }
                        Label {
                            text: modelData.desc
                            color: Kirigami.Theme.disabledTextColor
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                    }
                    Button {
                        text: modelData.active ? i18n("Active") : (modelData.downloaded ? i18n("Activate") : i18n("Download"))
                        icon.name: modelData.active ? "checkmark" : (modelData.downloaded ? "media-playback-start" : "download")
                        enabled: !modelData.active && !JarvisBackend.downloading
                        highlighted: modelData.active
                        onClicked: {
                            if (modelData.downloaded) JarvisBackend.setActiveLlmModel(modelData.id)
                            else JarvisBackend.downloadLlmModel(modelData.id)
                        }
                    }
                }
            }
        }

        Button {
            text: i18n("Fetch More Models")
            icon.name: "list-add"
            visible: JarvisBackend.llmProvider === "llamacpp"
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: Kirigami.Units.smallSpacing
            onClicked: JarvisBackend.fetchMoreModels()
        }

        // ════════════════════════════════════════
        // OLLAMA MODELS
        // ════════════════════════════════════════
        Kirigami.FormLayout {
            Layout.fillWidth: true
            visible: JarvisBackend.llmProvider === "ollama"

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Ollama Models")
            }

            Label {
                text: i18n("Models installed in Ollama. Select one to use, or type a model name in the field above.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }
        }

        Repeater {
            model: JarvisBackend.llmProvider === "ollama" ? JarvisBackend.availableLlmModels : []
            delegate: Kirigami.AbstractCard {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.smallSpacing
                Layout.rightMargin: Kirigami.Units.smallSpacing
                contentItem: RowLayout {
                    spacing: Kirigami.Units.largeSpacing
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        RowLayout {
                            spacing: Kirigami.Units.smallSpacing
                            Label {
                                text: modelData.name
                                font.bold: true
                            }
                            Label {
                                text: modelData.size
                                color: Kirigami.Theme.disabledTextColor
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                            }
                            Kirigami.Icon {
                                visible: modelData.active
                                source: "emblem-default"
                                implicitWidth: Kirigami.Units.iconSizes.small
                                implicitHeight: Kirigami.Units.iconSizes.small
                            }
                        }
                    }
                    Button {
                        text: modelData.active ? i18n("Active") : i18n("Select")
                        icon.name: modelData.active ? "checkmark" : "media-playback-start"
                        enabled: !modelData.active
                        highlighted: modelData.active
                        onClicked: {
                            JarvisBackend.setLlmModelId(modelData.id)
                            JarvisBackend.refreshOllamaModels()
                        }
                    }
                }
            }
        }

        Button {
            text: i18n("Refresh Models")
            icon.name: "view-refresh"
            visible: JarvisBackend.llmProvider === "ollama"
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: Kirigami.Units.smallSpacing
            onClicked: JarvisBackend.refreshOllamaModels()
        }

        // ════════════════════════════════════════
        // TTS VOICES
        // ════════════════════════════════════════
        Kirigami.FormLayout {
            Layout.fillWidth: true

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("TTS Voices (Piper)")
            }

            Label {
                text: i18n("Choose a voice for speech synthesis. Download a voice, then press Play to preview it.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }

            Label {
                Kirigami.FormData.label: i18n("Active voice:")
                text: JarvisBackend.currentVoiceName || i18n("None")
                font.bold: true
            }
        }

        Repeater {
            model: JarvisBackend.availableTtsVoices
            delegate: Kirigami.AbstractCard {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.smallSpacing
                Layout.rightMargin: Kirigami.Units.smallSpacing
                contentItem: RowLayout {
                    spacing: Kirigami.Units.largeSpacing
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        RowLayout {
                            spacing: Kirigami.Units.smallSpacing
                            Label {
                                text: modelData.name
                                font.bold: true
                            }
                            Label {
                                text: modelData.lang
                                color: Kirigami.Theme.disabledTextColor
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                            }
                            Kirigami.Icon {
                                visible: modelData.active
                                source: "emblem-default"
                                implicitWidth: Kirigami.Units.iconSizes.small
                                implicitHeight: Kirigami.Units.iconSizes.small
                            }
                        }
                        Label {
                            text: modelData.desc
                            color: Kirigami.Theme.disabledTextColor
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                    }
                    Button {
                        visible: modelData.downloaded
                        text: i18n("Play")
                        icon.name: "media-playback-start"
                        flat: true
                        ToolTip.text: i18n("Preview this voice")
                        ToolTip.visible: hovered
                        onClicked: JarvisBackend.testVoice(modelData.id)
                    }
                    Button {
                        text: modelData.active ? i18n("Active") : (modelData.downloaded ? i18n("Activate") : i18n("Download"))
                        icon.name: modelData.active ? "checkmark" : (modelData.downloaded ? "media-playback-start" : "download")
                        enabled: !modelData.active && !JarvisBackend.downloading
                        highlighted: modelData.active
                        onClicked: {
                            if (modelData.downloaded) JarvisBackend.setActiveTtsVoice(modelData.id)
                            else JarvisBackend.downloadTtsVoice(modelData.id)
                        }
                    }
                }
            }
        }

        Button {
            text: i18n("Fetch More Voices")
            icon.name: "list-add"
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: Kirigami.Units.smallSpacing
            onClicked: JarvisBackend.fetchMoreVoices()
        }

        // ════════════════════════════════════════
        // VOICE SYNTHESIS SETTINGS
        // ════════════════════════════════════════
        Kirigami.FormLayout {
            Layout.fillWidth: true

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Voice Synthesis Settings")
            }

            Slider {
                id: rateSlider
                Kirigami.FormData.label: i18n("Speech rate: %1", value.toFixed(2))
                from: -1.0; to: 1.0; stepSize: 0.05
                value: 0.05
                onMoved: JarvisBackend.setTtsRate(value)
            }

            Slider {
                id: pitchSlider
                Kirigami.FormData.label: i18n("Speech pitch: %1", value.toFixed(2))
                from: -1.0; to: 1.0; stepSize: 0.05
                value: -0.1
                onMoved: JarvisBackend.setTtsPitch(value)
            }

            Slider {
                id: volumeSlider
                Kirigami.FormData.label: i18n("Volume: %1%", (value * 100).toFixed(0))
                from: 0.0; to: 1.0; stepSize: 0.05
                value: 0.85
                onMoved: JarvisBackend.setTtsVolume(value)
            }

            Button {
                text: i18n("Test Current Voice")
                icon.name: "media-playback-start"
                onClicked: JarvisBackend.testVoice(JarvisBackend.currentVoiceName)
            }
        }

        // ════════════════════════════════════════
        // WAKE WORD & AUDIO
        // ════════════════════════════════════════
        Kirigami.FormLayout {
            Layout.fillWidth: true

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Wake Word & Audio")
            }

            CheckBox {
                Kirigami.FormData.label: i18n("Auto-start wake word detection:")
                checked: JarvisBackend.autoStartWakeWord
                onToggled: JarvisBackend.setAutoStartWakeWord(checked)
            }

            Label {
                text: i18n("Say \"Jarvis\" to activate voice commands without clicking.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }

            Slider {
                id: voiceCmdSlider
                Kirigami.FormData.label: i18n("Max voice command length: %1 seconds", value.toFixed(0))
                from: 3; to: 30; stepSize: 1
                value: JarvisBackend.voiceCmdMaxSeconds
                onMoved: JarvisBackend.setVoiceCmdMaxSeconds(value)
            }
        }

        // ════════════════════════════════════════
        // CHAT SETTINGS
        // ════════════════════════════════════════
        Kirigami.FormLayout {
            Layout.fillWidth: true

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Chat Settings")
            }

            Slider {
                id: historySlider
                Kirigami.FormData.label: i18n("Conversation memory: %1 message pairs", value.toFixed(0))
                from: 5; to: 100; stepSize: 5
                value: JarvisBackend.maxHistoryPairs
                onMoved: JarvisBackend.setMaxHistoryPairs(value)
            }

            Label {
                text: i18n("More pairs = better context memory but slower responses and more RAM usage.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }
        }

        // ════════════════════════════════════════
        // PERSONALITY
        // ════════════════════════════════════════
        Kirigami.FormLayout {
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
