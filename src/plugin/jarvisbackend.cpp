#include "jarvisbackend.h"
#include "settings/jarvissettings.h"
#include "tts/jarvisTts.h"
#include "audio/jarvisaudio.h"
#include "system/jarvissystem.h"
#include "commands/jarviscommands.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QDebug>

// ─────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────

JarvisBackend::JarvisBackend(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_healthCheckTimer(new QTimer(this))
    , m_reminderTimer(new QTimer(this))
{
    // Create modules
    m_settings = new JarvisSettings(m_networkManager, this);
    m_tts = new JarvisTts(m_settings, this);
    m_audio = new JarvisAudio(m_settings, this);
    m_system = new JarvisSystem(this);
    m_commands = new JarvisCommands(this);

    connectModuleSignals();

    // Load persisted continuous mode
    m_continuousMode = m_settings->continuousMode();

    // Health check — every 10s
    connect(m_healthCheckTimer, &QTimer::timeout, this, &JarvisBackend::checkConnection);
    m_healthCheckTimer->start(10000);
    checkConnection();

    // Conversation timeout — end continuous conversation after 30s of inactivity
    m_conversationTimeout = new QTimer(this);
    m_conversationTimeout->setSingleShot(true);
    m_conversationTimeout->setInterval(30000);
    connect(m_conversationTimeout, &QTimer::timeout, this, [this]() {
        if (m_conversationActive) {
            stopConversation();
            speak(QStringLiteral("Conversation timed out. I'll be here if you need me."));
        }
    });

    // Reminder check — every 1s
    connect(m_reminderTimer, &QTimer::timeout, this, &JarvisBackend::checkReminders);
    m_reminderTimer->start(1000);

    loadChatHistory();
    setStatus("Online. All systems operational.");
}

JarvisBackend::~JarvisBackend() = default;

void JarvisBackend::connectModuleSignals()
{
    // Audio → Backend
    connect(m_audio, &JarvisAudio::listeningChanged, this, &JarvisBackend::listeningChanged);
    connect(m_audio, &JarvisAudio::wakeWordActiveChanged, this, &JarvisBackend::wakeWordActiveChanged);
    connect(m_audio, &JarvisAudio::audioLevelChanged, this, &JarvisBackend::audioLevelChanged);
    connect(m_audio, &JarvisAudio::voiceCommandModeChanged, this, &JarvisBackend::voiceCommandModeChanged);
    connect(m_audio, &JarvisAudio::lastTranscriptionChanged, this, &JarvisBackend::lastTranscriptionChanged);
    connect(m_audio, &JarvisAudio::wakeWordDetected, this, [this]() {
        emit wakeWordDetected();
        setStatus("Wake word detected! Listening...");
        if (m_continuousMode) m_conversationActive = true;
    });
    connect(m_audio, &JarvisAudio::voiceCommandTranscribed, this, &JarvisBackend::onVoiceCommandTranscribed);
    connect(m_audio, &JarvisAudio::micBusyChanged, this, [this](bool busy) {
        emit micBusyChanged();
        if (busy) setStatus("Mic in use by another app — wake word paused.");
        else setStatus("Mic free — wake word active.");
    });

    // TTS → Backend
    connect(m_tts, &JarvisTts::speakingChanged, this, [this]() {
        emit speakingChanged();
        // Mute wake word detection while TTS is playing to prevent echo
        m_audio->setTtsSpeaking(m_tts->isSpeaking());
        // Continuous conversation: re-enter voice command mode after TTS finishes
        if (!m_tts->isSpeaking() && m_conversationActive
            && !m_processing && !m_audio->isVoiceCommandMode()) {
            QTimer::singleShot(300, this, [this]() {
                // Double-check everything is truly idle before re-entering
                if (m_conversationActive && !m_processing
                    && !m_tts->isSpeaking() && !m_audio->isVoiceCommandMode()
                    && !m_streamReply) {
                    m_audio->startVoiceCommand();
                }
            });
        }
    });

    // System → Backend
    connect(m_system, &JarvisSystem::systemStatsChanged, this, &JarvisBackend::systemStatsChanged);
    connect(m_system, &JarvisSystem::currentTimeChanged, this, &JarvisBackend::currentTimeChanged);

    // Settings → Backend
    connect(m_settings, &JarvisSettings::llmProviderChanged, this, [this]() {
        // Cancel any in-flight LLM request from the previous provider
        if (m_streamReply) {
            m_streamReply->abort();
            m_streamReply->deleteLater();
            m_streamReply = nullptr;
            m_processing = false;
            emit processingChanged();
        }
        m_pendingOAuthMessage.clear();
        emit llmProviderChanged();
        checkConnection();
    });
    connect(m_settings, &JarvisSettings::llmServerUrlChanged, this, [this]() {
        emit llmServerUrlChanged();
        checkConnection();
    });
    connect(m_settings, &JarvisSettings::openaiApiKeyChanged, this, &JarvisBackend::openaiApiKeyChanged);
    connect(m_settings, &JarvisSettings::geminiApiKeyChanged, this, &JarvisBackend::geminiApiKeyChanged);
    connect(m_settings, &JarvisSettings::claudeApiKeyChanged, this, &JarvisBackend::claudeApiKeyChanged);
    connect(m_settings, &JarvisSettings::llmModelIdChanged, this, &JarvisBackend::llmModelIdChanged);
    connect(m_settings, &JarvisSettings::currentModelNameChanged, this, [this]() {
        emit currentModelNameChanged();
        setStatus(QStringLiteral("LLM model set to: ") + m_settings->currentModelName());
    });
    connect(m_settings, &JarvisSettings::currentVoiceNameChanged, this, [this]() {
        emit currentVoiceNameChanged();
        setStatus(QStringLiteral("Voice changed to: ") + m_settings->currentVoiceName());
    });
    connect(m_settings, &JarvisSettings::downloadProgressChanged, this, &JarvisBackend::downloadProgressChanged);
    connect(m_settings, &JarvisSettings::downloadingChanged, this, &JarvisBackend::downloadingChanged);
    connect(m_settings, &JarvisSettings::downloadStatusChanged, this, &JarvisBackend::downloadStatusChanged);
    connect(m_settings, &JarvisSettings::maxHistoryPairsChanged, this, &JarvisBackend::maxHistoryPairsChanged);
    connect(m_settings, &JarvisSettings::wakeBufferSecondsChanged, this, [this]() {
        m_audio->updateWakeBufferInterval(m_settings->wakeBufferSeconds());
        emit wakeBufferSecondsChanged();
    });
    connect(m_settings, &JarvisSettings::voiceCmdMaxSecondsChanged, this, [this]() {
        m_audio->updateVoiceCmdTimeout(m_settings->voiceCmdMaxSeconds());
        emit voiceCmdMaxSecondsChanged();
    });
    connect(m_settings, &JarvisSettings::silenceTimeoutMsChanged, this, [this]() {
        m_audio->updateSilenceTimeout(m_settings->silenceTimeoutMs());
        emit silenceTimeoutMsChanged();
    });
    connect(m_settings, &JarvisSettings::autoStartWakeWordChanged, this, &JarvisBackend::autoStartWakeWordChanged);
    connect(m_settings, &JarvisSettings::wakeWordChanged, this, &JarvisBackend::wakeWordChanged);
    connect(m_settings, &JarvisSettings::personalityPromptChanged, this, &JarvisBackend::personalityPromptChanged);
    connect(m_settings, &JarvisSettings::ttsRateChanged, this, [this]() { m_tts->onTtsRateChanged(); });
    connect(m_settings, &JarvisSettings::ttsPitchChanged, this, [this]() { m_tts->onTtsPitchChanged(); });
    connect(m_settings, &JarvisSettings::ttsVolumeChanged, this, [this]() { m_tts->onTtsVolumeChanged(); });
    connect(m_settings, &JarvisSettings::ttsMutedChanged, this, &JarvisBackend::ttsMutedChanged);
    connect(m_settings, &JarvisSettings::voiceActivated, m_tts, &JarvisTts::onVoiceActivated);
    connect(m_settings, &JarvisSettings::oauthStatusChanged, this, &JarvisBackend::oauthStatusChanged);
    connect(m_settings, &JarvisSettings::cloudModelChoicesChanged, this, &JarvisBackend::cloudModelChoicesChanged);

    // OAuth token refresh → retry pending message
    connect(m_settings, &JarvisSettings::oauthTokenReady, this, [this]() {
        if (!m_pendingOAuthMessage.isEmpty()) {
            const QString msg = m_pendingOAuthMessage;
            m_pendingOAuthMessage.clear();
            qDebug() << "[JARVIS] OAuth token ready, retrying pending message";
            sendToLlm(msg);
        }
    });
    connect(m_settings, &JarvisSettings::oauthTokenError, this, [this](const QString &error) {
        if (!m_pendingOAuthMessage.isEmpty()) {
            m_pendingOAuthMessage.clear();
            m_processing = false;
            emit processingChanged();
            setStatus(QStringLiteral("OAuth error: ") + error);
            emit errorOccurred(QStringLiteral("OAuth authentication failed: ") + error);
        }
    });

    // Commands → Backend
    connect(m_commands, &JarvisCommands::commandMappingsChanged, this, &JarvisBackend::commandMappingsChanged);
    connect(m_commands, &JarvisCommands::commandExecuted, this, [this](const QString &phrase, const QString &action) {
        Q_UNUSED(action)
        setStatus(QStringLiteral("Executing: ") + phrase);
        addToChatHistory("jarvis", QStringLiteral("Executing command: %1").arg(phrase));
    });
    connect(m_commands, &JarvisCommands::commandOutput, this, [this](const QString &output) {
        addToChatHistory("jarvis", output);
    });
}

// ─────────────────────────────────────────────
// Delegated getters
// ─────────────────────────────────────────────

bool JarvisBackend::isListening() const { return m_audio->isListening(); }
bool JarvisBackend::isWakeWordActive() const { return m_audio->isWakeWordActive(); }
bool JarvisBackend::isMicBusy() const { return m_audio->isMicBusy(); }
double JarvisBackend::audioLevel() const { return m_audio->audioLevel(); }
bool JarvisBackend::isSpeaking() const { return m_tts->isSpeaking(); }
bool JarvisBackend::isTtsMuted() const { return m_tts->isMuted(); }
bool JarvisBackend::isVoiceCommandMode() const { return m_audio->isVoiceCommandMode(); }
QString JarvisBackend::lastTranscription() const { return m_audio->lastTranscription(); }

double JarvisBackend::cpuUsage() const { return m_system->cpuUsage(); }
double JarvisBackend::memoryUsage() const { return m_system->memoryUsage(); }
double JarvisBackend::memoryTotalGb() const { return m_system->memoryTotalGb(); }
double JarvisBackend::memoryUsedGb() const { return m_system->memoryUsedGb(); }
int JarvisBackend::cpuTemp() const { return m_system->cpuTemp(); }
QString JarvisBackend::uptime() const { return m_system->uptime(); }
QString JarvisBackend::hostname() const { return m_system->hostname(); }
QString JarvisBackend::kernelVersion() const { return m_system->kernelVersion(); }
QString JarvisBackend::currentTime() const { return m_system->currentTime(); }
QString JarvisBackend::currentDate() const { return m_system->currentDate(); }
QString JarvisBackend::greeting() const { return m_system->greeting(); }

QString JarvisBackend::llmProvider() const { return m_settings->llmProvider(); }
QString JarvisBackend::llmServerUrl() const { return m_settings->llmServerUrl(); }
QString JarvisBackend::openaiApiKey() const { return m_settings->openaiApiKey(); }
QString JarvisBackend::geminiApiKey() const { return m_settings->geminiApiKey(); }
QString JarvisBackend::claudeApiKey() const { return m_settings->claudeApiKey(); }
QString JarvisBackend::llmModelId() const { return m_settings->llmModelId(); }
QString JarvisBackend::currentModelName() const { return m_settings->currentModelName(); }
QString JarvisBackend::currentVoiceName() const { return m_settings->currentVoiceName(); }
QVariantList JarvisBackend::availableLlmModels() const { return m_settings->availableLlmModels(); }
QVariantList JarvisBackend::cloudModelChoices() const { return m_settings->cloudModelChoices(); }
QVariantList JarvisBackend::availableTtsVoices() const { return m_settings->availableTtsVoices(); }
double JarvisBackend::downloadProgress() const { return m_settings->downloadProgress(); }
bool JarvisBackend::isDownloading() const { return m_settings->isDownloading(); }
QString JarvisBackend::downloadStatus() const { return m_settings->downloadStatus(); }
int JarvisBackend::maxHistoryPairs() const { return m_settings->maxHistoryPairs(); }
int JarvisBackend::wakeBufferSeconds() const { return m_settings->wakeBufferSeconds(); }
int JarvisBackend::voiceCmdMaxSeconds() const { return m_settings->voiceCmdMaxSeconds(); }
int JarvisBackend::silenceTimeoutMs() const { return m_settings->silenceTimeoutMs(); }
bool JarvisBackend::autoStartWakeWord() const { return m_settings->autoStartWakeWord(); }
QString JarvisBackend::wakeWord() const { return m_settings->wakeWord(); }
QString JarvisBackend::personalityPrompt() const { return m_settings->personalityPrompt(); }

QVariantList JarvisBackend::commandMappings() const { return m_commands->commandMappings(); }

// ─────────────────────────────────────────────
// Invokable delegates
// ─────────────────────────────────────────────

void JarvisBackend::speak(const QString &text) { m_tts->speak(text); }
void JarvisBackend::stopSpeaking() { m_tts->stop(); }
void JarvisBackend::toggleTtsMute() { m_tts->toggleMute(); }
void JarvisBackend::toggleWakeWord() { m_audio->toggleWakeWord(); }
void JarvisBackend::startVoiceCommand() { m_audio->startVoiceCommand(); }
void JarvisBackend::stopVoiceCommand() { m_audio->stopVoiceCommand(); }

void JarvisBackend::setTtsRate(double rate) { m_settings->setTtsRate(rate); }
void JarvisBackend::setTtsPitch(double pitch) { m_settings->setTtsPitch(pitch); }
void JarvisBackend::setTtsVolume(double volume) { m_settings->setTtsVolume(volume); }
void JarvisBackend::setLlmProvider(const QString &provider) { m_settings->setLlmProvider(provider); }
void JarvisBackend::setLlmServerUrl(const QString &url) { m_settings->setLlmServerUrl(url); }
void JarvisBackend::setOpenaiApiKey(const QString &key) { m_settings->setOpenaiApiKey(key); }
void JarvisBackend::setGeminiApiKey(const QString &key) { m_settings->setGeminiApiKey(key); }
void JarvisBackend::setClaudeApiKey(const QString &key) { m_settings->setClaudeApiKey(key); }
void JarvisBackend::setLlmModelId(const QString &modelId) { m_settings->setLlmModelId(modelId); }
void JarvisBackend::refreshOllamaModels()
{
    m_settings->fetchOllamaModels();
    emit availableLlmModelsChanged();
}

void JarvisBackend::refreshCloudModels()
{
    m_settings->fetchCloudModels();
}

void JarvisBackend::downloadLlmModel(const QString &modelId) { m_settings->downloadLlmModel(modelId); }
void JarvisBackend::downloadTtsVoice(const QString &voiceId) { m_settings->downloadTtsVoice(voiceId); }
void JarvisBackend::setActiveLlmModel(const QString &modelId) { m_settings->setActiveLlmModel(modelId); }
void JarvisBackend::setActiveTtsVoice(const QString &voiceId) { m_settings->setActiveTtsVoice(voiceId); }
void JarvisBackend::setMaxHistoryPairs(int pairs) { m_settings->setMaxHistoryPairs(pairs); }
void JarvisBackend::setWakeBufferSeconds(int seconds) { m_settings->setWakeBufferSeconds(seconds); }
void JarvisBackend::setVoiceCmdMaxSeconds(int seconds) { m_settings->setVoiceCmdMaxSeconds(seconds); }
void JarvisBackend::setSilenceTimeoutMs(int ms) { m_settings->setSilenceTimeoutMs(ms); }
void JarvisBackend::setAutoStartWakeWord(bool enabled) { m_settings->setAutoStartWakeWord(enabled); }
void JarvisBackend::setWakeWord(const QString &word) { m_settings->setWakeWord(word); }

void JarvisBackend::setContinuousMode(bool enabled)
{
    m_settings->setContinuousMode(enabled);
    m_continuousMode = enabled;
    emit continuousModeChanged();
    if (!enabled) stopConversation();
}

void JarvisBackend::stopConversation()
{
    if (m_conversationActive) {
        m_conversationActive = false;
        m_emptyTranscriptionCount = 0;
        if (m_conversationTimeout) m_conversationTimeout->stop();
        emit conversationActiveChanged();
        setStatus("Conversation ended.");
    }
}

void JarvisBackend::setPersonalityPrompt(const QString &prompt) { m_settings->setPersonalityPrompt(prompt); }
void JarvisBackend::cancelDownload() { m_settings->cancelDownload(); }

void JarvisBackend::testVoice(const QString &voiceId)
{
    const QString voicesDir = m_settings->jarvisDataDir() + QStringLiteral("/piper-voices");
    const QString onnxPath = voicesDir + QStringLiteral("/") + voiceId + QStringLiteral(".onnx");
    if (!QFile::exists(onnxPath)) {
        qWarning() << "[JARVIS] testVoice: onnx not found:" << onnxPath;
        return;
    }

    QString piperBin;
    for (const auto &path : {"/usr/lib/piper-tts/bin/piper", "/usr/bin/piper", "/usr/local/bin/piper"}) {
        if (QFile::exists(QString::fromLatin1(path))) { piperBin = QString::fromLatin1(path); break; }
    }
    if (piperBin.isEmpty()) {
        qWarning() << "[JARVIS] testVoice: piper binary not found";
        return;
    }

    // Stop any current speech first
    m_tts->stop();

    const QString wavPath = QDir::tempPath() + QStringLiteral("/jarvis_voice_test.wav");

    // Use printf to avoid shell quoting issues with echo
    QString cmd = QStringLiteral(
        "printf '%s' 'All systems are nominal.' | "
        "'%1' -m '%2' -f '%3' --sentence-silence 0.3 2>/dev/null && "
        "pw-play '%3' 2>/dev/null; rm -f '%3'"
    ).arg(piperBin, onnxPath, wavPath);

    qDebug() << "[JARVIS] testVoice cmd:" << cmd;

    auto *proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [proc](int exitCode, QProcess::ExitStatus) {
        if (exitCode != 0) {
            qWarning() << "[JARVIS] testVoice failed, exit:" << exitCode
                       << "stderr:" << QString::fromUtf8(proc->readAllStandardError());
        }
        proc->deleteLater();
    });
    proc->setProcessChannelMode(QProcess::MergedChannels);
    proc->start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), cmd});
}

