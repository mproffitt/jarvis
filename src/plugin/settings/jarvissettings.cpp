#include "jarvissettings.h"
#include "jarvisoauth.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <KWallet/KWallet>

JarvisSettings::JarvisSettings(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent)
    , m_networkManager(nam)
    , m_oauth(new JarvisOAuth(nam, this))
{
    loadSettings();
    openWallet();

    // Forward OAuth signals
    connect(m_oauth, &JarvisOAuth::tokenRefreshed, this, [this](const QString &provider) {
        if (provider == m_llmProvider) {
            qDebug() << "[JARVIS] OAuth token refreshed for" << provider;
            emit oauthTokenReady();
        }
    });
    connect(m_oauth, &JarvisOAuth::tokenError, this, [this](const QString &provider, const QString &error) {
        if (provider == m_llmProvider) {
            qWarning() << "[JARVIS] OAuth token error for" << provider << ":" << error;
            emit oauthTokenError(error);
        }
    });
    connect(m_oauth, &JarvisOAuth::loginStarted, this, [this](const QString &provider) {
        emit oauthLoginStarted(provider);
        emit oauthStatusChanged();
    });
    connect(m_oauth, &JarvisOAuth::loginFinished, this, [this](const QString &provider, bool success) {
        emit oauthLoginFinished(provider, success);
        emit oauthStatusChanged();
        if (success) fetchCloudModels();
    });
    connect(m_oauth, &JarvisOAuth::loginStatusChanged, this, &JarvisSettings::oauthStatusChanged);

    if (m_llmProvider == QStringLiteral("ollama")) {
        fetchOllamaModels();
    } else if (m_llmProvider == QStringLiteral("llamacpp")) {
        populateModelList();
    } else {
        fetchCloudModels();
    }
    fetchPiperVoices(QStringLiteral("en"));

    // Auto-search HuggingFace for initial results (for ollama/llamacpp)
    if (m_llmProvider == QStringLiteral("ollama") || m_llmProvider == QStringLiteral("llamacpp")) {
        searchHuggingFaceModels(QString());
    }
}

// ─────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────

QString JarvisSettings::jarvisDataDir() const
{
    const QString dir = QDir::homePath() + QStringLiteral("/.local/share/jarvis");
    QDir().mkpath(dir);
    return dir;
}

// ─────────────────────────────────────────────
// KWallet
// ─────────────────────────────────────────────

void JarvisSettings::openWallet()
{
    m_wallet = KWallet::Wallet::openWallet(
        KWallet::Wallet::LocalWallet(), 0, KWallet::Wallet::Asynchronous);
    if (m_wallet) {
        connect(m_wallet, &KWallet::Wallet::walletOpened,
                this, &JarvisSettings::onWalletOpened);
    } else {
        qWarning() << "[JARVIS] KWallet unavailable — API keys will not be persisted";
    }
}

void JarvisSettings::onWalletOpened(bool success)
{
    if (!success || !m_wallet) {
        qWarning() << "[JARVIS] Failed to open KWallet — API keys will not be persisted";
        return;
    }

    if (!m_wallet->hasFolder(WALLET_FOLDER)) {
        m_wallet->createFolder(WALLET_FOLDER);
    }
    m_wallet->setFolder(WALLET_FOLDER);

    // Read stored keys
    QString key;
    if (m_wallet->readPassword(QStringLiteral("openai-api-key"), key) == 0 && !key.isEmpty()) {
        m_openaiApiKey = key;
        emit openaiApiKeyChanged();
    }
    if (m_wallet->readPassword(QStringLiteral("gemini-api-key"), key) == 0 && !key.isEmpty()) {
        m_geminiApiKey = key;
        emit geminiApiKeyChanged();
    }
    if (m_wallet->readPassword(QStringLiteral("claude-api-key"), key) == 0 && !key.isEmpty()) {
        m_claudeApiKey = key;
        emit claudeApiKeyChanged();
    }

    // Migrate any legacy key that was loaded from QSettings
    if (!m_openaiApiKey.isEmpty() && m_wallet->readPassword(QStringLiteral("openai-api-key"), key) == 0 && key.isEmpty()) {
        writeKeyToWallet(QStringLiteral("openai-api-key"), m_openaiApiKey);
    }

    qDebug() << "[JARVIS] KWallet opened — API keys loaded";
}

void JarvisSettings::writeKeyToWallet(const QString &entry, const QString &key)
{
    if (!m_wallet || !m_wallet->isOpen()) {
        qWarning() << "[JARVIS] KWallet not open — cannot save API key";
        return;
    }

    if (!m_wallet->hasFolder(WALLET_FOLDER)) {
        m_wallet->createFolder(WALLET_FOLDER);
    }
    m_wallet->setFolder(WALLET_FOLDER);

    if (key.isEmpty()) {
        m_wallet->removeEntry(entry);
    } else {
        m_wallet->writePassword(entry, key);
    }
}

// ─────────────────────────────────────────────
// Persistence
// ─────────────────────────────────────────────

void JarvisSettings::loadSettings()
{
    m_llmProvider = m_settings.value(QStringLiteral("llm/provider"),
                                      QStringLiteral("llamacpp")).toString();
    m_llmServerUrl = m_settings.value(QStringLiteral("llm/serverUrl"),
                                       defaultUrlForProvider(m_llmProvider)).toString();
    m_llmModelId = m_settings.value(QStringLiteral("llm/modelId")).toString();
    // Migrate any plaintext API key from old config into memory (will be moved to KWallet on first save)
    const QString legacyKey = m_settings.value(QStringLiteral("llm/apiKey")).toString();
    if (!legacyKey.isEmpty()) {
        m_openaiApiKey = legacyKey; // assume it was for the active provider
        m_settings.remove(QStringLiteral("llm/apiKey"));
        m_settings.sync();
    }
    m_currentModelName = m_settings.value(QStringLiteral("llm/modelName"),
                                           QStringLiteral("Qwen2.5-Coder-1.5B-Instruct")).toString();
    m_currentVoiceName = m_settings.value(QStringLiteral("tts/voiceName"),
                                           QStringLiteral("en_GB-alan-medium")).toString();
    m_maxHistoryPairs = m_settings.value(QStringLiteral("chat/maxHistoryPairs"), 20).toInt();
    m_wakeBufferSeconds = m_settings.value(QStringLiteral("audio/wakeBufferSeconds"), 2).toInt();
    m_voiceCmdMaxSeconds = m_settings.value(QStringLiteral("audio/voiceCmdMaxSeconds"), 8).toInt();
    m_silenceTimeoutMs = m_settings.value(QStringLiteral("audio/silenceTimeoutMs"), 640).toInt();
    m_autoStartWakeWord = m_settings.value(QStringLiteral("audio/autoStartWakeWord"), true).toBool();
    m_noiseSuppression = m_settings.value(QStringLiteral("audio/noiseSuppression"), true).toBool();
    m_whisperModel = m_settings.value(QStringLiteral("audio/whisperModel"), QStringLiteral("tiny")).toString();
    m_wakeWord = m_settings.value(QStringLiteral("audio/wakeWord"), QStringLiteral("jarvis")).toString();
    m_continuousMode = m_settings.value(QStringLiteral("audio/continuousMode"), false).toBool();
    m_smartRouting = m_settings.value(QStringLiteral("llm/smartRouting"), false).toBool();
    m_fastModelId = m_settings.value(QStringLiteral("llm/fastModelId")).toString();
    m_ttsRate = m_settings.value(QStringLiteral("tts/rate"), 0.05).toDouble();
    m_ttsPitch = m_settings.value(QStringLiteral("tts/pitch"), -0.1).toDouble();
    m_ttsVolume = m_settings.value(QStringLiteral("tts/volume"), 0.85).toDouble();
    m_ttsMuted = m_settings.value(QStringLiteral("tts/muted"), false).toBool();
    m_personalityPrompt = m_settings.value(QStringLiteral("chat/personalityPrompt")).toString();

    // Resolve piper model path from saved voice name
    const QString voicesDir = jarvisDataDir() + QStringLiteral("/piper-voices");
    const QString savedPath = voicesDir + QStringLiteral("/") + m_currentVoiceName + QStringLiteral(".onnx");
    if (QFile::exists(savedPath)) {
        m_piperModelPath = savedPath;
    } else {
        // Fallback search
        const QStringList fallbackPaths = {
            QDir::homePath() + QStringLiteral("/.local/share/jarvis/piper-voices/en_GB-alan-medium.onnx"),
            QStringLiteral("/usr/share/jarvis/piper-voices/en_GB-alan-medium.onnx"),
        };
        for (const auto &path : fallbackPaths) {
            if (QFile::exists(path)) { m_piperModelPath = path; break; }
        }
    }
}

