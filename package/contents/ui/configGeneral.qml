import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt.labs.platform 1.1 as Labs
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
                    { value: "ollama",   text: "Ollama (local)" },
                    { value: "openai",   text: "OpenAI (ChatGPT)" },
                    { value: "gemini",   text: "Google Gemini" },
                    { value: "claude",   text: "Anthropic (Claude)" }
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
                        if (p === "openai") return "https://api.openai.com"
                        if (p === "gemini") return "https://generativelanguage.googleapis.com/v1beta/openai"
                        if (p === "claude") return "https://api.anthropic.com"
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

            // OpenAI API Key
            RowLayout {
                Kirigami.FormData.label: i18n("OpenAI API Key:")
                visible: JarvisBackend.llmProvider === "openai"
                spacing: Kirigami.Units.smallSpacing
                TextField {
                    id: openaiKeyField
                    text: JarvisBackend.openaiApiKey
                    placeholderText: i18n("sk-...")
                    echoMode: TextInput.Password
                    Layout.fillWidth: true
                    onAccepted: JarvisBackend.setOpenaiApiKey(text)
                }
                Button {
                    text: i18n("Save")
                    icon.name: "dialog-ok-apply"
                    onClicked: JarvisBackend.setOpenaiApiKey(openaiKeyField.text)
                }
            }

            // Gemini API Key
            RowLayout {
                Kirigami.FormData.label: i18n("Gemini API Key:")
                visible: JarvisBackend.llmProvider === "gemini"
                spacing: Kirigami.Units.smallSpacing
                TextField {
                    id: geminiKeyField
                    text: JarvisBackend.geminiApiKey
                    placeholderText: i18n("AIza... (optional if logged in)")
                    echoMode: TextInput.Password
                    Layout.fillWidth: true
                    onAccepted: JarvisBackend.setGeminiApiKey(text)
                }
                Button {
                    text: i18n("Save")
                    icon.name: "dialog-ok-apply"
                    onClicked: JarvisBackend.setGeminiApiKey(geminiKeyField.text)
                }
            }

            // Gemini OAuth login
            RowLayout {
                Kirigami.FormData.label: i18n("Google Account:")
                visible: JarvisBackend.llmProvider === "gemini"
                spacing: Kirigami.Units.smallSpacing
                Kirigami.Icon {
                    visible: JarvisBackend.oauthLoggedIn
                    source: "emblem-default"
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                }
                Label {
                    visible: JarvisBackend.oauthLoggedIn
                    text: i18n("Logged in")
                    color: Kirigami.Theme.positiveTextColor
                }
                Button {
                    text: JarvisBackend.oauthLoggedIn ? i18n("Re-login") : i18n("Login with Google")
                    icon.name: "go-next"
                    onClicked: JarvisBackend.oauthLogin("gemini")
                }
                Button {
                    text: i18n("Logout")
                    icon.name: "system-log-out"
                    visible: JarvisBackend.oauthLoggedIn
                    flat: true
                    onClicked: JarvisBackend.oauthLogout("gemini")
                }
            }

            // Claude API Key
            RowLayout {
                Kirigami.FormData.label: i18n("Claude API Key:")
                visible: JarvisBackend.llmProvider === "claude"
                spacing: Kirigami.Units.smallSpacing
                TextField {
                    id: claudeKeyField
                    text: JarvisBackend.claudeApiKey
                    placeholderText: i18n("sk-ant-... (optional if logged in)")
                    echoMode: TextInput.Password
                    Layout.fillWidth: true
                    onAccepted: JarvisBackend.setClaudeApiKey(text)
                }
                Button {
                    text: i18n("Save")
                    icon.name: "dialog-ok-apply"
                    onClicked: JarvisBackend.setClaudeApiKey(claudeKeyField.text)
                }
            }

            // Claude OAuth login
            RowLayout {
                Kirigami.FormData.label: i18n("Claude Account:")
                visible: JarvisBackend.llmProvider === "claude" && !JarvisBackend.awaitingClaudeCode
                spacing: Kirigami.Units.smallSpacing
                Kirigami.Icon {
                    visible: JarvisBackend.oauthLoggedIn
                    source: "emblem-default"
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                }
                Label {
                    visible: JarvisBackend.oauthLoggedIn
                    text: i18n("Logged in")
                    color: Kirigami.Theme.positiveTextColor
                }
                Button {
                    text: JarvisBackend.oauthLoggedIn ? i18n("Re-login") : i18n("Login with Claude")
                    icon.name: "go-next"
                    onClicked: JarvisBackend.oauthLogin("claude")
                }
                Button {
                    text: i18n("Logout")
                    icon.name: "system-log-out"
                    visible: JarvisBackend.oauthLoggedIn
                    flat: true
                    onClicked: JarvisBackend.oauthLogout("claude")
                }
            }

            // Claude OAuth code paste field
            ColumnLayout {
                visible: JarvisBackend.llmProvider === "claude" && JarvisBackend.awaitingClaudeCode
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Label {
                    text: i18n("Authorize in your browser, then paste the code below:")
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }

                RowLayout {
                    Kirigami.FormData.label: i18n("Auth Code:")
                    spacing: Kirigami.Units.smallSpacing
                    TextField {
                        id: claudeCodeField
                        placeholderText: i18n("Paste authorization code here...")
                        Layout.fillWidth: true
                        onAccepted: {
                            if (text.length > 0) {
                                JarvisBackend.completeClaudeLogin(text)
                                text = ""
                            }
                        }
                    }
                    Button {
                        text: i18n("Submit")
                        icon.name: "dialog-ok-apply"
                        enabled: claudeCodeField.text.length > 0
                        onClicked: {
                            JarvisBackend.completeClaudeLogin(claudeCodeField.text)
                            claudeCodeField.text = ""
                        }
                    }
                    Button {
                        text: i18n("Cancel")
                        icon.name: "dialog-cancel"
                        flat: true
                        onClicked: JarvisBackend.cancelOAuthLogin()
                    }
                }
            }

            Label {
                visible: JarvisBackend.llmProvider === "openai"
                text: i18n("API key stored in KDE Wallet. Falls back to OPENAI_API_KEY environment variable.")
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            Label {
                visible: JarvisBackend.llmProvider === "gemini"
                text: i18n("Login with your Google account to use your Gemini subscription, or enter an API key. Tokens refresh automatically. Falls back to GEMINI_API_KEY env var.")
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            Label {
                visible: JarvisBackend.llmProvider === "claude"
                text: i18n("Login with your Claude account to use your subscription, or enter an API key. Tokens refresh automatically. Falls back to ANTHROPIC_API_KEY env var.")
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                wrapMode: Text.Wrap
                Layout.fillWidth: true
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

            // Smart routing
            CheckBox {
                Kirigami.FormData.label: i18n("Smart model routing:")
                visible: JarvisBackend.llmProvider !== "llamacpp"
                checked: JarvisBackend.smartRouting
                onToggled: JarvisBackend.setSmartRouting(checked)
            }

            ComboBox {
                id: fastModelCombo
                Kirigami.FormData.label: i18n("Fast model:")
                visible: JarvisBackend.smartRouting && JarvisBackend.llmProvider !== "llamacpp"
                Layout.fillWidth: true

                property var srcChoices: JarvisBackend.llmProvider === "ollama"
                    ? JarvisBackend.availableLlmModels : JarvisBackend.cloudModelChoices

                model: {
                    var items = []
                    var ch = srcChoices
                    for (var i = 0; i < ch.length; i++)
                        items.push(ch[i].name + (ch[i].id !== ch[i].name ? "  (" + ch[i].id + ")" : ""))
                    return items
                }
                currentIndex: {
                    var id = JarvisBackend.fastModelId
                    var ch = srcChoices
                    for (var i = 0; i < ch.length; i++)
                        if (ch[i].id === id) return i
                    return -1
                }
                onActivated: function(index) {
                    var ch = srcChoices
                    if (index >= 0 && index < ch.length)
                        JarvisBackend.setFastModelId(ch[index].id)
                }
            }

            Label {
                visible: JarvisBackend.smartRouting && JarvisBackend.llmProvider !== "llamacpp"
                text: i18n("Simple queries use the fast model, complex queries use the main model.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }

            // Model selector for Ollama (dropdown populated from installed models)
            RowLayout {
                Kirigami.FormData.label: i18n("Model:")
                visible: JarvisBackend.llmProvider === "ollama"
                spacing: Kirigami.Units.smallSpacing
                ComboBox {
                    id: ollamaModelCombo
                    Layout.fillWidth: true
                    property var choices: JarvisBackend.availableLlmModels
                    model: {
                        var items = []
                        var ch = JarvisBackend.availableLlmModels
                        for (var i = 0; i < ch.length; i++)
                            items.push(ch[i].name + "  (" + ch[i].size + ")")
                        return items
                    }
                    currentIndex: {
                        var id = JarvisBackend.llmModelId
                        var ch = JarvisBackend.availableLlmModels
                        for (var i = 0; i < ch.length; i++)
                            if (ch[i].id === id) return i
                        return -1
                    }
                    onActivated: function(index) {
                        var ch = JarvisBackend.availableLlmModels
                        if (index >= 0 && index < ch.length)
                            JarvisBackend.setLlmModelId(ch[index].id)
                    }
                }
                Button {
                    icon.name: "view-refresh"
                    ToolTip.text: i18n("Refresh model list")
                    ToolTip.visible: hovered
                    onClicked: JarvisBackend.refreshOllamaModels()
                }
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

            // llama.cpp model dropdown (downloaded models only)
            ComboBox {
                Kirigami.FormData.label: i18n("Active model:")
                visible: JarvisBackend.llmProvider === "llamacpp"
                Layout.fillWidth: true
                model: {
                    var items = []
                    var all = JarvisBackend.availableLlmModels
                    for (var i = 0; i < all.length; i++)
                        if (all[i].downloaded)
                            items.push(all[i].name + "  (" + all[i].size + ")")
                    if (items.length === 0) items.push(i18n("No models downloaded"))
                    return items
                }
                currentIndex: {
                    var name = JarvisBackend.currentModelName
                    var all = JarvisBackend.availableLlmModels
                    var idx = 0
                    for (var i = 0; i < all.length; i++) {
                        if (!all[i].downloaded) continue
                        if (all[i].id === name || all[i].name.indexOf(name) >= 0) return idx
                        idx++
                    }
                    return 0
                }
                onActivated: function(index) {
                    var all = JarvisBackend.availableLlmModels
                    var idx = 0
                    for (var i = 0; i < all.length; i++) {
                        if (!all[i].downloaded) continue
                        if (idx === index) {
                            JarvisBackend.setActiveLlmModel(all[i].id)
                            return
                        }
                        idx++
                    }
                }
            }
        }

        // ════════════════════════════════════════
        // LLM MODELS (llama.cpp GGUF download)
        // ════════════════════════════════════════
        Kirigami.FormLayout {
            Layout.fillWidth: true
            visible: JarvisBackend.llmProvider === "llamacpp"

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Download GGUF Models")
            }

            Label {
                text: i18n("Download models for llama.cpp. Smaller = faster, larger = smarter.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }
        }

        Repeater {
            model: JarvisBackend.llmProvider === "llamacpp" ? JarvisBackend.availableLlmModels : []
            delegate: Kirigami.AbstractCard {
                visible: !modelData.downloaded
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
                        text: i18n("Download")
                        icon.name: "download"
                        enabled: !JarvisBackend.downloading
                        onClicked: JarvisBackend.downloadLlmModel(modelData.id)
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

            RowLayout {
                Kirigami.FormData.label: i18n("Wake word:")
                spacing: Kirigami.Units.smallSpacing
                TextField {
                    id: wakeWordField
                    text: JarvisBackend.wakeWord
                    placeholderText: i18n("jarvis")
                    Layout.fillWidth: true
                    onAccepted: JarvisBackend.setWakeWord(text)
                }
                Button {
                    text: i18n("Apply")
                    icon.name: "dialog-ok-apply"
                    onClicked: JarvisBackend.setWakeWord(wakeWordField.text)
                }
            }

            ComboBox {
                Kirigami.FormData.label: i18n("Whisper model:")
                model: [
                    { value: "tiny",  text: "Tiny (75MB — fastest, least accurate)" },
                    { value: "base",  text: "Base (142MB — good balance)" },
                    { value: "small", text: "Small (466MB — best accuracy)" }
                ]
                textRole: "text"
                valueRole: "value"
                currentIndex: {
                    var m = JarvisBackend.whisperModel
                    for (var i = 0; i < model.length; i++)
                        if (model[i].value === m) return i
                    return 0
                }
                onActivated: JarvisBackend.setWhisperModel(currentValue)
            }

            Label {
                text: i18n("Requires restart. Download models from huggingface.co/ggerganov/whisper.cpp and place in ~/.local/share/jarvis/")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }

            CheckBox {
                Kirigami.FormData.label: i18n("Auto-start wake word detection:")
                checked: JarvisBackend.autoStartWakeWord
                onToggled: JarvisBackend.setAutoStartWakeWord(checked)
            }

            CheckBox {
                Kirigami.FormData.label: i18n("Continuous conversation mode:")
                checked: JarvisBackend.continuousMode
                onToggled: JarvisBackend.setContinuousMode(checked)
            }

            Label {
                visible: JarvisBackend.continuousMode
                text: i18n("After the wake word, the mic stays open for back-and-forth conversation. Say \"stop\", \"goodbye\", or \"thank you\" to end.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }

            Label {
                text: i18n("Say the wake word to activate voice commands without clicking.")
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

            Slider {
                id: silenceSlider
                Kirigami.FormData.label: i18n("Silence timeout: %1 ms", value.toFixed(0))
                from: 200; to: 3000; stepSize: 80
                value: JarvisBackend.silenceTimeoutMs
                onMoved: JarvisBackend.setSilenceTimeoutMs(value)
            }

            Label {
                text: i18n("How long to wait after you stop speaking before processing. Lower = faster response, higher = fewer false stops.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
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