void JarvisBackend::fetchMoreModels()
{
    m_settings->fetchMoreModels();
    emit availableLlmModelsChanged();
}

void JarvisBackend::fetchMoreVoices()
{
    m_settings->fetchMoreVoices();
    emit availableTtsVoicesChanged();
}

// OAuth delegated methods
bool JarvisBackend::isOAuthLoggedIn() { return m_settings->isOAuthLoggedIn(); }
void JarvisBackend::oauthLogin(const QString &provider) { m_settings->oauthLogin(provider); }
void JarvisBackend::oauthLogout(const QString &provider) { m_settings->oauthLogout(provider); }
void JarvisBackend::cancelOAuthLogin() { m_settings->cancelOAuthLogin(); }
void JarvisBackend::completeClaudeLogin(const QString &code) { m_settings->completeClaudeLogin(code); }
bool JarvisBackend::awaitingClaudeCode() const { return m_settings->awaitingClaudeCode(); }

void JarvisBackend::addCommand(const QString &phrase, const QString &action, const QString &type) { m_commands->addCommand(phrase, action, type); }
void JarvisBackend::removeCommand(int index) { m_commands->removeCommand(index); }
void JarvisBackend::updateCommand(int index, const QString &phrase, const QString &action, const QString &type) { m_commands->updateCommand(index, phrase, action, type); }
void JarvisBackend::resetCommandsToDefaults() { m_commands->resetToDefaults(); }