void JarvisSettings::saveSettings()
{
    m_settings.setValue(QStringLiteral("llm/provider"), m_llmProvider);
    m_settings.setValue(QStringLiteral("llm/serverUrl"), m_llmServerUrl);
    m_settings.setValue(QStringLiteral("llm/modelId"), m_llmModelId);
    m_settings.setValue(QStringLiteral("llm/modelName"), m_currentModelName);
    m_settings.setValue(QStringLiteral("tts/voiceName"), m_currentVoiceName);
    m_settings.setValue(QStringLiteral("chat/maxHistoryPairs"), m_maxHistoryPairs);
    m_settings.setValue(QStringLiteral("audio/wakeBufferSeconds"), m_wakeBufferSeconds);
    m_settings.setValue(QStringLiteral("audio/voiceCmdMaxSeconds"), m_voiceCmdMaxSeconds);
    m_settings.setValue(QStringLiteral("audio/silenceTimeoutMs"), m_silenceTimeoutMs);
    m_settings.setValue(QStringLiteral("audio/autoStartWakeWord"), m_autoStartWakeWord);
    m_settings.setValue(QStringLiteral("audio/noiseSuppression"), m_noiseSuppression);
    m_settings.setValue(QStringLiteral("audio/whisperModel"), m_whisperModel);
    m_settings.setValue(QStringLiteral("audio/wakeWord"), m_wakeWord);
    m_settings.setValue(QStringLiteral("audio/continuousMode"), m_continuousMode);
    m_settings.setValue(QStringLiteral("llm/smartRouting"), m_smartRouting);
    m_settings.setValue(QStringLiteral("llm/fastModelId"), m_fastModelId);
    m_settings.setValue(QStringLiteral("tts/rate"), m_ttsRate);
    m_settings.setValue(QStringLiteral("tts/pitch"), m_ttsPitch);
    m_settings.setValue(QStringLiteral("tts/volume"), m_ttsVolume);
    m_settings.setValue(QStringLiteral("tts/muted"), m_ttsMuted);
    m_settings.setValue(QStringLiteral("chat/personalityPrompt"), m_personalityPrompt);
    m_settings.sync();
}

// ─────────────────────────────────────────────
// Setters
// ─────────────────────────────────────────────

void JarvisSettings::setLlmServerUrl(const QString &url)
{
    if (m_llmServerUrl != url) {
        m_llmServerUrl = url;
        saveSettings();
        emit llmServerUrlChanged();
    }
}

void JarvisSettings::setLlmProvider(const QString &provider)
{
    if (m_llmProvider != provider) {
        // Save current model for the old provider
        if (!m_llmModelId.isEmpty())
            m_settings.setValue(QStringLiteral("llm/modelId/%1").arg(m_llmProvider), m_llmModelId);
        m_llmProvider = provider;
        // Update URL to the default for this provider
        m_llmServerUrl = defaultUrlForProvider(provider);
        // Restore saved model for the new provider
        m_llmModelId = m_settings.value(QStringLiteral("llm/modelId/%1").arg(provider)).toString();
        saveSettings();
        emit llmProviderChanged();
        emit llmServerUrlChanged();
        emit llmModelIdChanged();
        // Refresh model list for the new provider
        if (provider == QStringLiteral("ollama")) {
            fetchOllamaModels();
        } else if (provider == QStringLiteral("llamacpp")) {
            populateModelList();
        } else {
            fetchCloudModels();
        }
    }
}

void JarvisSettings::setOpenaiApiKey(const QString &key)
{
    if (m_openaiApiKey != key) {
        m_openaiApiKey = key;
        writeKeyToWallet(QStringLiteral("openai-api-key"), key);
        emit openaiApiKeyChanged();
    }
}

void JarvisSettings::setGeminiApiKey(const QString &key)
{
    if (m_geminiApiKey != key) {
        m_geminiApiKey = key;
        writeKeyToWallet(QStringLiteral("gemini-api-key"), key);
        emit geminiApiKeyChanged();
    }
}

void JarvisSettings::setClaudeApiKey(const QString &key)
{
    if (m_claudeApiKey != key) {
        m_claudeApiKey = key;
        writeKeyToWallet(QStringLiteral("claude-api-key"), key);
        emit claudeApiKeyChanged();
    }
}

QString JarvisSettings::activeApiKey()
{
    const auto env = QProcessEnvironment::systemEnvironment();

    if (m_llmProvider == QStringLiteral("openai")) {
        if (!m_openaiApiKey.isEmpty()) return m_openaiApiKey;
        return env.value(QStringLiteral("OPENAI_API_KEY"));
    }
    if (m_llmProvider == QStringLiteral("gemini")) {
        if (!m_geminiApiKey.isEmpty()) return m_geminiApiKey;
        // Try CLI OAuth token
        const QString oauthToken = m_oauth->accessToken(QStringLiteral("gemini"));
        if (!oauthToken.isEmpty()) return oauthToken;
        return env.value(QStringLiteral("GEMINI_API_KEY"));
    }
    if (m_llmProvider == QStringLiteral("claude")) {
        if (!m_claudeApiKey.isEmpty()) return m_claudeApiKey;
        // Try CLI OAuth token
        const QString oauthToken = m_oauth->accessToken(QStringLiteral("claude"));
        if (!oauthToken.isEmpty()) return oauthToken;
        return env.value(QStringLiteral("ANTHROPIC_API_KEY"));
    }
    return {};
}

