#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QJsonArray>
#include <QJsonValue>
#include <QVariantList>
#include <QVariantMap>
#include <QDateTime>
#include <vector>
#include <atomic>

class JarvisSettings;
class JarvisTts;
class JarvisAudio;
class JarvisSystem;
class JarvisCommands;
class JarvisRag;
class JarvisMcp;

class JarvisBackend : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Core properties
    Q_PROPERTY(QString lastResponse READ lastResponse NOTIFY lastResponseChanged)
    Q_PROPERTY(QString streamingResponse READ streamingResponse NOTIFY streamingResponseChanged)
    Q_PROPERTY(bool listening READ isListening NOTIFY listeningChanged)
    Q_PROPERTY(bool processing READ isProcessing NOTIFY processingChanged)
    Q_PROPERTY(bool wakeWordActive READ isWakeWordActive NOTIFY wakeWordActiveChanged)
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(bool micBusy READ isMicBusy NOTIFY micBusyChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QStringList chatHistory READ chatHistory NOTIFY chatHistoryChanged)
    Q_PROPERTY(double audioLevel READ audioLevel NOTIFY audioLevelChanged)
    Q_PROPERTY(bool speaking READ isSpeaking NOTIFY speakingChanged)
    Q_PROPERTY(bool ttsMuted READ isTtsMuted NOTIFY ttsMutedChanged)

    // Voice command mode
    Q_PROPERTY(bool voiceCommandMode READ isVoiceCommandMode NOTIFY voiceCommandModeChanged)
    Q_PROPERTY(QString lastTranscription READ lastTranscription NOTIFY lastTranscriptionChanged)
    Q_PROPERTY(QStringList whisperLog READ whisperLog NOTIFY whisperLogChanged)

    // System monitoring
    Q_PROPERTY(double cpuUsage READ cpuUsage NOTIFY systemStatsChanged)
    Q_PROPERTY(double memoryUsage READ memoryUsage NOTIFY systemStatsChanged)
    Q_PROPERTY(double memoryTotalGb READ memoryTotalGb NOTIFY systemStatsChanged)
    Q_PROPERTY(double memoryUsedGb READ memoryUsedGb NOTIFY systemStatsChanged)
    Q_PROPERTY(int cpuTemp READ cpuTemp NOTIFY systemStatsChanged)
    Q_PROPERTY(QString uptime READ uptime NOTIFY systemStatsChanged)
    Q_PROPERTY(QString hostname READ hostname CONSTANT)
    Q_PROPERTY(QString kernelVersion READ kernelVersion CONSTANT)

    // Date/Time
    Q_PROPERTY(QString currentTime READ currentTime NOTIFY currentTimeChanged)
    Q_PROPERTY(QString currentDate READ currentDate NOTIFY currentTimeChanged)
    Q_PROPERTY(QString greeting READ greeting NOTIFY currentTimeChanged)

    // Reminders/Timers
    Q_PROPERTY(QVariantList activeReminders READ activeReminders NOTIFY remindersChanged)

    // Settings properties (delegated to JarvisSettings, exposed for config QML)
    Q_PROPERTY(QString llmProvider READ llmProvider NOTIFY llmProviderChanged)
    Q_PROPERTY(QString llmServerUrl READ llmServerUrl NOTIFY llmServerUrlChanged)
    Q_PROPERTY(QString openaiApiKey READ openaiApiKey NOTIFY openaiApiKeyChanged)
    Q_PROPERTY(QString geminiApiKey READ geminiApiKey NOTIFY geminiApiKeyChanged)
    Q_PROPERTY(QString claudeApiKey READ claudeApiKey NOTIFY claudeApiKeyChanged)
    Q_PROPERTY(QString llmModelId READ llmModelId NOTIFY llmModelIdChanged)
    Q_PROPERTY(QString currentModelName READ currentModelName NOTIFY currentModelNameChanged)
    Q_PROPERTY(QString currentVoiceName READ currentVoiceName NOTIFY currentVoiceNameChanged)
    Q_PROPERTY(QVariantList availableLlmModels READ availableLlmModels NOTIFY availableLlmModelsChanged)
    Q_PROPERTY(QVariantList hfSearchResults READ hfSearchResults NOTIFY hfSearchResultsChanged)
    Q_PROPERTY(QVariantMap modelDetails READ modelDetails NOTIFY modelDetailsChanged)
    Q_PROPERTY(QVariantList cloudModelChoices READ cloudModelChoices NOTIFY cloudModelChoicesChanged)
    Q_PROPERTY(QVariantList availableTtsVoices READ availableTtsVoices NOTIFY availableTtsVoicesChanged)
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY downloadProgressChanged)
    Q_PROPERTY(bool downloading READ isDownloading NOTIFY downloadingChanged)
    Q_PROPERTY(QString downloadStatus READ downloadStatus NOTIFY downloadStatusChanged)
    Q_PROPERTY(int maxHistoryPairs READ maxHistoryPairs NOTIFY maxHistoryPairsChanged)
    Q_PROPERTY(int estimatedTokens READ estimatedTokens NOTIFY chatHistoryChanged)
    Q_PROPERTY(int contextTokenLimit READ contextTokenLimit NOTIFY llmModelIdChanged)
    Q_PROPERTY(int wakeBufferSeconds READ wakeBufferSeconds NOTIFY wakeBufferSecondsChanged)
    Q_PROPERTY(int voiceCmdMaxSeconds READ voiceCmdMaxSeconds NOTIFY voiceCmdMaxSecondsChanged)
    Q_PROPERTY(int silenceTimeoutMs READ silenceTimeoutMs NOTIFY silenceTimeoutMsChanged)
    Q_PROPERTY(bool autoStartWakeWord READ autoStartWakeWord NOTIFY autoStartWakeWordChanged)
    Q_PROPERTY(bool noiseSuppression READ noiseSuppression NOTIFY noiseSuppressionChanged)
    Q_PROPERTY(QString whisperModel READ whisperModel NOTIFY whisperModelChanged)
    Q_PROPERTY(bool whisperGpu READ whisperGpu NOTIFY whisperGpuChanged)
    Q_PROPERTY(QVariantList whisperModelList READ whisperModelList NOTIFY whisperModelListChanged)
    Q_PROPERTY(QString wakeWord READ wakeWord NOTIFY wakeWordChanged)
    Q_PROPERTY(QString personalityPrompt READ personalityPrompt NOTIFY personalityPromptChanged)

    // Commands
    Q_PROPERTY(QVariantList commandMappings READ commandMappings NOTIFY commandMappingsChanged)

    // Continuous conversation
    Q_PROPERTY(bool continuousMode READ continuousMode NOTIFY continuousModeChanged)
    Q_PROPERTY(bool smartRouting READ smartRouting NOTIFY smartRoutingChanged)
    Q_PROPERTY(QString fastModelId READ fastModelId NOTIFY fastModelIdChanged)
    Q_PROPERTY(bool conversationActive READ isConversationActive NOTIFY conversationActiveChanged)

    // OAuth login status
    Q_PROPERTY(bool oauthLoggedIn READ isOAuthLoggedIn NOTIFY oauthStatusChanged)
    Q_PROPERTY(bool awaitingClaudeCode READ awaitingClaudeCode NOTIFY oauthStatusChanged)