void JarvisBackend::openUrl(const QString &url)
{
    QProcess::startDetached(QStringLiteral("xdg-open"), {url});
}

// ─────────────────────────────────────────────
// Voice Command Processing
// ─────────────────────────────────────────────

void JarvisBackend::onVoiceCommandTranscribed(const QString &text)
{
    if (text.isEmpty()) {
        if (m_conversationActive) {
            ++m_emptyTranscriptionCount;
            if (m_emptyTranscriptionCount >= 2) {
                stopConversation();
                return;
            }
            // Re-listen silently
            m_audio->startVoiceCommand();
        } else {
            setStatus("I couldn't make out what you said.");
        }
        return;
    }

    m_emptyTranscriptionCount = 0;

    qDebug() << "[JARVIS] Voice command transcribed:" << text;

    // Check for conversation exit phrases
    if (m_conversationActive) {
        static const QStringList exitPhrases = {
            QStringLiteral("stop"), QStringLiteral("goodbye"), QStringLiteral("that's all"),
            QStringLiteral("never mind"), QStringLiteral("thank you"), QStringLiteral("thanks"),
            QStringLiteral("dismiss"), QStringLiteral("bye"),
        };
        const QString lower = text.toLower().trimmed();
        for (const auto &phrase : exitPhrases) {
            if (lower == phrase || lower == phrase + QStringLiteral(".")) {
                stopConversation();
                speak(QStringLiteral("Very well. I'll be here if you need me."));
                return;
            }
        }
        // Reset conversation timeout
        if (m_conversationTimeout) m_conversationTimeout->start();
    }

    // Remove wake word from transcription if present
    const QString wakeWord = m_settings->wakeWord();
    QString command = text;
    command.remove(QRegularExpression(
        QStringLiteral("^\\s*%1[,\\s]*").arg(QRegularExpression::escape(wakeWord)),
        QRegularExpression::CaseInsensitiveOption));
    command = command.trimmed();

    if (command.isEmpty()) {
        setStatus("Yes? I'm listening.");
        speak(QStringLiteral("Yes?"));
        return;
    }

    // Try system commands first
    if (m_commands->tryExecuteVoiceCommand(command)) {
        speak(QStringLiteral("Right away."));
        return;
    }

    // Otherwise send to LLM
    sendMessage(command);
}