bool JarvisSettings::hasOAuthCredentials() const
{
    return m_oauth->hasCredentials(m_llmProvider);
}

void JarvisSettings::ensureOAuthToken()
{
    m_oauth->ensureValidToken(m_llmProvider);
}

bool JarvisSettings::isOAuthLoggedIn()
{
    return m_oauth->hasValidToken(m_llmProvider) || m_oauth->hasCredentials(m_llmProvider);
}

void JarvisSettings::oauthLogin(const QString &provider)
{
    m_oauth->startLogin(provider);
}

void JarvisSettings::oauthLogout(const QString &provider)
{
    m_oauth->logout(provider);
}

void JarvisSettings::cancelOAuthLogin()
{
    m_oauth->cancelLogin();
}

void JarvisSettings::completeClaudeLogin(const QString &code)
{
    m_oauth->completeClaudeLogin(code);
}

bool JarvisSettings::awaitingClaudeCode() const
{
    return m_oauth->awaitingClaudeCode();
}

bool JarvisSettings::isGeminiOAuthMode() const
{
    // We're in Gemini OAuth mode when: provider is gemini, no API key set, and OAuth creds exist
    return m_llmProvider == QStringLiteral("gemini")
        && m_geminiApiKey.isEmpty()
        && m_oauth->isGeminiOAuthMode();
}

QString JarvisSettings::geminiCloudCodeUrl() const
{
    return QStringLiteral("https://cloudcode-pa.googleapis.com/v1internal");
}

QString JarvisSettings::geminiProjectId() const
{
    return m_oauth->geminiProjectId();
}

void JarvisSettings::setLlmModelId(const QString &modelId)
{
    if (m_llmModelId != modelId) {
        m_llmModelId = modelId;
        // Save per-provider so switching back restores it
        m_settings.setValue(QStringLiteral("llm/modelId/%1").arg(m_llmProvider), modelId);
        saveSettings();
        emit llmModelIdChanged();
    }
}

void JarvisSettings::setCurrentModelName(const QString &name)
{
    if (m_currentModelName != name) {
        m_currentModelName = name;
        saveSettings();
        emit currentModelNameChanged();
    }
}

void JarvisSettings::setMaxHistoryPairs(int pairs)
{
    pairs = qBound(5, pairs, 100);
    if (m_maxHistoryPairs != pairs) {
        m_maxHistoryPairs = pairs;
        saveSettings();
        emit maxHistoryPairsChanged();
    }
}

void JarvisSettings::setWakeBufferSeconds(int seconds)
{
    seconds = qBound(1, seconds, 5);
    if (m_wakeBufferSeconds != seconds) {
        m_wakeBufferSeconds = seconds;
        saveSettings();
        emit wakeBufferSecondsChanged();
    }
}

void JarvisSettings::setVoiceCmdMaxSeconds(int seconds)
{
    seconds = qBound(3, seconds, 30);
    if (m_voiceCmdMaxSeconds != seconds) {
        m_voiceCmdMaxSeconds = seconds;
        saveSettings();
        emit voiceCmdMaxSecondsChanged();
    }
}

void JarvisSettings::setSilenceTimeoutMs(int ms)
{
    ms = qBound(200, ms, 3000);
    if (m_silenceTimeoutMs != ms) {
        m_silenceTimeoutMs = ms;
        saveSettings();
        emit silenceTimeoutMsChanged();
    }
}

void JarvisSettings::setAutoStartWakeWord(bool enabled)
{
    if (m_autoStartWakeWord != enabled) {
        m_autoStartWakeWord = enabled;
        saveSettings();
        emit autoStartWakeWordChanged();
    }
}

void JarvisSettings::setNoiseSuppression(bool enabled)
{
    if (m_noiseSuppression != enabled) {
        m_noiseSuppression = enabled;
        saveSettings();
        emit noiseSuppressionChanged();
    }
}

void JarvisSettings::setWhisperModel(const QString &model)
{
    if (m_whisperModel != model) {
        m_whisperModel = model;
        saveSettings();
        emit whisperModelChanged();
    }
}

void JarvisSettings::setWakeWord(const QString &word)
{
    const QString cleaned = word.toLower().trimmed();
    if (m_wakeWord != cleaned) {
        m_wakeWord = cleaned;
        saveSettings();
        emit wakeWordChanged();
    }
}

void JarvisSettings::setContinuousMode(bool enabled)
{
    if (m_continuousMode != enabled) {
        m_continuousMode = enabled;
        saveSettings();
        emit continuousModeChanged();
    }
}

void JarvisSettings::setSmartRouting(bool enabled)
{
    if (m_smartRouting != enabled) {
        m_smartRouting = enabled;
        saveSettings();
        emit smartRoutingChanged();
    }
}

void JarvisSettings::setFastModelId(const QString &modelId)
{
    if (m_fastModelId != modelId) {
        m_fastModelId = modelId;
        // Save per-provider
        m_settings.setValue(QStringLiteral("llm/fastModelId/%1").arg(m_llmProvider), modelId);
        saveSettings();
        emit fastModelIdChanged();
    }
}

QString JarvisSettings::routeModel(const QString &query) const
{
    if (!m_smartRouting || m_fastModelId.isEmpty())
        return m_llmModelId;

    const QString lower = query.toLower();
    const int wordCount = lower.split(QLatin1Char(' '), Qt::SkipEmptyParts).size();

    // Complex indicators — use full model
    static const QStringList complexKeywords = {
        QStringLiteral("explain"), QStringLiteral("describe"), QStringLiteral("analyze"),
        QStringLiteral("compare"), QStringLiteral("write"), QStringLiteral("code"),
        QStringLiteral("implement"), QStringLiteral("step by step"), QStringLiteral("in detail"),
        QStringLiteral("summarize"), QStringLiteral("translate"), QStringLiteral("review"),
        QStringLiteral("debug"), QStringLiteral("refactor"), QStringLiteral("why"),
        QStringLiteral("how does"), QStringLiteral("difference between"),
    };

    for (const auto &kw : complexKeywords) {
        if (lower.contains(kw)) return m_llmModelId;
    }

    // Long queries are likely complex
    if (wordCount > 15) return m_llmModelId;

    // Short/simple queries — use fast model
    return m_fastModelId;
}

void JarvisSettings::setPersonalityPrompt(const QString &prompt)
{
    if (m_personalityPrompt != prompt) {
        m_personalityPrompt = prompt;
        saveSettings();
        emit personalityPromptChanged();
    }
}

void JarvisSettings::setTtsRate(double rate)
{
    m_ttsRate = qBound(-1.0, rate, 1.0);
    saveSettings();
    emit ttsRateChanged();
}

void JarvisSettings::setTtsPitch(double pitch)
{
    m_ttsPitch = qBound(-1.0, pitch, 1.0);
    saveSettings();
    emit ttsPitchChanged();
}

void JarvisSettings::setTtsVolume(double volume)
{
    m_ttsVolume = qBound(0.0, volume, 1.0);
    saveSettings();
    emit ttsVolumeChanged();
}