public:
    explicit JarvisBackend(QObject *parent = nullptr);
    ~JarvisBackend() override;

    // Core getters
    [[nodiscard]] QString lastResponse() const { return m_lastResponse; }
    [[nodiscard]] QString streamingResponse() const { return m_streamingResponse; }
    [[nodiscard]] bool isListening() const;
    [[nodiscard]] bool isProcessing() const { return m_processing; }
    [[nodiscard]] bool isWakeWordActive() const;
    [[nodiscard]] bool isConnected() const { return m_connected; }
    [[nodiscard]] bool isMicBusy() const;
    [[nodiscard]] QString statusText() const { return m_statusText; }
    [[nodiscard]] QStringList chatHistory() const { return m_chatHistory; }
    [[nodiscard]] double audioLevel() const;
    [[nodiscard]] bool isSpeaking() const;
    [[nodiscard]] bool isTtsMuted() const;

    // Voice command
    [[nodiscard]] bool isVoiceCommandMode() const;
    [[nodiscard]] QString lastTranscription() const;
    [[nodiscard]] QStringList whisperLog() const { return m_whisperLog; }

    // System monitoring (delegated)
    [[nodiscard]] double cpuUsage() const;
    [[nodiscard]] double memoryUsage() const;
    [[nodiscard]] double memoryTotalGb() const;
    [[nodiscard]] double memoryUsedGb() const;
    [[nodiscard]] int cpuTemp() const;
    [[nodiscard]] QString uptime() const;
    [[nodiscard]] QString hostname() const;
    [[nodiscard]] QString kernelVersion() const;

    // Date/Time (delegated)
    [[nodiscard]] QString currentTime() const;
    [[nodiscard]] QString currentDate() const;
    [[nodiscard]] QString greeting() const;

    // Reminders
    [[nodiscard]] QVariantList activeReminders() const { return m_activeReminders; }

    // Settings (delegated)
    [[nodiscard]] QString llmProvider() const;
    [[nodiscard]] QString llmServerUrl() const;
    [[nodiscard]] QString openaiApiKey() const;
    [[nodiscard]] QString geminiApiKey() const;
    [[nodiscard]] QString claudeApiKey() const;
    [[nodiscard]] QString llmModelId() const;
    [[nodiscard]] QString currentModelName() const;
    [[nodiscard]] QString currentVoiceName() const;
    [[nodiscard]] QVariantList availableLlmModels() const;
    [[nodiscard]] QVariantList hfSearchResults() const;
    [[nodiscard]] QVariantMap modelDetails() const;
    [[nodiscard]] QVariantList cloudModelChoices() const;
    [[nodiscard]] QVariantList availableTtsVoices() const;
    [[nodiscard]] double downloadProgress() const;
    [[nodiscard]] bool isDownloading() const;
    [[nodiscard]] QString downloadStatus() const;
    [[nodiscard]] int maxHistoryPairs() const;
    [[nodiscard]] int estimatedTokens() const;
    [[nodiscard]] int contextTokenLimit() const;
    [[nodiscard]] int wakeBufferSeconds() const;
    [[nodiscard]] int voiceCmdMaxSeconds() const;
    [[nodiscard]] int silenceTimeoutMs() const;
    [[nodiscard]] bool autoStartWakeWord() const;
    [[nodiscard]] bool noiseSuppression() const;
    [[nodiscard]] QString whisperModel() const;
    [[nodiscard]] bool whisperGpu() const;
    [[nodiscard]] QVariantList whisperModelList() const { return m_whisperModelList; }
    [[nodiscard]] QString wakeWord() const;
    [[nodiscard]] QString personalityPrompt() const;

    // Continuous conversation
    [[nodiscard]] bool continuousMode() const { return m_continuousMode; }
    [[nodiscard]] bool smartRouting() const;
    [[nodiscard]] QString fastModelId() const;
    [[nodiscard]] bool isConversationActive() const { return m_conversationActive; }

    // OAuth (delegated)
    [[nodiscard]] bool isOAuthLoggedIn();
    [[nodiscard]] bool awaitingClaudeCode() const;

    // Commands (delegated)
    [[nodiscard]] QVariantList commandMappings() const;

    // Core invokables
    Q_INVOKABLE void sendMessage(const QString &message);
    Q_INVOKABLE void toggleWakeWord();
    Q_INVOKABLE void speak(const QString &text);
    Q_INVOKABLE void stopSpeaking();
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void exportHistory(const QString &path);
    Q_INVOKABLE void importHistory(const QString &path);
    Q_INVOKABLE void checkConnection();

    // Voice command
    Q_INVOKABLE void startVoiceCommand();
    Q_INVOKABLE void stopVoiceCommand();

    // Reminders
    Q_INVOKABLE void addReminder(const QString &text, int secondsFromNow);
    Q_INVOKABLE void removeReminder(int index);

    // TTS control
    Q_INVOKABLE void setTtsRate(double rate);
    Q_INVOKABLE void setTtsPitch(double pitch);
    Q_INVOKABLE void setTtsVolume(double volume);
    Q_INVOKABLE void toggleTtsMute();

    // Settings invokables
    Q_INVOKABLE void setLlmProvider(const QString &provider);
    Q_INVOKABLE void setLlmServerUrl(const QString &url);
    Q_INVOKABLE void setOpenaiApiKey(const QString &key);
    Q_INVOKABLE void setGeminiApiKey(const QString &key);
    Q_INVOKABLE void setClaudeApiKey(const QString &key);
    Q_INVOKABLE void setLlmModelId(const QString &modelId);
    Q_INVOKABLE void refreshOllamaModels();
    Q_INVOKABLE void searchModels(const QString &query);
    Q_INVOKABLE void fetchModelDetails(const QString &modelId);
    Q_INVOKABLE void pullOllamaModel(const QString &modelName);
    Q_INVOKABLE void refreshCloudModels();
    Q_INVOKABLE void downloadLlmModel(const QString &modelId);
    Q_INVOKABLE void downloadTtsVoice(const QString &voiceId);
    Q_INVOKABLE void setActiveLlmModel(const QString &modelId);
    Q_INVOKABLE void setActiveTtsVoice(const QString &voiceId);
    Q_INVOKABLE void setMaxHistoryPairs(int pairs);
    Q_INVOKABLE void setWakeBufferSeconds(int seconds);
    Q_INVOKABLE void setVoiceCmdMaxSeconds(int seconds);
    Q_INVOKABLE void setSilenceTimeoutMs(int ms);
    Q_INVOKABLE void setAutoStartWakeWord(bool enabled);
    Q_INVOKABLE void setNoiseSuppression(bool enabled);
    Q_INVOKABLE void setWhisperModel(const QString &model);
    Q_INVOKABLE void setWhisperGpu(bool enabled);
    Q_INVOKABLE void fetchWhisperModels();
    Q_INVOKABLE void downloadWhisperModel(const QString &filename);
    Q_INVOKABLE void setWakeWord(const QString &word);
    Q_INVOKABLE void setContinuousMode(bool enabled);
    Q_INVOKABLE void setSmartRouting(bool enabled);
    Q_INVOKABLE void setFastModelId(const QString &modelId);
    Q_INVOKABLE void stopConversation();
    Q_INVOKABLE void setPersonalityPrompt(const QString &prompt);
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void openUrl(const QString &url);
    Q_INVOKABLE void testVoice(const QString &voiceId);
    Q_INVOKABLE void searchVoices(const QString &langFilter, const QString &qualityFilter = {});

    // OAuth invokables
    Q_INVOKABLE void oauthLogin(const QString &provider);
    Q_INVOKABLE void oauthLogout(const QString &provider);
    Q_INVOKABLE void cancelOAuthLogin();
    Q_INVOKABLE void completeClaudeLogin(const QString &code);

    // Commands invokables
    Q_INVOKABLE void addCommand(const QString &phrase, const QString &action, const QString &type);
    Q_INVOKABLE void removeCommand(int index);
    Q_INVOKABLE void updateCommand(int index, const QString &phrase, const QString &action, const QString &type);
    Q_INVOKABLE void resetCommandsToDefaults();