// ─────────────────────────────────────────────
// LLM Communication
// ─────────────────────────────────────────────

void JarvisBackend::sendMessage(const QString &message)
{
    if (message.trimmed().isEmpty()) return;

    addToChatHistory("user", message);
    sendToLlm(message);
}

void JarvisBackend::sendToLlm(const QString &userMessage)
{
    m_processing = true;
    emit processingChanged();
    setStatus("Processing...");

    m_conversationHistory.push_back({QStringLiteral("user"), userMessage});

    const int maxPairs = m_settings->maxHistoryPairs();
    while (m_conversationHistory.size() > static_cast<size_t>(maxPairs * 2)) {
        m_conversationHistory.erase(m_conversationHistory.begin());
    }

    // Reset streaming state
    m_streamBuffer.clear();
    m_fullStreamedResponse.clear();
    m_spokenSoFar.clear();
    m_streamingResponse.clear();
    emit streamingResponseChanged();

    // Build request based on provider
    QJsonObject requestBody;
    QUrl url;
    QNetworkRequest request;

    if (m_settings->isGeminiOAuthMode()) {
        // Gemini Cloud Code Assist API — different endpoint, format, and auth
        const QString apiKey = m_settings->activeApiKey();
        if (apiKey.isEmpty()) {
            if (m_settings->hasOAuthCredentials()) {
                m_pendingOAuthMessage = userMessage;
                setStatus("Refreshing authentication token...");
                m_settings->ensureOAuthToken();
                return;
            }
            m_processing = false;
            emit processingChanged();
            setStatus("No credentials. Please log in with Google.");
            emit errorOccurred("No Gemini credentials. Please log in.");
            return;
        }

        // Build Cloud Code request format
        QJsonArray contents;
        // System instruction
        const QString sysPrompt = buildSystemPrompt();
        QJsonObject systemInstruction{
            {QStringLiteral("role"), QStringLiteral("user")},
            {QStringLiteral("parts"), QJsonArray{QJsonObject{{QStringLiteral("text"), sysPrompt}}}},
        };
        // Conversation contents
        for (const auto &msg : m_conversationHistory) {
            const QString role = (msg.role == QStringLiteral("assistant")) ? QStringLiteral("model") : QStringLiteral("user");
            contents.append(QJsonObject{
                {QStringLiteral("role"), role},
                {QStringLiteral("parts"), QJsonArray{QJsonObject{{QStringLiteral("text"), msg.content}}}},
            });
        }

        const QString modelId = m_settings->llmModelId().isEmpty()
            ? QStringLiteral("gemini-2.5-flash") : m_settings->llmModelId();

        requestBody = QJsonObject{
            {QStringLiteral("model"), modelId},
            {QStringLiteral("project"), m_settings->geminiProjectId()},
            {QStringLiteral("user_prompt_id"), QStringLiteral("jarvis-%1").arg(QDateTime::currentMSecsSinceEpoch())},
            {QStringLiteral("request"), QJsonObject{
                {QStringLiteral("contents"), contents},
                {QStringLiteral("systemInstruction"), systemInstruction},
                {QStringLiteral("generationConfig"), QJsonObject{
                    {QStringLiteral("maxOutputTokens"), 2048},
                    {QStringLiteral("temperature"), 0.7},
                }},
            }},
        };

        url = QUrl(m_settings->geminiCloudCodeUrl() + QStringLiteral(":streamGenerateContent"));
        request.setUrl(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setTransferTimeout(120000);
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(apiKey).toUtf8());

    } else {
        // Standard OpenAI-compatible / Claude API
        QJsonArray messages = buildConversationContext();
        requestBody[QStringLiteral("messages")] = messages;
        requestBody[QStringLiteral("temperature")] = 0.7;
        requestBody[QStringLiteral("max_tokens")] = 2048;
        requestBody[QStringLiteral("stream")] = true;

        if (m_settings->providerNeedsModelInRequest()) {
            QString modelId = m_settings->llmModelId();
            if (modelId.isEmpty() && m_settings->isClaudeProvider())
                modelId = QStringLiteral("claude-sonnet-4-20250514");
            if (modelId.isEmpty()) {
                m_processing = false;
                emit processingChanged();
                setStatus("No model selected. Please choose a model in settings.");
                emit errorOccurred("No model selected.");
                if (!m_conversationHistory.empty()) m_conversationHistory.pop_back();
                return;
            }
            requestBody[QStringLiteral("model")] = modelId;
        }

        if (m_settings->isClaudeProvider()) {
            requestBody[QStringLiteral("system")] = buildSystemPrompt();
        }

        url = QUrl(m_settings->chatCompletionsUrl());
        request.setUrl(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setTransferTimeout(120000);

        if (m_settings->providerNeedsApiKey()) {
            const QString apiKey = m_settings->activeApiKey();
            if (apiKey.isEmpty()) {
                if (m_settings->hasOAuthCredentials()) {
                    m_pendingOAuthMessage = userMessage;
                    setStatus("Refreshing authentication token...");
                    m_settings->ensureOAuthToken();
                    return;
                }
                m_processing = false;
                emit processingChanged();
                setStatus("No API key configured for this provider.");
                emit errorOccurred("No API key configured. Please set one in settings.");
                return;
            }
            if (m_settings->isClaudeProvider()) {
                // OAuth tokens (sk-ant-oat*) use Bearer auth + beta header; API keys use x-api-key
                if (apiKey.startsWith(QStringLiteral("sk-ant-oat"))) {
                    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(apiKey).toUtf8());
                    request.setRawHeader("anthropic-beta", "oauth-2025-04-20");
                } else {
                    request.setRawHeader("x-api-key", apiKey.toUtf8());
                }
                request.setRawHeader("anthropic-version", "2023-06-01");
            } else {
                request.setRawHeader("Authorization",
                    QStringLiteral("Bearer %1").arg(apiKey).toUtf8());
            }
        }
    }

    m_streamReply = m_networkManager->post(request, QJsonDocument(requestBody).toJson());
    connect(m_streamReply, &QNetworkReply::readyRead, this, &JarvisBackend::onLlmStreamReadyRead);
    connect(m_streamReply, &QNetworkReply::finished, this, &JarvisBackend::onLlmStreamFinished);
}