void JarvisSettings::setTtsMuted(bool muted)
{
    if (m_ttsMuted != muted) {
        m_ttsMuted = muted;
        saveSettings();
        emit ttsMutedChanged();
    }
}

// ─────────────────────────────────────────────
// Provider Helpers
// ─────────────────────────────────────────────

QString JarvisSettings::defaultUrlForProvider(const QString &provider) const
{
    if (provider == QStringLiteral("ollama"))
        return QStringLiteral("http://127.0.0.1:11434");
    if (provider == QStringLiteral("openai"))
        return QStringLiteral("https://api.openai.com");
    if (provider == QStringLiteral("gemini"))
        return QStringLiteral("https://generativelanguage.googleapis.com/v1beta/openai");
    if (provider == QStringLiteral("claude"))
        return QStringLiteral("https://api.anthropic.com");
    // llamacpp default
    return QStringLiteral("http://127.0.0.1:8080");
}

QString JarvisSettings::chatCompletionsUrl() const
{
    if (m_llmProvider == QStringLiteral("ollama"))
        return m_llmServerUrl + QStringLiteral("/v1/chat/completions");
    if (m_llmProvider == QStringLiteral("openai"))
        return m_llmServerUrl + QStringLiteral("/v1/chat/completions");
    if (m_llmProvider == QStringLiteral("gemini"))
        return m_llmServerUrl + QStringLiteral("/chat/completions");
    if (m_llmProvider == QStringLiteral("claude"))
        return m_llmServerUrl + QStringLiteral("/v1/messages");
    // llamacpp
    return m_llmServerUrl + QStringLiteral("/v1/chat/completions");
}

QString JarvisSettings::healthCheckUrl() const
{
    if (m_llmProvider == QStringLiteral("ollama"))
        return m_llmServerUrl + QStringLiteral("/api/tags");
    if (m_llmProvider == QStringLiteral("openai"))
        return m_llmServerUrl + QStringLiteral("/v1/models");
    if (m_llmProvider == QStringLiteral("gemini"))
        return m_llmServerUrl + QStringLiteral("/models");
    if (m_llmProvider == QStringLiteral("claude"))
        return m_llmServerUrl + QStringLiteral("/v1/models");
    // llamacpp
    return m_llmServerUrl + QStringLiteral("/health");
}

bool JarvisSettings::providerNeedsApiKey() const
{
    return m_llmProvider == QStringLiteral("openai")
        || m_llmProvider == QStringLiteral("gemini")
        || m_llmProvider == QStringLiteral("claude");
}

bool JarvisSettings::providerNeedsModelInRequest() const
{
    return m_llmProvider != QStringLiteral("llamacpp");
}

int JarvisSettings::contextTokenLimit() const
{
    const QString model = m_llmModelId.toLower();

    // Claude models
    if (m_llmProvider == QStringLiteral("claude")) {
        if (model.contains(QStringLiteral("opus"))) return 200000;
        if (model.contains(QStringLiteral("sonnet"))) return 200000;
        if (model.contains(QStringLiteral("haiku"))) return 200000;
        return 200000;
    }
    // Gemini models
    if (m_llmProvider == QStringLiteral("gemini")) {
        if (model.contains(QStringLiteral("2.5"))) return 1000000;
        if (model.contains(QStringLiteral("2.0"))) return 1000000;
        return 1000000;
    }
    // OpenAI models
    if (m_llmProvider == QStringLiteral("openai")) {
        if (model.contains(QStringLiteral("gpt-4o"))) return 128000;
        if (model.contains(QStringLiteral("gpt-4.1"))) return 1000000;
        if (model.contains(QStringLiteral("o3"))) return 200000;
        if (model.contains(QStringLiteral("o4"))) return 200000;
        return 128000;
    }
    // Ollama / llama.cpp — varies widely, use conservative default
    if (model.contains(QStringLiteral("llama3"))) return 128000;
    if (model.contains(QStringLiteral("qwen"))) return 32000;
    if (model.contains(QStringLiteral("mistral"))) return 32000;
    return 8192; // Safe fallback for unknown models
}

void JarvisSettings::fetchCloudModels()
{
    if (m_llmProvider == QStringLiteral("llamacpp") || m_llmProvider == QStringLiteral("ollama"))
        return;

    const QString apiKey = activeApiKey();

    if (m_llmProvider == QStringLiteral("claude")) {
        QUrl url(m_llmServerUrl + QStringLiteral("/v1/models"));
        QNetworkRequest request(url);
        if (apiKey.startsWith(QStringLiteral("sk-ant-oat"))) {
            request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(apiKey).toUtf8());
            request.setRawHeader("anthropic-beta", "oauth-2025-04-20");
        } else {
            request.setRawHeader("x-api-key", apiKey.toUtf8());
        }
        request.setRawHeader("anthropic-version", "2023-06-01");

        auto *reply = m_networkManager->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) return;

            const auto doc = QJsonDocument::fromJson(reply->readAll());
            const auto data = doc.object()[QStringLiteral("data")].toArray();

            m_cloudModelChoices.clear();
            for (const auto &val : data) {
                const auto obj = val.toObject();
                const QString id = obj[QStringLiteral("id")].toString();
                const QString name = obj[QStringLiteral("display_name")].toString();
                m_cloudModelChoices.append(QVariantMap{
                    {QStringLiteral("id"), id},
                    {QStringLiteral("name"), name.isEmpty() ? id : name},
                });
            }
            emit cloudModelChoicesChanged();
        });
        return;
    }

    if (m_llmProvider == QStringLiteral("openai")) {
        QUrl url(m_llmServerUrl + QStringLiteral("/v1/models"));
        QNetworkRequest request(url);
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(apiKey).toUtf8());

        auto *reply = m_networkManager->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) return;

            const auto doc = QJsonDocument::fromJson(reply->readAll());
            const auto data = doc.object()[QStringLiteral("data")].toArray();

            m_cloudModelChoices.clear();
            for (const auto &val : data) {
                const auto obj = val.toObject();
                const QString id = obj[QStringLiteral("id")].toString();
                // Only show chat models, skip embedding/tts/etc
                if (id.startsWith(QStringLiteral("gpt")) || id.startsWith(QStringLiteral("o1"))
                    || id.startsWith(QStringLiteral("o3")) || id.startsWith(QStringLiteral("o4"))) {
                    m_cloudModelChoices.append(QVariantMap{
                        {QStringLiteral("id"), id},
                        {QStringLiteral("name"), id},
                    });
                }
            }
            // Sort by name
            std::sort(m_cloudModelChoices.begin(), m_cloudModelChoices.end(),
                [](const QVariant &a, const QVariant &b) {
                    return a.toMap()[QStringLiteral("name")].toString()
                         < b.toMap()[QStringLiteral("name")].toString();
                });
            emit cloudModelChoicesChanged();
        });
        return;
    }

    if (m_llmProvider == QStringLiteral("gemini")) {
        // Gemini Cloud Code doesn't have a models list endpoint — use hardcoded
        m_cloudModelChoices = {
            QVariantMap{{QStringLiteral("id"), QStringLiteral("gemini-2.5-flash")},
                        {QStringLiteral("name"), QStringLiteral("Gemini 2.5 Flash")}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("gemini-2.5-pro")},
                        {QStringLiteral("name"), QStringLiteral("Gemini 2.5 Pro")}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("gemini-2.0-flash")},
                        {QStringLiteral("name"), QStringLiteral("Gemini 2.0 Flash")}},
        };
        emit cloudModelChoicesChanged();
    }
}