signals:
    void lastResponseChanged();
    void streamingResponseChanged();
    void listeningChanged();
    void processingChanged();
    void wakeWordActiveChanged();
    void connectedChanged();
    void micBusyChanged();
    void statusTextChanged();
    void chatHistoryChanged();
    void audioLevelChanged();
    void speakingChanged();
    void wakeWordDetected();
    void responseReceived(const QString &response);
    void errorOccurred(const QString &error);
    void voiceCommandModeChanged();
    void lastTranscriptionChanged();
    void whisperLogChanged();
    void systemStatsChanged();
    void currentTimeChanged();
    void remindersChanged();
    void reminderTriggered(const QString &text);
    void ttsMutedChanged();
    void llmProviderChanged();
    void llmServerUrlChanged();
    void openaiApiKeyChanged();
    void geminiApiKeyChanged();
    void claudeApiKeyChanged();
    void llmModelIdChanged();
    void currentModelNameChanged();
    void currentVoiceNameChanged();
    void downloadProgressChanged();
    void downloadingChanged();
    void downloadStatusChanged();
    void maxHistoryPairsChanged();
    void wakeBufferSecondsChanged();
    void voiceCmdMaxSecondsChanged();
    void silenceTimeoutMsChanged();
    void autoStartWakeWordChanged();
    void noiseSuppressionChanged();
    void whisperModelChanged();
    void whisperGpuChanged();
    void whisperModelListChanged();
    void wakeWordChanged();
    void personalityPromptChanged();
    void continuousModeChanged();
    void smartRoutingChanged();
    void fastModelIdChanged();
    void conversationActiveChanged();
    void oauthStatusChanged();
    void commandMappingsChanged();
    void availableLlmModelsChanged();
    void hfSearchResultsChanged();
    void modelDetailsChanged();
    void cloudModelChoicesChanged();
    void availableTtsVoicesChanged();