QString JarvisBackend::buildSystemPrompt() const
{
    QString systemPrompt = m_settings->personalityPrompt().isEmpty()
        ? QString::fromUtf8(JARVIS_SYSTEM_PROMPT) : m_settings->personalityPrompt();
    systemPrompt += QStringLiteral("\n\nCurrent system status:\n");
    systemPrompt += QStringLiteral("- CPU Usage: %1%\n").arg(m_system->cpuUsage(), 0, 'f', 1);
    systemPrompt += QStringLiteral("- Memory: %1 / %2 GB (%3%)\n")
                        .arg(m_system->memoryUsedGb(), 0, 'f', 1)
                        .arg(m_system->memoryTotalGb(), 0, 'f', 1)
                        .arg(m_system->memoryUsage(), 0, 'f', 1);
    systemPrompt += QStringLiteral("- CPU Temperature: %1°C\n").arg(m_system->cpuTemp());
    systemPrompt += QStringLiteral("- Uptime: %1\n").arg(m_system->uptime());
    systemPrompt += QStringLiteral("- Hostname: %1\n").arg(m_system->hostname());
    systemPrompt += QStringLiteral("- Current time: %1 %2\n").arg(m_system->currentTime(), m_system->currentDate());

    if (!m_reminders.empty()) {
        systemPrompt += QStringLiteral("- Active reminders: %1\n").arg(m_reminders.size());
    }
    return systemPrompt;
}

QJsonArray JarvisBackend::buildConversationContext() const
{
    QJsonArray messages;

    // For Claude, the system prompt is top-level (not in messages array)
    if (!m_settings->isClaudeProvider()) {
        QJsonObject systemMsg;
        systemMsg[QStringLiteral("role")] = QStringLiteral("system");
        systemMsg[QStringLiteral("content")] = buildSystemPrompt();
        messages.append(systemMsg);
    }

    for (const auto &[role, content] : m_conversationHistory) {
        QJsonObject msg;
        msg[QStringLiteral("role")] = role;
        msg[QStringLiteral("content")] = content;
        messages.append(msg);
    }

    return messages;
}

QString JarvisBackend::extractStreamToken(const QString &jsonStr) const
{
    const auto doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    if (doc.isNull()) return {};

    const auto obj = doc.object();

    // Gemini Cloud Code format: {"response":{"candidates":[{"content":{"parts":[{"text":"..."}]}}]}}
    if (m_settings->isGeminiOAuthMode()) {
        const auto response = obj[QStringLiteral("response")].toObject();
        const auto candidates = response[QStringLiteral("candidates")].toArray();
        if (candidates.isEmpty()) return {};
        const auto parts = candidates[0].toObject()
            [QStringLiteral("content")].toObject()
            [QStringLiteral("parts")].toArray();
        if (parts.isEmpty()) return {};
        return parts[0].toObject()[QStringLiteral("text")].toString();
    }

    // Claude format: {"type":"content_block_delta","delta":{"type":"text_delta","text":"token"}}
    if (m_settings->isClaudeProvider()) {
        const QString type = obj[QStringLiteral("type")].toString();
        if (type == QStringLiteral("content_block_delta")) {
            return obj[QStringLiteral("delta")].toObject()
                      [QStringLiteral("text")].toString();
        }
        return {};
    }

    // OpenAI-compatible format: {"choices":[{"delta":{"content":"token"}}]}
    const auto choices = obj[QStringLiteral("choices")].toArray();
    if (choices.isEmpty()) return {};
    return choices[0].toObject()[QStringLiteral("delta")].toObject()
                      [QStringLiteral("content")].toString();
}