void JarvisSettings::fetchOllamaModels()
{
    if (m_llmProvider != QStringLiteral("ollama")) return;

    const QUrl url(m_llmServerUrl + QStringLiteral("/api/tags"));
    QNetworkRequest request(url);
    request.setTransferTimeout(5000);

    auto *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;

        const auto doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isNull()) return;

        const auto models = doc.object()[QStringLiteral("models")].toArray();
        m_availableLlmModels.clear();

        for (const auto &m : models) {
            const auto obj = m.toObject();
            const QString name = obj[QStringLiteral("name")].toString();
            const qint64 size = obj[QStringLiteral("size")].toVariant().toLongLong();
            const QString sizeStr = QStringLiteral("%1 GB").arg(size / 1073741824.0, 0, 'f', 1);

            m_availableLlmModels.append(QVariantMap{
                {QStringLiteral("id"), name},
                {QStringLiteral("name"), name},
                {QStringLiteral("size"), sizeStr},
                {QStringLiteral("desc"), QStringLiteral("Installed in Ollama")},
                {QStringLiteral("downloaded"), true},
                {QStringLiteral("active"), name == m_llmModelId},
            });
        }
        emit availableLlmModelsChanged();
    });
}

void JarvisSettings::searchHuggingFaceModels(const QString &query)
{
    m_lastHfQuery = query;
    const QString searchQuery = query.isEmpty()
        ? QStringLiteral("gguf instruct")
        : QStringLiteral("gguf %1").arg(query);

    const QString encodedQuery = QString(searchQuery).replace(QLatin1Char(' '), QLatin1Char('+'));
    const QByteArray searchUrlBytes = QStringLiteral(
        "https://huggingface.co/api/models?search=%1&sort=downloads&direction=-1&limit=20"
        "&expand[]=siblings&expand[]=downloads&expand[]=tags&expand[]=pipeline_tag"
    ).arg(encodedQuery).toUtf8();

    QNetworkRequest request{QUrl::fromEncoded(searchUrlBytes)};
    request.setTransferTimeout(15000);

    auto *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;

        const QByteArray data = reply->readAll();
        const QJsonArray models = QJsonDocument::fromJson(data).array();

        // Build list of installed model names for filtering
        QStringList installed;
        for (const auto &v : std::as_const(m_availableLlmModels)) {
            const QString id = v.toMap()[QStringLiteral("id")].toString().toLower();
            installed << id << id.split(':').first();
        }

        m_hfSearchResults.clear();
        for (const auto &val : models) {
            const QJsonObject obj = val.toObject();
            QString modelId = obj[QStringLiteral("modelId")].toString();
            if (modelId.isEmpty())
                modelId = obj[QStringLiteral("id")].toString();

            // Skip if already installed (check repo name against installed model names)
            const QString repoName = modelId.section('/', -1).toLower();
            bool alreadyInstalled = false;
            for (const auto &inst : installed) {
                if (inst.contains(repoName.left(repoName.indexOf('-')))
                    || repoName.contains(inst.split(':').first())) {
                    alreadyInstalled = true;
                    break;
                }
            }
            if (alreadyInstalled) continue;
            const qint64 downloads = obj[QStringLiteral("downloads")].toVariant().toLongLong();
            const QString pipelineTag = obj[QStringLiteral("pipeline_tag")].toString();

            // Extract useful info from tags
            QString license;
            QStringList languages;
            for (const auto &t : obj[QStringLiteral("tags")].toArray()) {
                const QString tag = t.toString();
                if (tag.startsWith(QStringLiteral("license:")))
                    license = tag.mid(8);
                else if (tag.length() == 2 && tag[0].isLower() && tag[1].isLower())
                    languages.append(tag); // 2-letter language codes
            }

            // Format download count
            QString dlStr;
            if (downloads >= 1000000)
                dlStr = QStringLiteral("%1M").arg(downloads / 1000000.0, 0, 'f', 1);
            else if (downloads >= 1000)
                dlStr = QStringLiteral("%1K").arg(downloads / 1000.0, 0, 'f', 1);
            else
                dlStr = QString::number(downloads);

            // Build a friendly base name from modelId
            QString baseName = modelId.section('/', -1);
            baseName.remove(QStringLiteral("-GGUF"), Qt::CaseInsensitive);
            baseName.remove(QStringLiteral("_GGUF"), Qt::CaseInsensitive);
            baseName.replace('-', ' ');
            baseName.replace('_', ' ');

            // Create one card per quantization (group split GGUF shards)
            const QJsonArray siblings = obj[QStringLiteral("siblings")].toArray();
            // Match quant tag before .gguf or before -00001-of-NNNNN.gguf
            static const QRegularExpression quantRe(
                QStringLiteral("[._-]((?:I?Q[0-9]+(?:_[A-Z0-9]+)*|[Ff]16|[Ff]32))(?:-\\d{5}-of-\\d{5})?\\.gguf$"),
                QRegularExpression::CaseInsensitiveOption);
            QMap<QString, QString> quantToFile; // quant -> first filename
            for (const auto &sib : siblings) {
                const QString fname = sib.toObject()[QStringLiteral("rfilename")].toString();
                if (!fname.endsWith(QStringLiteral(".gguf"))) continue;

                const auto qm = quantRe.match(fname);
                const QString quant = qm.hasMatch() ? qm.captured(1).toUpper() : QStringLiteral("default");
                if (!quantToFile.contains(quant))
                    quantToFile[quant] = fname;
            }

            // Common fields for all cards from this repo
            const QVariantMap common = {
                {QStringLiteral("id"), modelId},
                {QStringLiteral("downloads"), dlStr},
                {QStringLiteral("license"), license},
                {QStringLiteral("languages"), languages.join(QStringLiteral(", "))},
                {QStringLiteral("desc"), pipelineTag.isEmpty() ? QStringLiteral("GGUF model") : pipelineTag},
            };

            if (quantToFile.isEmpty()) {
                QVariantMap entry = common;
                entry[QStringLiteral("name")] = baseName;
                m_hfSearchResults.append(entry);
            } else {
                for (auto it = quantToFile.constBegin(); it != quantToFile.constEnd(); ++it) {
                    const QString &quant = it.key();
                    QVariantMap entry = common;
                    entry[QStringLiteral("name")] = quant == QStringLiteral("default")
                        ? baseName : QStringLiteral("%1 (%2)").arg(baseName, quant);
                    entry[QStringLiteral("file")] = it.value();
                    m_hfSearchResults.append(entry);
                }
            }
        }

        emit hfSearchResultsChanged();
    });
}

