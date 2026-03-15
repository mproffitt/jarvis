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
    clip: true

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
            Component.onCompleted: { for (var i = 0; i < children.length; i++) { if (children[i].hasOwnProperty("columns")) { children[i].anchors.horizontalCenter = undefined; children[i].anchors.left = left; children[i].anchors.leftMargin = Qt.binding(function() { return Kirigami.Units.smallSpacing; }); } } }
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
                    implicitWidth: Kirigami.Units.gridUnit * 12
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
                    implicitWidth: Kirigami.Units.gridUnit * 12
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
                    implicitWidth: Kirigami.Units.gridUnit * 12
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
                        implicitWidth: Kirigami.Units.gridUnit * 12
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
                    implicitWidth: Kirigami.Units.gridUnit * 12
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
        // DOWNLOAD NEW OLLAMA MODELS
        // ════════════════════════════════════════
        Kirigami.FormLayout {
            Layout.fillWidth: true
            visible: JarvisBackend.llmProvider === "ollama"
            Component.onCompleted: { for (var i = 0; i < children.length; i++) { if (children[i].hasOwnProperty("columns")) { children[i].anchors.horizontalCenter = undefined; children[i].anchors.left = left; children[i].anchors.leftMargin = Qt.binding(function() { return Kirigami.Units.smallSpacing; }); } } }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Download New Models")
            }

            Label {
                text: i18n("Search the Ollama registry, or enter a model name to pull directly.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Search:")
                spacing: Kirigami.Units.smallSpacing
                TextField {
                    id: ollamaFilterField
                    placeholderText: i18n("Search models or enter name to pull...")
                    Layout.fillWidth: true
                    implicitWidth: Kirigami.Units.gridUnit * 12
                    onAccepted: JarvisBackend.fetchSuggestedOllamaModels(text)
                }
                Button {
                    icon.name: "search"
                    onClicked: JarvisBackend.fetchSuggestedOllamaModels(ollamaFilterField.text)
                }
                Button {
                    text: i18n("Pull")
                    icon.name: "download"
                    visible: ollamaFilterField.text.length > 0
                    enabled: !JarvisBackend.downloading
                    onClicked: JarvisBackend.pullOllamaModel(ollamaFilterField.text)
                }
            }
        }

        Repeater {
            id: ollamaSuggestedRepeater
            model: JarvisBackend.llmProvider === "ollama" ? JarvisBackend.suggestedOllamaModels : []
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
                        text: i18n("Pull")
                        icon.name: "download"
                        enabled: !JarvisBackend.downloading
                        onClicked: JarvisBackend.pullOllamaModel(modelData.id)
                    }
                }
            }
        }

        // ════════════════════════════════════════
        // LLM MODELS (llama.cpp GGUF download)
        // ════════════════════════════════════════
        Kirigami.FormLayout {
            Component.onCompleted: { for (var i = 0; i < children.length; i++) { if (children[i].hasOwnProperty("columns")) { children[i].anchors.horizontalCenter = undefined; children[i].anchors.left = left; children[i].anchors.leftMargin = Qt.binding(function() { return Kirigami.Units.smallSpacing; }); } } }
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

        // Bottom spacer
        Item { Layout.preferredHeight: Kirigami.Units.largeSpacing }
    }
    }
}