void JarvisBackend::onLlmStreamReadyRead()
{
    if (!m_streamReply) return;

    const QByteArray data = m_streamReply->readAll();
    m_streamBuffer += QString::fromUtf8(data);

    if (m_settings->isGeminiOAuthMode()) {
        // Gemini Cloud Code returns a JSON array: [{...},{...},...]
        // Parse complete JSON objects from the stream
        while (true) {
            // Find the start of a JSON object
            int objStart = m_streamBuffer.indexOf(QLatin1Char('{'));
            if (objStart < 0) break;

            // Find matching closing brace (simple depth counting)
            int depth = 0;
            int objEnd = -1;
            bool inString = false;
            bool escaped = false;
            for (int i = objStart; i < m_streamBuffer.size(); ++i) {
                const QChar ch = m_streamBuffer[i];
                if (escaped) { escaped = false; continue; }
                if (ch == QLatin1Char('\\') && inString) { escaped = true; continue; }
                if (ch == QLatin1Char('"')) { inString = !inString; continue; }
                if (inString) continue;
                if (ch == QLatin1Char('{')) ++depth;
                else if (ch == QLatin1Char('}')) {
                    --depth;
                    if (depth == 0) { objEnd = i; break; }
                }
            }
            if (objEnd < 0) break; // Incomplete object, wait for more data

            const QString jsonStr = m_streamBuffer.mid(objStart, objEnd - objStart + 1);
            m_streamBuffer = m_streamBuffer.mid(objEnd + 1);

            const QString content = extractStreamToken(jsonStr);
            if (!content.isEmpty()) {
                m_fullStreamedResponse += content;
                m_streamingResponse = stripActionsFromResponse(m_fullStreamedResponse);
                emit streamingResponseChanged();
                trySpeakCompleteSentences();
            }
        }
    } else {
        // SSE format: each chunk is "data: {...}\n\n"
        while (true) {
            const int nlPos = m_streamBuffer.indexOf(QLatin1Char('\n'));
            if (nlPos < 0) break;

            const QString line = m_streamBuffer.left(nlPos).trimmed();
            m_streamBuffer = m_streamBuffer.mid(nlPos + 1);

            if (line.isEmpty() || line == QStringLiteral("data: [DONE]")) {
                continue;
            }

            // Skip SSE event-type lines (e.g. "event: content_block_delta")
            if (!line.startsWith(QStringLiteral("data: "))) {
                continue;
            }

            const QString content = extractStreamToken(line.mid(6));
            if (!content.isEmpty()) {
                m_fullStreamedResponse += content;
                m_streamingResponse = stripActionsFromResponse(m_fullStreamedResponse);
                emit streamingResponseChanged();
                trySpeakCompleteSentences();
            }
        }
    }
}

void JarvisBackend::trySpeakCompleteSentences()
{
    // Get the displayable text so far (without action blocks)
    const QString displayText = stripActionsFromResponse(m_fullStreamedResponse);

    // Speak on sentence-ending punctuation, commas, or dashes — for lower latency
    // First try sentence boundaries, then fall back to clause boundaries if we have enough text
    static const QRegularExpression sentenceEndRe(QStringLiteral("[.!?;:]\\s"));
    static const QRegularExpression clauseEndRe(QStringLiteral("[,\\-—]\\s"));

    const int searchStart = m_spokenSoFar.length();
    if (searchStart >= displayText.length()) return;

    const QString unspoken = displayText.mid(searchStart);

    auto match = sentenceEndRe.match(unspoken);
    if (!match.hasMatch() && unspoken.length() > 40) {
        // No complete sentence yet but enough text — try speaking at clause boundaries
        match = clauseEndRe.match(unspoken);
    }

    if (match.hasMatch()) {
        // We have at least one complete sentence
        const int endPos = match.capturedStart() + 1; // include the punctuation
        const QString sentence = unspoken.left(endPos).trimmed();

        if (!sentence.isEmpty()) {
            m_spokenSoFar = displayText.left(searchStart + endPos);
            m_tts->speakSentence(sentence);
        }
    }
}

void JarvisBackend::finalizeStreamingResponse()
{
    const QString responseText = m_fullStreamedResponse.trimmed();

    if (responseText.isEmpty()) {
        m_lastResponse = QStringLiteral("Sorry, I wasn't able to formulate a response.");
        emit lastResponseChanged();
        addToChatHistory("jarvis", m_lastResponse);
        setStatus("Ready.");
        return;
    }

    m_conversationHistory.push_back({QStringLiteral("assistant"), responseText});

    const QString spokenText = stripActionsFromResponse(responseText);

    m_lastResponse = spokenText;
    m_streamingResponse.clear();
    emit lastResponseChanged();
    emit streamingResponseChanged();

    addToChatHistory("jarvis", spokenText);
    setStatus("Ready.");
    emit responseReceived(spokenText);

    // Speak any remaining unspoken text
    const QString remaining = spokenText.mid(m_spokenSoFar.length()).trimmed();
    if (!remaining.isEmpty()) {
        m_tts->speakSentence(remaining);
    }

    // Parse and execute any actions embedded in the LLM response
    parseAndExecuteActions(responseText);
}

void JarvisBackend::onLlmStreamFinished()
{
    if (!m_streamReply) return;

    const auto error = m_streamReply->error();
    const QString errorString = m_streamReply->errorString();
    const int httpStatus = m_streamReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // Process any remaining data before cleanup
    const QByteArray remaining = m_streamReply->readAll();
    if (!remaining.isEmpty()) {
        m_streamBuffer += QString::fromUtf8(remaining);
    }

    m_streamReply->deleteLater();
    m_streamReply = nullptr;

    m_processing = false;
    emit processingChanged();

    if (error != QNetworkReply::NoError && m_fullStreamedResponse.isEmpty()) {
        qWarning() << "[JARVIS] LLM request failed:" << httpStatus << errorString;
        const auto errorMsg = QStringLiteral("Connection issue: %1")
                                  .arg(errorString);
        setStatus(errorMsg);
        emit errorOccurred(errorMsg);
        if (!m_conversationHistory.empty()) {
            m_conversationHistory.pop_back();
        }
        return;
    }

    // Parse any remaining SSE lines in buffer
    while (true) {
        const int nlPos = m_streamBuffer.indexOf(QLatin1Char('\n'));
        if (nlPos < 0) break;

        const QString line = m_streamBuffer.left(nlPos).trimmed();
        m_streamBuffer = m_streamBuffer.mid(nlPos + 1);

        if (line.isEmpty() || line == QStringLiteral("data: [DONE]")) continue;
        if (!line.startsWith(QStringLiteral("data: "))) continue;

        const QString content = extractStreamToken(line.mid(6));
        if (!content.isEmpty()) {
            m_fullStreamedResponse += content;
        }
    }

    // Finalize
    finalizeStreamingResponse();
}

void JarvisBackend::checkConnection()
{
    // Cloud providers — don't ping the server, just check if credentials exist
    if (m_settings->providerNeedsApiKey()) {
        // Avoid calling activeApiKey() here as it triggers token refresh attempts
        const bool hasOAuth = m_settings->hasOAuthCredentials();
        const bool hasKey = m_settings->isClaudeProvider()
            ? !m_settings->claudeApiKey().isEmpty()
            : m_settings->isGeminiOAuthMode()
                ? true  // OAuth creds checked via hasOAuth
                : !m_settings->geminiApiKey().isEmpty() || !m_settings->openaiApiKey().isEmpty();
        const bool wasConnected = m_connected;

        m_connected = hasKey || hasOAuth;

        if (m_connected != wasConnected) {
            emit connectedChanged();
            if (m_connected) {
                setStatus("Credentials ready. All systems operational.");
            } else {
                setStatus("No API credentials configured. Please log in or add an API key.");
            }
        }
        return;
    }

    // Local providers (llama.cpp, Ollama) — actually ping the server
    const QUrl url(m_settings->healthCheckUrl());
    QNetworkRequest request(url);
    request.setTransferTimeout(5000);

    auto *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onHealthCheckFinished(reply);
    });
}