void JarvisSettings::fetchModelDetails(const QString &modelId)
{
    const QByteArray url = QStringLiteral("https://huggingface.co/api/models/%1").arg(modelId).toUtf8();
    QNetworkRequest request{QUrl::fromEncoded(url)};
    request.setTransferTimeout(10000);

    auto *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, modelId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;

        const auto doc = QJsonDocument::fromJson(reply->readAll());
        const auto obj = doc.object();
        const auto cardData = obj[QStringLiteral("cardData")].toObject();
        const auto gguf = obj[QStringLiteral("gguf")].toObject();

        // Parameter count
        const qint64 totalParams = gguf[QStringLiteral("total")].toVariant().toLongLong();
        QString paramStr;
        if (totalParams >= 1000000000)
            paramStr = QStringLiteral("%1B").arg(totalParams / 1000000000.0, 0, 'f', 1);
        else if (totalParams > 0)
            paramStr = QStringLiteral("%1M").arg(totalParams / 1000000.0, 0, 'f', 0);

        // License
        QString license = cardData[QStringLiteral("license")].toString();
        if (license.isEmpty()) {
            for (const auto &t : obj[QStringLiteral("tags")].toArray()) {
                const QString tag = t.toString();
                if (tag.startsWith(QStringLiteral("license:"))) {
                    license = tag.mid(8);
                    break;
                }
            }
        }

        // Tags — filter to useful ones
        QStringList tags;
        for (const auto &t : obj[QStringLiteral("tags")].toArray()) {
            const QString tag = t.toString();
            if (tag == QStringLiteral("gguf") || tag == QStringLiteral("transformers")
                || tag.startsWith(QStringLiteral("license:"))
                || tag.startsWith(QStringLiteral("base_model:"))
                || tag.startsWith(QStringLiteral("region:")))
                continue;
            // Keep language codes, architecture names, task types
            tags.append(tag);
        }

        m_modelDetails = QVariantMap{
            {QStringLiteral("id"), modelId},
            {QStringLiteral("params"), paramStr},
            {QStringLiteral("architecture"), gguf[QStringLiteral("architecture")].toString()},
            {QStringLiteral("contextLength"), gguf[QStringLiteral("context_length")].toVariant().toLongLong()},
            {QStringLiteral("license"), license},
            {QStringLiteral("author"), obj[QStringLiteral("author")].toString()},
            {QStringLiteral("baseModel"), cardData[QStringLiteral("base_model")].toString()},
            {QStringLiteral("likes"), obj[QStringLiteral("likes")].toVariant().toLongLong()},
            {QStringLiteral("tags"), tags.join(QStringLiteral(", "))},
            {QStringLiteral("url"), QStringLiteral("https://huggingface.co/%1").arg(modelId)},
        };
        emit modelDetailsChanged();

        // Fetch file sizes from tree API
        const QByteArray treeUrl = QStringLiteral(
            "https://huggingface.co/api/models/%1/tree/main").arg(modelId).toUtf8();
        QNetworkRequest treeReq{QUrl::fromEncoded(treeUrl)};
        treeReq.setTransferTimeout(10000);
        auto *treeReply = m_networkManager->get(treeReq);
        connect(treeReply, &QNetworkReply::finished, this, [this, treeReply, modelId]() {
            treeReply->deleteLater();
            if (treeReply->error() != QNetworkReply::NoError) return;
            if (m_modelDetails[QStringLiteral("id")].toString() != modelId) return;

            const auto treeArr = QJsonDocument::fromJson(treeReply->readAll()).array();
            QVariantMap fileSizes;
            for (const auto &f : treeArr) {
                const auto fObj = f.toObject();
                const QString path = fObj[QStringLiteral("path")].toString();
                if (!path.endsWith(QStringLiteral(".gguf"))) continue;
                const qint64 size = fObj[QStringLiteral("size")].toVariant().toLongLong();
                if (size > 0) {
                    fileSizes[path] = QStringLiteral("%1 GB").arg(size / 1073741824.0, 0, 'f', 1);
                }
            }
            m_modelDetails[QStringLiteral("fileSizes")] = fileSizes;
            emit modelDetailsChanged();
        });
    });
}

// ─────────────────────────────────────────────
// Model & Voice Lists
// ─────────────────────────────────────────────

void JarvisSettings::populateModelList()
{
    m_availableLlmModels.clear();

    const QString modelsDir = jarvisDataDir() + QStringLiteral("/models");
    QDir dir(modelsDir);
    dir.mkpath(QStringLiteral("."));

    const QStringList ggufFiles = dir.entryList({QStringLiteral("*.gguf")}, QDir::Files, QDir::Name);
    for (const QString &filename : ggufFiles) {
        const QString id = filename.chopped(5); // remove ".gguf"
        const QFileInfo fi(modelsDir + QStringLiteral("/") + filename);
        const double sizeGb = fi.size() / 1073741824.0;
        const QString sizeStr = QStringLiteral("%1 GB").arg(sizeGb, 0, 'f', 1);

        // Build a friendly name from the filename
        QString friendlyName = id;
        friendlyName.replace('-', ' ');
        friendlyName.replace('_', ' ');

        m_availableLlmModels.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), friendlyName},
            {QStringLiteral("size"), sizeStr},
            {QStringLiteral("downloaded"), true},
            {QStringLiteral("active"), (id == m_currentModelName || friendlyName.contains(m_currentModelName))},
            {QStringLiteral("desc"), QStringLiteral("Local GGUF model")},
        });
    }
}