private slots:
    void onLlmStreamReadyRead();
    void onLlmStreamFinished();
    void onHealthCheckFinished(QNetworkReply *reply);
    void checkReminders();
    void onVoiceCommandTranscribed(const QString &text);

private:
    void sendToLlm(const QString &userMessage);
    void setStatus(const QString &status);
    void appendWhisperLog(const QString &source, const QString &text);
    void addToChatHistory(const QString &role, const QString &message);
    QJsonArray buildConversationContext() const;
    QString buildSystemPrompt() const;
    QJsonArray builtinToolsForClaude() const;
    QJsonArray builtinToolsForOpenAI() const;
    bool dispatchToolCall(const QString &name, const QJsonObject &args,
                          std::function<void(QJsonArray, bool)> callback);
    QString doRagSearch(const QString &userMessage);
    void onRagFinished(const QString &userMessage, const QString &context);
    void continueToLlm(const QString &userMessage);
    QString extractStreamToken(const QString &jsonStr) const;
    void connectModuleSignals();
    void trySpeakCompleteSentences();
    void finalizeStreamingResponse();
    static int estimateTokenCount(const QString &text) { return text.length() / 4; }
    void trimConversationToTokenLimit();
    void handleScreenContext(const QString &question);
    bool handleMusicRequest(const QString &command);
    void saveChatHistory();
    void loadChatHistory();
    void continueAfterToolCall();

    static constexpr auto JARVIS_SYSTEM_PROMPT =
        "You are a friendly, helpful AI voice assistant running on a Linux desktop.\n"
        "- Be warm, concise, and conversational\n"
        "- Keep responses short and natural — this is a voice interface, not a text chat\n"
        "- Be witty when appropriate but don't force it\n"
        "- Respond in the same language the user speaks to you\n"
        "- When reporting system stats, keep it brief and clear\n\n"
        "SYSTEM INTERACTION CAPABILITIES:\n"
        "You can interact with the Linux system by including ACTION blocks in your response.\n"
        "Put your spoken response FIRST, then any actions at the END.\n"
        "Actions are in this exact format (one per line):\n\n"
        "[ACTION:run_command] command here\n"
        "  Runs a shell command. Example: [ACTION:run_command] ls -la ~/Desktop\n\n"
        "[ACTION:open_terminal] command here\n"
        "  Opens a terminal and runs a command in it. Example: [ACTION:open_terminal] htop\n\n"
        "[ACTION:write_file] /path/to/file.txt\n"
        "CONTENT_START\n"
        "file contents here, can be multiple lines\n"
        "CONTENT_END\n"
        "  Writes content to a file. Creates directories if needed.\n\n"
        "[ACTION:open_app] application_name\n"
        "  Opens a GUI application. Example: [ACTION:open_app] kate\n\n"
        "[ACTION:open_url] https://example.com\n"
        "  Opens a URL in the default browser.\n\n"
        "[ACTION:type_text] text to type\n"
        "  Types text into the currently focused window using xdotool.\n\n"
        "IMPORTANT RULES FOR ACTIONS:\n"
        "- ALWAYS put your spoken response first, then actions at the end\n"
        "- The user's home directory is available as ~ or $HOME\n"
        "- The user's desktop is at ~/Desktop\n"
        "- For creating text files, use write_file with the full path\n"
        "- If asked to open an editor and write something, use write_file to create the file, then open_app to open it\n"
        "- Use .md extension for notes/documents, .txt for plain text, .sh for scripts\n"
        "- You can chain multiple actions\n"
        "- If you don't need to perform any system action, just respond normally without ACTION blocks\n\n"
        "MUSIC:\n"
        "When the user asks to play music, you MUST include an ACTION block to actually play it.\n"
        "Say something brief, then ALWAYS add:\n"
        "[ACTION:open_url] spotify:search:Artist Name Song Title\n"
        "Example: User says 'play some jazz'\n"
        "Response: Great choice! How about some Miles Davis.\n"
        "[ACTION:open_url] spotify:search:Miles Davis Kind of Blue\n"
        "NEVER just describe music without the ACTION block — the user wants to hear it.\n";

    // Action parsing and execution
    void parseAndExecuteActions(const QString &responseText);
    void executeRunCommand(const QString &command);
    void executeOpenTerminal(const QString &command);
    void executeWriteFile(const QString &path, const QString &content);
    void executeOpenApp(const QString &app);
    void executeTypeText(const QString &text);
    QString stripActionsFromResponse(const QString &responseText) const;
    QString expandPath(const QString &path) const;

    static constexpr int MAX_TOOL_CALL_LOOPS = 10;

    // Module instances (owned)
    JarvisSettings *m_settings{nullptr};
    JarvisTts *m_tts{nullptr};
    JarvisAudio *m_audio{nullptr};
    JarvisSystem *m_system{nullptr};
    JarvisCommands *m_commands{nullptr};
    JarvisRag *m_rag{nullptr};
    JarvisMcp *m_mcp{nullptr};

    // Network
    QNetworkAccessManager *m_networkManager{nullptr};
    QTimer *m_healthCheckTimer{nullptr};
    QTimer *m_reminderTimer{nullptr};

    // Core state
    QString m_lastResponse;
    QString m_streamingResponse;
    QString m_statusText;
    QStringList m_chatHistory;
    std::atomic<bool> m_processing{false};
    std::atomic<bool> m_connected{false};

    // Streaming state
    QNetworkReply *m_streamReply{nullptr};
    QString m_streamBuffer;
    QString m_fullStreamedResponse;
    QString m_spokenSoFar;
    QString m_pendingOAuthMessage;
    int m_toolCallLoopCount{0};

    // Interrupted context — lets the model know it was cut off
    QString m_interruptedQuestion;
    QString m_interruptedAnswer;
    QString m_interruptedSpoken;
    QString m_currentUserMessage; // Original message before RAG modification

    // Continuous conversation
    bool m_continuousMode{false};
    bool m_conversationActive{false};
    QStringList m_whisperLog;
    bool m_ragActive{false};
    QString m_ragContext; // Transient RAG content for current request only
    QVariantList m_whisperModelList;
    int m_emptyTranscriptionCount{0};
    QTimer *m_conversationTimeout{nullptr};

    // Conversation
    struct ChatMessage {
        QString role;
        QJsonValue content;  // QString (plain text) or QJsonArray (tool_use/tool_result blocks)
    };
    std::vector<ChatMessage> m_conversationHistory;

    // Reminders
    struct Reminder {
        QString text;
        QDateTime triggerTime;
    };
    std::vector<Reminder> m_reminders;
    QVariantList m_activeReminders;
};