void JarvisBackend::onHealthCheckFinished(QNetworkReply *reply)
{
    reply->deleteLater();

    const bool wasConnected = m_connected;
    m_connected = (reply->error() == QNetworkReply::NoError);

    if (m_connected != wasConnected) {
        emit connectedChanged();
        if (m_connected) {
            setStatus("LLM server connected. All systems operational.");
        } else {
            setStatus("LLM server offline. Attempting to reconnect...");
        }
    }
}

// ─────────────────────────────────────────────
// Reminders
// ─────────────────────────────────────────────

void JarvisBackend::addReminder(const QString &text, int secondsFromNow)
{
    Reminder r;
    r.text = text;
    r.triggerTime = QDateTime::currentDateTime().addSecs(secondsFromNow);
    m_reminders.push_back(r);

    QVariantMap map;
    map[QStringLiteral("text")] = text;
    map[QStringLiteral("time")] = r.triggerTime.toString(QStringLiteral("HH:mm:ss"));
    m_activeReminders.append(map);
    emit remindersChanged();

    setStatus(QStringLiteral("Reminder set. I'll notify you at %1.")
                  .arg(r.triggerTime.toString(QStringLiteral("HH:mm"))));
}

void JarvisBackend::removeReminder(int index)
{
    if (index < 0 || index >= static_cast<int>(m_reminders.size())) return;

    m_reminders.erase(m_reminders.begin() + index);
    m_activeReminders.removeAt(index);
    emit remindersChanged();
}

void JarvisBackend::checkReminders()
{
    const auto now = QDateTime::currentDateTime();
    bool changed = false;

    auto it = m_reminders.begin();
    int idx = 0;
    while (it != m_reminders.end()) {
        if (now >= it->triggerTime) {
            const QString text = it->text;
            emit reminderTriggered(text);
            speak(QStringLiteral("Reminder: %1").arg(text));
            addToChatHistory("jarvis", QStringLiteral("\u23f0 Reminder: %1").arg(text));

            it = m_reminders.erase(it);
            if (idx < m_activeReminders.size()) {
                m_activeReminders.removeAt(idx);
            }
            changed = true;
        } else {
            ++it;
            ++idx;
        }
    }

    if (changed) {
        emit remindersChanged();
    }
}

// ─────────────────────────────────────────────
// Misc
// ─────────────────────────────────────────────

void JarvisBackend::clearHistory()
{
    m_chatHistory.clear();
    m_conversationHistory.clear();
    m_lastResponse.clear();
    emit chatHistoryChanged();
    emit lastResponseChanged();
    saveChatHistory();
    setStatus("Memory cleared. Fresh start.");
}

void JarvisBackend::setStatus(const QString &status)
{
    if (m_statusText != status) {
        m_statusText = status;
        emit statusTextChanged();
    }
}

void JarvisBackend::addToChatHistory(const QString &role, const QString &message)
{
    m_chatHistory.append(QStringLiteral("%1|%2").arg(role, message));

    const int maxPairs = m_settings->maxHistoryPairs();
    while (m_chatHistory.size() > maxPairs * 2) {
        m_chatHistory.removeFirst();
    }

    emit chatHistoryChanged();
    saveChatHistory();
}

void JarvisBackend::saveChatHistory()
{
    const QString path = m_settings->jarvisDataDir() + QStringLiteral("/chat_history.json");
    QJsonArray historyArray;
    for (const auto &[role, content] : m_conversationHistory) {
        historyArray.append(QJsonObject{
            {QStringLiteral("role"), role},
            {QStringLiteral("content"), content},
        });
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(historyArray).toJson(QJsonDocument::Compact));
    }
}

void JarvisBackend::loadChatHistory()
{
    const QString path = m_settings->jarvisDataDir() + QStringLiteral("/chat_history.json");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;

    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray()) return;

    const auto arr = doc.array();
    for (const auto &val : arr) {
        const auto obj = val.toObject();
        const QString role = obj[QStringLiteral("role")].toString();
        const QString content = obj[QStringLiteral("content")].toString();
        if (role.isEmpty() || content.isEmpty()) continue;
        m_conversationHistory.push_back({role, content});
        m_chatHistory.append(QStringLiteral("%1|%2").arg(role, content));
    }
    if (!m_chatHistory.isEmpty()) emit chatHistoryChanged();
}

// ─────────────────────────────────────────────
// Action Parsing & Execution
// ─────────────────────────────────────────────

QString JarvisBackend::expandPath(const QString &path) const
{
    QString expanded = path.trimmed();
    const QString home = QDir::homePath();
    if (expanded.startsWith(QStringLiteral("~/"))) {
        expanded = home + expanded.mid(1);
    } else if (expanded == QStringLiteral("~")) {
        expanded = home;
    }
    expanded.replace(QStringLiteral("$HOME"), home);
    return expanded;
}

QString JarvisBackend::stripActionsFromResponse(const QString &responseText) const
{
    QString cleaned = responseText;

    // Remove [ACTION:...] lines and CONTENT_START/CONTENT_END blocks
    static const QRegularExpression actionLineRe(
        QStringLiteral("\\[ACTION:[^\\]]+\\].*"),
        QRegularExpression::MultilineOption);
    static const QRegularExpression contentBlockRe(
        QStringLiteral("CONTENT_START\\n[\\s\\S]*?CONTENT_END"));

    cleaned.remove(contentBlockRe);
    cleaned.remove(actionLineRe);

    // Clean up extra whitespace
    cleaned = cleaned.trimmed();
    static const QRegularExpression multiNewline(QStringLiteral("\\n{3,}"));
    cleaned.replace(multiNewline, QStringLiteral("\n\n"));

    return cleaned;
}