void JarvisSettings::fetchPiperVoices(const QString &langFilter, const QString &qualityFilter)
{
    m_voiceLangFilter = langFilter;

    QNetworkRequest request{QUrl(QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/voices.json"))};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, langFilter, qualityFilter]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[JARVIS] Failed to fetch piper voices:" << reply->errorString();
            return;
        }

        const auto doc = QJsonDocument::fromJson(reply->readAll());
        const auto voices = doc.object();
        const QString voicesDir = jarvisDataDir() + QStringLiteral("/piper-voices");
        QDir().mkpath(voicesDir);

        m_availableTtsVoices.clear();

        const QString baseUrl = QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/");

        for (auto it = voices.begin(); it != voices.end(); ++it) {
            const auto v = it.value().toObject();
            const QString voiceId = v[QStringLiteral("key")].toString();
            const QString name = v[QStringLiteral("name")].toString();
            const QString quality = v[QStringLiteral("quality")].toString();
            const auto langObj = v[QStringLiteral("language")].toObject();
            const QString langFamily = langObj[QStringLiteral("family")].toString();
            const QString langCode = langObj[QStringLiteral("code")].toString();
            const QString langEnglish = langObj[QStringLiteral("name_english")].toString();
            const QString countryEnglish = langObj[QStringLiteral("country_english")].toString();

            // Apply filters
            if (!langFilter.isEmpty() && langFamily != langFilter) continue;
            if (!qualityFilter.isEmpty() && quality != qualityFilter) continue;

            // Find the .onnx file path and size
            QString onnxPath;
            qint64 sizeBytes = 0;
            const auto files = v[QStringLiteral("files")].toObject();
            for (auto fit = files.begin(); fit != files.end(); ++fit) {
                if (fit.key().endsWith(QStringLiteral(".onnx")) && !fit.key().endsWith(QStringLiteral(".onnx.json"))) {
                    onnxPath = fit.key();
                    sizeBytes = fit.value().toObject()[QStringLiteral("size_bytes")].toVariant().toLongLong();
                    break;
                }
            }
            if (onnxPath.isEmpty()) continue;

            // Build friendly name
            QString displayName = name;
            displayName.replace(QLatin1Char('_'), QLatin1Char(' '));
            if (!displayName.isEmpty()) displayName[0] = displayName[0].toUpper();

            const QString langDisplay = countryEnglish.isEmpty()
                ? langEnglish : langEnglish + QStringLiteral(" (") + countryEnglish + QStringLiteral(")");
            const QString sizeStr = sizeBytes > 0
                ? QStringLiteral("%1 MB").arg(sizeBytes / 1048576.0, 0, 'f', 0) : QString();

            const QString filename = voiceId + QStringLiteral(".onnx");
            const bool downloaded = QFile::exists(voicesDir + QStringLiteral("/") + filename);
            const bool active = (voiceId == m_currentVoiceName);

            m_availableTtsVoices.append(QVariantMap{
                {QStringLiteral("id"), voiceId},
                {QStringLiteral("name"), displayName},
                {QStringLiteral("lang"), langDisplay},
                {QStringLiteral("quality"), quality},
                {QStringLiteral("size"), sizeStr},
                {QStringLiteral("desc"), quality + QStringLiteral(" quality")},
                {QStringLiteral("url"), baseUrl + onnxPath},
                {QStringLiteral("urlJson"), baseUrl + onnxPath + QStringLiteral(".json")},
                {QStringLiteral("downloaded"), downloaded},
                {QStringLiteral("active"), active},
                {QStringLiteral("source"), QStringLiteral("official")},
            });
        }

        emit availableTtsVoicesChanged();

        // Also fetch community voices
        fetchCommunityVoices();
    });
}

void JarvisSettings::fetchCommunityVoices()
{
    // Search HuggingFace for community piper voice repos
    QNetworkRequest request{QUrl(QStringLiteral(
        "https://huggingface.co/api/models?search=piper+voice+onnx&sort=downloads&direction=-1&limit=30"))};
    request.setTransferTimeout(10000);

    auto *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;

        const auto models = QJsonDocument::fromJson(reply->readAll()).array();
        const QString voicesDir = jarvisDataDir() + QStringLiteral("/piper-voices");
        bool added = false;

        for (const auto &val : models) {
            const auto obj = val.toObject();
            const QString repoId = obj[QStringLiteral("modelId")].toString();

            // Skip the official repo and forks/mirrors
            if (repoId == QStringLiteral("rhasspy/piper-voices")) continue;
            if (repoId.endsWith(QStringLiteral("/piper-voices"))) continue;

            // Check siblings for .onnx files
            const auto siblings = obj[QStringLiteral("siblings")].toArray();
            for (const auto &sib : siblings) {
                const QString fname = sib.toObject()[QStringLiteral("rfilename")].toString();
                if (!fname.endsWith(QStringLiteral(".onnx")) || fname.endsWith(QStringLiteral(".onnx.json")))
                    continue;

                // Derive voice ID from filename
                QString voiceId = fname;
                voiceId = voiceId.section('/', -1); // take filename only
                voiceId.chop(5); // remove .onnx
                if (voiceId == QStringLiteral("model")) voiceId = repoId.section('/', -1);

                // Check if already in list
                bool duplicate = false;
                for (const auto &v : std::as_const(m_availableTtsVoices)) {
                    if (v.toMap()[QStringLiteral("id")].toString() == voiceId) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) continue;

                const QString baseUrl = QStringLiteral("https://huggingface.co/%1/resolve/main/").arg(repoId);
                const bool downloaded = QFile::exists(voicesDir + QStringLiteral("/") + voiceId + QStringLiteral(".onnx"));
                const bool active = (voiceId == m_currentVoiceName);

                // Try to build a friendly name
                QString displayName = voiceId;
                displayName.replace(QLatin1Char('-'), QLatin1Char(' '));
                displayName.replace(QLatin1Char('_'), QLatin1Char(' '));

                m_availableTtsVoices.append(QVariantMap{
                    {QStringLiteral("id"), voiceId},
                    {QStringLiteral("name"), displayName},
                    {QStringLiteral("lang"), QStringLiteral("Community")},
                    {QStringLiteral("quality"), QString()},
                    {QStringLiteral("size"), QString()},
                    {QStringLiteral("desc"), QStringLiteral("Community voice from %1").arg(repoId)},
                    {QStringLiteral("url"), baseUrl + fname},
                    {QStringLiteral("urlJson"), baseUrl + fname + QStringLiteral(".json")},
                    {QStringLiteral("downloaded"), downloaded},
                    {QStringLiteral("active"), active},
                    {QStringLiteral("source"), QStringLiteral("community")},
                });
                added = true;
            }
        }

        if (added) emit availableTtsVoicesChanged();
    });
}

// ─────────────────────────────────────────────
// Downloads
// ─────────────────────────────────────────────

