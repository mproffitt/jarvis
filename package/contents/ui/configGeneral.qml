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

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Download progress banner (visible on both tabs)
        Kirigami.InlineMessage {
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

        TabBar {
            id: tabBar
            Layout.fillWidth: true

            TabButton { text: i18n("Provider") }
            TabButton { text: i18n("Models") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            // ════════════════════════════════════════
            // TAB 0: PROVIDER
            // ════════════════════════════════════════
            ScrollView {
                id: providerScroll
                contentWidth: availableWidth

                ColumnLayout {
                    width: providerScroll.availableWidth
                    spacing: 0

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

                        RowLayout {
                            Kirigami.FormData.label: i18n("Tools:")
                            spacing: Kirigami.Units.smallSpacing
                            Kirigami.Icon {
                                source: JarvisBackend.modelSupportsTools ? "emblem-default" : "emblem-warning"
                                implicitWidth: Kirigami.Units.iconSizes.small
                                implicitHeight: Kirigami.Units.iconSizes.small
                            }
                            Label {
                                text: {
                                    if (!JarvisBackend.modelSupportsTools)
                                        return i18n("Model does not support tool calling (using ACTION blocks)")
                                    var count = JarvisBackend.mcpToolCount
                                    var servers = JarvisBackend.mcpServerNames
                                    if (count === 0)
                                        return i18n("Native tool calling (no MCP servers)")
                                    return i18n("%1 tools from %2", count, servers.join(", "))
                                }
                                color: JarvisBackend.modelSupportsTools ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.neutralTextColor
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                        }

                        // ── Authentication ──

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

                        // ── Model selection ──

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

                        // Custom model text field
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

                        // Local model selector (ollama / llama.cpp)
                        RowLayout {
                            Kirigami.FormData.label: i18n("Model:")
                            visible: JarvisBackend.llmProvider === "ollama" || JarvisBackend.llmProvider === "llamacpp"
                            spacing: Kirigami.Units.smallSpacing

                            property var chatModels: {
                                var all = JarvisBackend.availableLlmModels
                                var result = []
                                for (var i = 0; i < all.length; i++) {
                                    if (!all[i].downloaded) continue
                                    var caps = all[i].capabilities || []
                                    for (var c = 0; c < caps.length; c++) {
                                        if (caps[c] === "chat") { result.push(all[i]); break }
                                    }
                                }
                                return result
                            }

                            ComboBox {
                                id: providerModelCombo
                                Layout.fillWidth: true
                                model: {
                                    var items = []
                                    var fm = parent.chatModels
                                    for (var i = 0; i < fm.length; i++)
                                        items.push(fm[i].name + "  (" + fm[i].size + ")")
                                    if (items.length === 0) items.push(i18n("No models available"))
                                    return items
                                }
                                currentIndex: {
                                    var fm = parent.chatModels
                                    var id = JarvisBackend.llmProvider === "llamacpp"
                                        ? JarvisBackend.currentModelName : JarvisBackend.llmModelId
                                    for (var i = 0; i < fm.length; i++)
                                        if (fm[i].id === id || fm[i].name.indexOf(id) >= 0) return i
                                    return fm.length > 0 ? 0 : -1
                                }
                                onActivated: function(index) {
                                    var fm = parent.chatModels
                                    if (index >= 0 && index < fm.length) {
                                        if (JarvisBackend.llmProvider === "llamacpp")
                                            JarvisBackend.setActiveLlmModel(fm[index].id)
                                        else
                                            JarvisBackend.setLlmModelId(fm[index].id)
                                    }
                                }
                            }
                            Button {
                                icon.name: "view-refresh"
                                ToolTip.text: i18n("Refresh model list")
                                ToolTip.visible: hovered
                                onClicked: JarvisBackend.refreshOllamaModels()
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

                            property var srcChoices: {
                                if (JarvisBackend.llmProvider === "ollama") {
                                    var all = JarvisBackend.availableLlmModels
                                    var result = []
                                    for (var i = 0; i < all.length; i++) {
                                        var caps = all[i].capabilities || []
                                        for (var c = 0; c < caps.length; c++) {
                                            if (caps[c] === "chat") { result.push(all[i]); break }
                                        }
                                    }
                                    return result
                                }
                                return JarvisBackend.cloudModelChoices
                            }

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
                    }

                    Item { Layout.preferredHeight: Kirigami.Units.largeSpacing }
                }
            }

            // ════════════════════════════════════════
            // TAB 1: MODELS (Download)
            // ════════════════════════════════════════
            ColumnLayout {
                spacing: 0

                // Fixed filter bar — does not scroll
                Kirigami.FormLayout {
                    Layout.fillWidth: true
                    Component.onCompleted: {
                        for (var i = 0; i < children.length; i++) {
                            if (children[i].hasOwnProperty("columns")) {
                                children[i].anchors.horizontalCenter = undefined;
                                children[i].anchors.left = left;
                                children[i].anchors.leftMargin = Qt.binding(function() { return Kirigami.Units.smallSpacing; });
                            }
                        }
                    }

                        Kirigami.Separator {
                            Kirigami.FormData.isSection: true
                            Kirigami.FormData.label: i18n("Find Models")
                        }

                        // ── Filters ──

                        RowLayout {
                            Kirigami.FormData.label: i18n("Filters:")
                            spacing: Kirigami.Units.largeSpacing

                            Label { text: i18n("Category:") }
                            ComboBox {
                                id: modelCategoryCombo
                                model: [i18n("General"), i18n("Fast"), i18n("Vision"), i18n("Embedding")]
                                currentIndex: 0
                                onCurrentIndexChanged: {
                                    modelSizeCombo.currentIndex = 0
                                    hfSearchField.hfDoSearch()
                                }
                            }

                            Label {
                                text: i18n("Size:")
                                visible: modelSizeCombo.visible
                            }
                            ComboBox {
                                id: modelSizeCombo
                                visible: modelCategoryCombo.currentIndex !== 3 && modelCategoryCombo.currentIndex !== 1
                                model: [i18n("Any"), i18n("Tiny (<3B)"), i18n("Small (3–8B)"), i18n("Medium (8–35B)"), i18n("Large (>35B)")]
                                currentIndex: 0
                                onCurrentIndexChanged: filteredResultsModel.update()
                            }

                            Label {
                                text: i18n("Quantization:")
                                visible: modelQuantCombo.visible
                            }
                            ComboBox {
                                id: modelQuantCombo
                                visible: modelCategoryCombo.currentIndex !== 3
                                model: {
                                    var quants = ["Any"]
                                    var results = JarvisBackend.hfSearchResults
                                    var seen = {}
                                    for (var i = 0; i < results.length; i++) {
                                        var q = results[i].quant || ""
                                        if (q.length > 0 && q !== "default" && !seen[q]) {
                                            seen[q] = true
                                            quants.push(q)
                                        }
                                    }
                                    return quants
                                }
                                currentIndex: 0
                                onCurrentIndexChanged: filteredResultsModel.update()
                            }
                        }

                        Label {
                            text: {
                                var cat = modelCategoryCombo.currentIndex
                                if (cat === 0) return i18n("Capable models for complex tasks and conversations.")
                                if (cat === 1) return i18n("Small, quick models for simple tasks. Good as a fast routing model.")
                                if (cat === 2) return i18n("Multimodal models that can understand images and screenshots.")
                                return i18n("Models for RAG document search and semantic similarity.")
                            }
                            color: Kirigami.Theme.disabledTextColor
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }

                        TextField {
                            id: hfSearchField
                            Kirigami.FormData.label: i18n("Search:")
                            placeholderText: {
                                var cat = modelCategoryCombo.currentIndex
                                if (cat === 3) return i18n("Search embedding models...")
                                if (cat === 2) return i18n("Search vision models...")
                                if (cat === 1) return i18n("Search small models...")
                                return i18n("Search models...")
                            }
                            Layout.fillWidth: true
                            implicitWidth: Kirigami.Units.gridUnit * 12
                            onTextChanged: hfSearchTimer.restart()
                            Keys.onReturnPressed: function(event) { hfSearchTimer.stop(); hfDoSearch(); event.accepted = true }
                            Keys.onEnterPressed: function(event) { hfSearchTimer.stop(); hfDoSearch(); event.accepted = true }

                            function hfDoSearch() {
                                var cat = modelCategoryCombo.currentIndex
                                var prefix
                                if (cat === 3) prefix = "embedding"
                                else if (cat === 2) prefix = "gguf vision"
                                else if (cat === 1) prefix = "gguf small instruct"
                                else prefix = "gguf instruct"

                                var query = hfSearchField.text.length > 0
                                    ? prefix + " " + hfSearchField.text
                                    : prefix
                                JarvisBackend.searchModels(query)
                            }

                            Component.onCompleted: hfDoSearch()

                            Timer {
                                id: hfSearchTimer
                                interval: 500
                                onTriggered: {
                                    if (hfSearchField.text.length >= 2)
                                        hfSearchField.hfDoSearch()
                                }
                            }
                        }

                        Label {
                            text: {
                                var n = filteredResultsModel.items.length
                                var total = JarvisBackend.hfSearchResults.length
                                if (n === total)
                                    return i18n("Showing %1 results, sorted by popularity.", n)
                                return i18n("Showing %1 of %2 results (filtered).", n, total)
                            }
                            color: Kirigami.Theme.disabledTextColor
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                    }

                // ── Filtered results model ──
                QtObject {
                    id: filteredResultsModel
                    property var items: []

                    function update() {
                        var results = JarvisBackend.hfSearchResults
                        var catIdx = modelCategoryCombo.currentIndex
                        var sizeIdx = modelSizeCombo.currentIndex
                        var sizeFilter = sizeIdx === 1 ? "tiny" : sizeIdx === 2 ? "small"
                                       : sizeIdx === 3 ? "medium" : sizeIdx === 4 ? "large" : ""

                        var isFast = (catIdx === 1)
                        var isGeneral = (catIdx === 0)

                        var quantIdx = modelQuantCombo.currentIndex
                        var quantFilter = quantIdx > 0 ? modelQuantCombo.model[quantIdx] : ""

                        var out = []
                        for (var i = 0; i < results.length; i++) {
                            var r = results[i]
                            var sc = r.sizeCategory || ""

                            if (isFast) {
                                // Fast: only tiny and small
                                if (sc !== "tiny" && sc !== "small" && sc !== "")
                                    continue
                            } else if (isGeneral) {
                                // General: exclude tiny (not capable enough)
                                if (sc === "tiny")
                                    continue
                                if (sizeFilter.length > 0 && sc !== sizeFilter)
                                    continue
                            } else if (sizeFilter.length > 0 && sc !== sizeFilter) {
                                continue
                            }

                            if (quantFilter.length > 0) {
                                var q = r.quant || ""
                                if (q !== quantFilter) continue
                            }
                            out.push(r)
                        }
                        items = out
                    }

                    Component.onCompleted: update()
                }

                Connections {
                    target: JarvisBackend
                    function onHfSearchResultsChanged() {
                        modelQuantCombo.currentIndex = 0
                        filteredResultsModel.update()
                    }
                }

                // ── Scrollable result cards ──
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ListView {
                        id: modelResultsList
                        clip: true
                        spacing: Kirigami.Units.smallSpacing
                        model: filteredResultsModel.items

                        delegate: Kirigami.AbstractCard {
                            id: modelCard
                            width: modelResultsList.width - Kirigami.Units.smallSpacing * 2
                            x: Kirigami.Units.smallSpacing

                            property bool expanded: false
                            property bool detailsLoaded: false

                            contentItem: ColumnLayout {
                                spacing: Kirigami.Units.smallSpacing

                                RowLayout {
                                    spacing: Kirigami.Units.largeSpacing
                                    Layout.fillWidth: true
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
                                                text: modelData.sizeCategory ? modelData.sizeCategory : ""
                                                visible: text.length > 0
                                                color: Kirigami.Theme.disabledTextColor
                                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                                font.capitalization: Font.Capitalize
                                            }
                                            Label {
                                                text: modelData.downloads ? (modelData.downloads + " downloads") : ""
                                                visible: modelData.downloads && modelData.downloads.length > 0
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
                                        text: modelCard.expanded ? "▲" : "▼"
                                        flat: true
                                        implicitWidth: Kirigami.Units.gridUnit * 2
                                        onClicked: {
                                            modelCard.expanded = !modelCard.expanded
                                            if (modelCard.expanded && !modelCard.detailsLoaded) {
                                                JarvisBackend.fetchModelDetails(modelData.id)
                                                modelCard.detailsLoaded = true
                                            }
                                        }
                                    }
                                    Button {
                                        text: JarvisBackend.llmProvider === "ollama" ? i18n("Pull") : i18n("Download")
                                        icon.name: "download"
                                        enabled: !JarvisBackend.downloading
                                        onClicked: {
                                            if (JarvisBackend.llmProvider === "ollama") {
                                                var file = modelData.file ? modelData.file : ""
                                                var tag = file ? ":" + file : ""
                                                JarvisBackend.pullOllamaModel("hf.co/" + modelData.id + tag)
                                            } else {
                                                JarvisBackend.downloadLlmModel(modelData.id)
                                            }
                                        }
                                    }
                                }

                                // Expanded details
                                GridLayout {
                                    visible: modelCard.expanded && JarvisBackend.modelDetails.id === modelData.id
                                    Layout.fillWidth: true
                                    columns: 2
                                    columnSpacing: Kirigami.Units.largeSpacing
                                    rowSpacing: 2

                                    Label { text: i18n("File size:"); font.bold: true; font.pointSize: Kirigami.Theme.smallFont.pointSize }
                                    Label {
                                        font.pointSize: Kirigami.Theme.smallFont.pointSize; Layout.fillWidth: true
                                        text: {
                                            var sizes = JarvisBackend.modelDetails.fileSizes
                                            if (!sizes) return "loading..."
                                            var file = modelData.file || ""
                                            if (file && sizes[file]) return sizes[file]
                                            if (sizes[""]) return sizes[""] + " (total)"
                                            return "loading..."
                                        }
                                    }

                                    Label { text: i18n("Parameters:"); font.bold: true; font.pointSize: Kirigami.Theme.smallFont.pointSize }
                                    Label { text: JarvisBackend.modelDetails.params || "—"; font.pointSize: Kirigami.Theme.smallFont.pointSize; Layout.fillWidth: true }

                                    Label { text: i18n("Architecture:"); font.bold: true; font.pointSize: Kirigami.Theme.smallFont.pointSize }
                                    Label { text: JarvisBackend.modelDetails.architecture || "—"; font.pointSize: Kirigami.Theme.smallFont.pointSize; Layout.fillWidth: true }

                                    Label { text: i18n("Context:"); font.bold: true; font.pointSize: Kirigami.Theme.smallFont.pointSize }
                                    Label { text: JarvisBackend.modelDetails.contextLength ? JarvisBackend.modelDetails.contextLength.toLocaleString() + " tokens" : "—"; font.pointSize: Kirigami.Theme.smallFont.pointSize; Layout.fillWidth: true }

                                    Label { text: i18n("License:"); font.bold: true; font.pointSize: Kirigami.Theme.smallFont.pointSize }
                                    Label { text: JarvisBackend.modelDetails.license || "—"; font.pointSize: Kirigami.Theme.smallFont.pointSize; Layout.fillWidth: true }

                                    Label { text: i18n("Author:"); font.bold: true; font.pointSize: Kirigami.Theme.smallFont.pointSize }
                                    Label { text: JarvisBackend.modelDetails.author || "—"; font.pointSize: Kirigami.Theme.smallFont.pointSize; Layout.fillWidth: true }

                                    Label { text: i18n("Base model:"); font.bold: true; font.pointSize: Kirigami.Theme.smallFont.pointSize }
                                    Label { text: JarvisBackend.modelDetails.baseModel || "—"; font.pointSize: Kirigami.Theme.smallFont.pointSize; Layout.fillWidth: true; wrapMode: Text.Wrap }

                                    Label { text: i18n("Tags:"); font.bold: true; font.pointSize: Kirigami.Theme.smallFont.pointSize }
                                    Label { text: JarvisBackend.modelDetails.tags || "—"; font.pointSize: Kirigami.Theme.smallFont.pointSize; Layout.fillWidth: true; wrapMode: Text.Wrap }

                                    Label { text: i18n("Likes:"); font.bold: true; font.pointSize: Kirigami.Theme.smallFont.pointSize }
                                    Label { text: JarvisBackend.modelDetails.likes ? JarvisBackend.modelDetails.likes.toString() : "—"; font.pointSize: Kirigami.Theme.smallFont.pointSize; Layout.fillWidth: true }
                                }

                                Label {
                                    visible: modelCard.expanded && JarvisBackend.modelDetails.id === modelData.id
                                    text: "<a href=\"" + (JarvisBackend.modelDetails.url || "") + "\">View on HuggingFace</a>"
                                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                                    onLinkActivated: function(link) { Qt.openUrlExternally(link) }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