void JarvisBackend::parseAndExecuteActions(const QString &responseText)
{
    // Parse [ACTION:type] arg lines
    static const QRegularExpression actionRe(
        QStringLiteral("\\[ACTION:(\\w+)\\]\\s*(.*)"),
        QRegularExpression::MultilineOption);

    auto it = actionRe.globalMatch(responseText);
    while (it.hasNext()) {
        const auto match = it.next();
        const QString actionType = match.captured(1).toLower();
        const QString arg = match.captured(2).trimmed();

        qDebug() << "[JARVIS] Action:" << actionType << "arg:" << arg;

        if (actionType == QStringLiteral("run_command")) {
            executeRunCommand(arg);
        } else if (actionType == QStringLiteral("open_terminal")) {
            executeOpenTerminal(arg);
        } else if (actionType == QStringLiteral("write_file")) {
            // Extract content between CONTENT_START and CONTENT_END after this action
            const int actionPos = responseText.indexOf(match.captured(0));
            const int csPos = responseText.indexOf(QStringLiteral("CONTENT_START"), actionPos);
            const int cePos = responseText.indexOf(QStringLiteral("CONTENT_END"), csPos);
            if (csPos >= 0 && cePos > csPos) {
                const int contentStart = csPos + QStringLiteral("CONTENT_START").length();
                QString content = responseText.mid(contentStart, cePos - contentStart);
                // Remove leading/trailing newline only (preserve internal formatting)
                if (content.startsWith(QLatin1Char('\n'))) content = content.mid(1);
                if (content.endsWith(QLatin1Char('\n'))) content.chop(1);
                executeWriteFile(arg, content);
            } else {
                qWarning() << "[JARVIS] write_file action missing CONTENT_START/CONTENT_END block";
            }
        } else if (actionType == QStringLiteral("open_app")) {
            executeOpenApp(arg);
        } else if (actionType == QStringLiteral("open_url")) {
            openUrl(arg);
        } else if (actionType == QStringLiteral("type_text")) {
            executeTypeText(arg);
        } else {
            qWarning() << "[JARVIS] Unknown action type:" << actionType;
        }
    }
}

void JarvisBackend::executeRunCommand(const QString &command)
{
    if (command.isEmpty()) return;
    qDebug() << "[JARVIS] Executing command:" << command;

    auto *proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, command](int exitCode, QProcess::ExitStatus) {
        const QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
        const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        if (!out.isEmpty()) {
            addToChatHistory("system", QStringLiteral("Command output: %1").arg(out));
        }
        if (exitCode != 0 && !err.isEmpty()) {
            addToChatHistory("system", QStringLiteral("Command error: %1").arg(err));
        }
        proc->deleteLater();
    });

    // Expand ~ in the command
    const QString expanded = command.contains(QStringLiteral("~"))
        ? QString(command).replace(QStringLiteral("~"), QDir::homePath())
        : command;

    proc->start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), expanded});
}

void JarvisBackend::executeOpenTerminal(const QString &command)
{
    if (command.isEmpty()) return;
    qDebug() << "[JARVIS] Opening terminal with command:" << command;

    const QString expanded = command.contains(QStringLiteral("~"))
        ? QString(command).replace(QStringLiteral("~"), QDir::homePath())
        : command;

    // Try common terminal emulators with execute flag
    const QStringList terminals = {
        QStringLiteral("konsole"),
        QStringLiteral("gnome-terminal"),
        QStringLiteral("xfce4-terminal"),
        QStringLiteral("xterm"),
    };

    for (const auto &term : terminals) {
        auto *which = new QProcess(this);
        which->start(QStringLiteral("which"), {term});
        which->waitForFinished(1000);
        if (which->exitCode() == 0) {
            delete which;
            if (term == QStringLiteral("konsole")) {
                QProcess::startDetached(term, {QStringLiteral("-e"), QStringLiteral("/bin/sh"), QStringLiteral("-c"), expanded + QStringLiteral("; exec $SHELL")});
            } else if (term == QStringLiteral("gnome-terminal")) {
                QProcess::startDetached(term, {QStringLiteral("--"), QStringLiteral("/bin/sh"), QStringLiteral("-c"), expanded + QStringLiteral("; exec $SHELL")});
            } else {
                QProcess::startDetached(term, {QStringLiteral("-e"), QStringLiteral("/bin/sh"), QStringLiteral("-c"), expanded + QStringLiteral("; exec $SHELL")});
            }
            addToChatHistory("system", QStringLiteral("Opened terminal: %1").arg(command));
            return;
        }
        delete which;
    }
    qWarning() << "[JARVIS] No terminal emulator found";
}

void JarvisBackend::executeWriteFile(const QString &path, const QString &content)
{
    const QString expanded = expandPath(path);
    qDebug() << "[JARVIS] Writing file:" << expanded << "content length:" << content.length();

    // Create parent directories
    const QFileInfo fi(expanded);
    QDir().mkpath(fi.absolutePath());

    QFile file(expanded);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(content.toUtf8());
        file.close();
        addToChatHistory("system", QStringLiteral("File written: %1 (%2 bytes)").arg(expanded).arg(content.size()));
        qDebug() << "[JARVIS] File written successfully:" << expanded;
    } else {
        const QString err = QStringLiteral("Failed to write file: %1 \u2014 %2").arg(expanded, file.errorString());
        addToChatHistory("system", err);
        qWarning() << "[JARVIS]" << err;
    }
}

void JarvisBackend::executeOpenApp(const QString &app)
{
    if (app.isEmpty()) return;
    qDebug() << "[JARVIS] Opening app:" << app;

    // If it contains a path (like ~/Desktop/file.md), open with xdg-open
    if (app.contains(QStringLiteral("/")) || app.contains(QStringLiteral("~"))) {
        const QString expanded = expandPath(app);
        QProcess::startDetached(QStringLiteral("xdg-open"), {expanded});
        addToChatHistory("system", QStringLiteral("Opened: %1").arg(expanded));
        return;
    }

    // Try to find and launch the app, possibly with arguments
    const auto parts = app.split(QLatin1Char(' '));
    const QString bin = parts.first();
    QStringList args = parts.mid(1);

    // Expand paths in args
    for (auto &a : args) {
        a = expandPath(a);
    }

    auto *which = new QProcess(this);
    which->start(QStringLiteral("which"), {bin});
    which->waitForFinished(1000);
    if (which->exitCode() == 0) {
        delete which;
        QProcess::startDetached(bin, args);
        addToChatHistory("system", QStringLiteral("Launched: %1").arg(app));
    } else {
        delete which;
        // Try with xdg-open as fallback
        QProcess::startDetached(QStringLiteral("xdg-open"), {app});
        addToChatHistory("system", QStringLiteral("Attempted to open: %1").arg(app));
    }
}

void JarvisBackend::executeTypeText(const QString &text)
{
    if (text.isEmpty()) return;
    qDebug() << "[JARVIS] Typing text:" << text.left(50) << "...";

    // Use xdotool to type text into the focused window
    // Small delay to allow window focus to settle
    auto *proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            proc, &QProcess::deleteLater);

    // xdotool type with a small delay between keystrokes for reliability
    const QString escapedText = QString(text).replace(QStringLiteral("'"), QStringLiteral("'\\''"));
    const QString cmd = QStringLiteral("sleep 0.5 && xdotool type --clearmodifiers --delay 12 '%1'").arg(escapedText);
    proc->start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), cmd});
}