void JarvisSettings::downloadLlmModel(const QString &modelId)
{
    if (m_downloading) return;

    // modelId is a HuggingFace model ID like "bartowski/Llama-3.2-3B-Instruct-GGUF"
    // Look up in HF search results first, then fall back to availableLlmModels (legacy)
    QVariantMap targetModel;
    for (const auto &v : std::as_const(m_hfSearchResults)) {
        auto map = v.toMap();
        if (map[QStringLiteral("id")].toString() == modelId) {
            targetModel = map;
            break;
        }
    }
    if (targetModel.isEmpty()) {
        for (const auto &v : std::as_const(m_availableLlmModels)) {
            auto map = v.toMap();
            if (map[QStringLiteral("id")].toString() == modelId) {
                targetModel = map;
                break;
            }
        }
    }
    if (targetModel.isEmpty()) return;

    // Construct GGUF download URL from HF model ID
    // Format: https://huggingface.co/{modelId}/resolve/main/{filename}.gguf
    // Prefer Q4_K_M quantization
    QString url = targetModel[QStringLiteral("url")].toString();
    if (url.isEmpty()) {
        // Derive the GGUF filename — take the repo name part and look for Q4_K_M
        const QString repoName = modelId.section('/', -1);
        QString baseName = repoName;
        baseName.remove(QStringLiteral("-GGUF"), Qt::CaseInsensitive);
        baseName.remove(QStringLiteral("_GGUF"), Qt::CaseInsensitive);
        const QString ggufFile = baseName + QStringLiteral("-Q4_K_M.gguf");
        url = QStringLiteral("https://huggingface.co/%1/resolve/main/%2").arg(modelId, ggufFile);
    }

    // Use a filesystem-safe name derived from the model ID
    const QString safeName = modelId.section('/', -1).remove(QStringLiteral("-GGUF"), Qt::CaseInsensitive)
        .remove(QStringLiteral("_GGUF"), Qt::CaseInsensitive) + QStringLiteral("-Q4_K_M");
    const QString modelsDir = jarvisDataDir() + QStringLiteral("/models");
    QDir().mkpath(modelsDir);
    const QString filePath = modelsDir + QStringLiteral("/") + safeName + QStringLiteral(".gguf");

    if (QFile::exists(filePath)) {
        setActiveLlmModel(safeName);
        return;
    }

    m_downloading = true;
    m_downloadProgress = 0.0;
    m_downloadStatus = QStringLiteral("Downloading ") + targetModel[QStringLiteral("name")].toString() + QStringLiteral("...");
    emit downloadingChanged();
    emit downloadProgressChanged();
    emit downloadStatusChanged();

    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_downloadReply = m_networkManager->get(request);

    auto *outFile = new QFile(filePath + QStringLiteral(".part"), this);
    outFile->open(QIODevice::WriteOnly);

    connect(m_downloadReply, &QNetworkReply::readyRead, this, [this, outFile]() {
        if (outFile->isOpen()) outFile->write(m_downloadReply->readAll());
    });

    connect(m_downloadReply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (total > 0) {
            m_downloadProgress = static_cast<double>(received) / total;
            m_downloadStatus = QStringLiteral("Downloading... %1 / %2 MB")
                .arg(received / 1048576).arg(total / 1048576);
            emit downloadProgressChanged();
            emit downloadStatusChanged();
        }
    });

    connect(m_downloadReply, &QNetworkReply::finished, this, [this, outFile, filePath, safeName]() {
        outFile->close();
        outFile->deleteLater();

        if (m_downloadReply->error() == QNetworkReply::NoError) {
            QFile::rename(filePath + QStringLiteral(".part"), filePath);
            m_downloadStatus = QStringLiteral("Download complete!");
            setActiveLlmModel(safeName);
            populateModelList();
        } else {
            QFile::remove(filePath + QStringLiteral(".part"));
            m_downloadStatus = QStringLiteral("Download failed: ") + m_downloadReply->errorString();
        }

        m_downloading = false;
        m_downloadReply = nullptr;
        emit downloadingChanged();
        emit downloadStatusChanged();
    });
}

void JarvisSettings::downloadTtsVoice(const QString &voiceId)
{
    if (m_downloading) return;

    QVariantMap targetVoice;
    for (const auto &v : std::as_const(m_availableTtsVoices)) {
        auto map = v.toMap();
        if (map[QStringLiteral("id")].toString() == voiceId) {
            targetVoice = map;
            break;
        }
    }
    if (targetVoice.isEmpty()) return;

    const QString voicesDir = jarvisDataDir() + QStringLiteral("/piper-voices");
    QDir().mkpath(voicesDir);
    const QString onnxPath = voicesDir + QStringLiteral("/") + voiceId + QStringLiteral(".onnx");
    const QString jsonPath = onnxPath + QStringLiteral(".json");

    if (QFile::exists(onnxPath) && QFile::exists(jsonPath)) {
        setActiveTtsVoice(voiceId);
        return;
    }

    m_downloading = true;
    m_downloadProgress = 0.0;
    m_downloadStatus = QStringLiteral("Downloading voice: ") + targetVoice[QStringLiteral("name")].toString() + QStringLiteral("...");
    emit downloadingChanged();
    emit downloadProgressChanged();
    emit downloadStatusChanged();

    QNetworkRequest request{QUrl(targetVoice[QStringLiteral("url")].toString())};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_downloadReply = m_networkManager->get(request);

    auto *outFile = new QFile(onnxPath + QStringLiteral(".part"), this);
    outFile->open(QIODevice::WriteOnly);

    connect(m_downloadReply, &QNetworkReply::readyRead, this, [this, outFile]() {
        if (outFile->isOpen()) outFile->write(m_downloadReply->readAll());
    });

    connect(m_downloadReply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (total > 0) {
            m_downloadProgress = static_cast<double>(received) / total;
            m_downloadStatus = QStringLiteral("Downloading voice... %1 / %2 MB")
                .arg(received / 1048576).arg(total / 1048576);
            emit downloadProgressChanged();
            emit downloadStatusChanged();
        }
    });

    connect(m_downloadReply, &QNetworkReply::finished, this, [this, outFile, onnxPath, jsonPath, targetVoice, voiceId]() {
        outFile->close();
        outFile->deleteLater();

        if (m_downloadReply->error() == QNetworkReply::NoError) {
            QFile::rename(onnxPath + QStringLiteral(".part"), onnxPath);

            QNetworkRequest jsonReq{QUrl(targetVoice[QStringLiteral("urlJson")].toString())};
            jsonReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
            auto *jsonReply = m_networkManager->get(jsonReq);
            connect(jsonReply, &QNetworkReply::finished, this, [this, jsonReply, jsonPath, voiceId]() {
                if (jsonReply->error() == QNetworkReply::NoError) {
                    QFile jf(jsonPath);
                    if (jf.open(QIODevice::WriteOnly)) {
                        jf.write(jsonReply->readAll());
                        jf.close();
                    }
                }
                jsonReply->deleteLater();
                m_downloadStatus = QStringLiteral("Voice downloaded!");
                setActiveTtsVoice(voiceId);
                fetchPiperVoices(m_voiceLangFilter);
                m_downloading = false;
                m_downloadReply = nullptr;
                emit downloadingChanged();
                emit downloadStatusChanged();
            });
        } else {
            QFile::remove(onnxPath + QStringLiteral(".part"));
            m_downloadStatus = QStringLiteral("Download failed: ") + m_downloadReply->errorString();
            m_downloading = false;
            m_downloadReply = nullptr;
            emit downloadingChanged();
            emit downloadStatusChanged();
        }
    });
}

void JarvisSettings::setActiveLlmModel(const QString &modelId)
{
    m_currentModelName = modelId;
    saveSettings();
    populateModelList();
    emit currentModelNameChanged();
}

void JarvisSettings::setActiveTtsVoice(const QString &voiceId)
{
    const QString voicesDir = jarvisDataDir() + QStringLiteral("/piper-voices");
    const QString onnxPath = voicesDir + QStringLiteral("/") + voiceId + QStringLiteral(".onnx");

    if (QFile::exists(onnxPath)) {
        m_piperModelPath = onnxPath;
        m_currentVoiceName = voiceId;
        saveSettings();
        fetchPiperVoices(m_voiceLangFilter);
        emit currentVoiceNameChanged();
        emit voiceActivated(voiceId, onnxPath);
    }
}

void JarvisSettings::cancelDownload()
{
    if (m_downloadReply) {
        m_downloadReply->abort();
        m_downloadReply = nullptr;
    }
    m_downloading = false;
    m_downloadProgress = 0.0;
    m_downloadStatus = QStringLiteral("Download cancelled.");
    emit downloadingChanged();
    emit downloadProgressChanged();
    emit downloadStatusChanged();
}
