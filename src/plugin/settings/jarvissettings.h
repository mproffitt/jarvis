#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class JarvisOAuth;
namespace KWallet { class Wallet; }

class JarvisSettings : public QObject
{
    Q_OBJECT

public:
    explicit JarvisSettings(QNetworkAccessManager *nam, QObject *parent = nullptr);

    // Getters
    [[nodiscard]] QString llmServerUrl() const { return m_llmServerUrl; }
    [[nodiscard]] QString llmProvider() const { return m_llmProvider; }
    [[nodiscard]] QString openaiApiKey() const { return m_openaiApiKey; }
    [[nodiscard]] QString geminiApiKey() const { return m_geminiApiKey; }
    [[nodiscard]] QString claudeApiKey() const { return m_claudeApiKey; }
    [[nodiscard]] QString activeApiKey();
    [[nodiscard]] bool hasOAuthCredentials() const;
    [[nodiscard]] bool isOAuthLoggedIn();
    void ensureOAuthToken();
    void oauthLogin(const QString &provider);
    void oauthLogout(const QString &provider);
    void cancelOAuthLogin();
    void completeClaudeLogin(const QString &code);
    [[nodiscard]] bool awaitingClaudeCode() const;
    [[nodiscard]] bool isClaudeProvider() const { return m_llmProvider == QStringLiteral("claude"); }
    [[nodiscard]] bool isGeminiOAuthMode() const;
    [[nodiscard]] QString geminiCloudCodeUrl() const;
    [[nodiscard]] QString geminiProjectId() const;
    [[nodiscard]] QString llmModelId() const { return m_llmModelId; }
    [[nodiscard]] QString currentModelName() const { return m_currentModelName; }
    [[nodiscard]] QString currentVoiceName() const { return m_currentVoiceName; }
    [[nodiscard]] QVariantList availableLlmModels() const { return m_availableLlmModels; }
    [[nodiscard]] QVariantList availableTtsVoices() const { return m_availableTtsVoices; }
    [[nodiscard]] double downloadProgress() const { return m_downloadProgress; }
    [[nodiscard]] bool isDownloading() const { return m_downloading; }
    [[nodiscard]] QString downloadStatus() const { return m_downloadStatus; }
    [[nodiscard]] int maxHistoryPairs() const { return m_maxHistoryPairs; }
    [[nodiscard]] int contextTokenLimit() const;
    [[nodiscard]] int wakeBufferSeconds() const { return m_wakeBufferSeconds; }
    [[nodiscard]] int voiceCmdMaxSeconds() const { return m_voiceCmdMaxSeconds; }
    [[nodiscard]] int silenceTimeoutMs() const { return m_silenceTimeoutMs; }
    [[nodiscard]] bool autoStartWakeWord() const { return m_autoStartWakeWord; }
    [[nodiscard]] QString whisperModel() const { return m_whisperModel; }
    [[nodiscard]] QString wakeWord() const { return m_wakeWord; }
    [[nodiscard]] bool continuousMode() const { return m_continuousMode; }
    [[nodiscard]] QString personalityPrompt() const { return m_personalityPrompt; }
    [[nodiscard]] double ttsRate() const { return m_ttsRate; }
    [[nodiscard]] double ttsPitch() const { return m_ttsPitch; }
    [[nodiscard]] double ttsVolume() const { return m_ttsVolume; }
    [[nodiscard]] bool ttsMuted() const { return m_ttsMuted; }

    // Setters
    void setLlmServerUrl(const QString &url);
    void setLlmProvider(const QString &provider);
    void setOpenaiApiKey(const QString &key);
    void setGeminiApiKey(const QString &key);
    void setClaudeApiKey(const QString &key);
    void setLlmModelId(const QString &modelId);
    void setCurrentModelName(const QString &name);
    void setMaxHistoryPairs(int pairs);
    void setWakeBufferSeconds(int seconds);
    void setVoiceCmdMaxSeconds(int seconds);
    void setSilenceTimeoutMs(int ms);
    void setAutoStartWakeWord(bool enabled);
    void setWhisperModel(const QString &model);
    void setWakeWord(const QString &word);
    void setContinuousMode(bool enabled);
    void setPersonalityPrompt(const QString &prompt);
    void setTtsRate(double rate);
    void setTtsPitch(double pitch);
    void setTtsVolume(double volume);
    void setTtsMuted(bool muted);

    // Downloads
    void downloadLlmModel(const QString &modelId);
    void downloadTtsVoice(const QString &voiceId);
    void setActiveLlmModel(const QString &modelId);
    void setActiveTtsVoice(const QString &voiceId);
    void cancelDownload();

    // Helpers
    [[nodiscard]] QString jarvisDataDir() const;
    [[nodiscard]] QString piperModelPath() const { return m_piperModelPath; }
    [[nodiscard]] QString chatCompletionsUrl() const;
    [[nodiscard]] QString healthCheckUrl() const;
    [[nodiscard]] bool providerNeedsApiKey() const;
    [[nodiscard]] bool providerNeedsModelInRequest() const;
    [[nodiscard]] QString defaultUrlForProvider(const QString &provider) const;
    [[nodiscard]] QVariantList cloudModelChoices() const { return m_cloudModelChoices; }
    void fetchCloudModels();
    void populateModelList();
    void populateVoiceList();
    void fetchMoreModels();
    void fetchMoreVoices();
    void fetchOllamaModels();

signals:
    void llmServerUrlChanged();
    void llmProviderChanged();
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
    void whisperModelChanged();
    void wakeWordChanged();
    void continuousModeChanged();
    void personalityPromptChanged();
    void ttsRateChanged();
    void ttsPitchChanged();
    void ttsVolumeChanged();
    void ttsMutedChanged();
    void voiceActivated(const QString &voiceId, const QString &onnxPath);
    void cloudModelChoicesChanged();
    void oauthTokenReady();
    void oauthTokenError(const QString &error);
    void oauthLoginStarted(const QString &provider);
    void oauthLoginFinished(const QString &provider, bool success);
    void oauthStatusChanged();

private:
    void loadSettings();
    void saveSettings();
    void openWallet();
    void onWalletOpened(bool success);
    void writeKeyToWallet(const QString &entry, const QString &key);

    static inline const QString WALLET_FOLDER = QStringLiteral("jarvis-plasmoid");

    QSettings m_settings{QStringLiteral("jarvis-plasmoid"), QStringLiteral("jarvis")};
    QNetworkAccessManager *m_networkManager{nullptr};
    KWallet::Wallet *m_wallet{nullptr};
    JarvisOAuth *m_oauth{nullptr};

    QString m_llmProvider{QStringLiteral("llamacpp")};
    QString m_llmServerUrl{QStringLiteral("http://127.0.0.1:8080")};
    QString m_openaiApiKey;
    QString m_geminiApiKey;
    QString m_claudeApiKey;
    QString m_llmModelId;
    QString m_currentModelName;
    QString m_currentVoiceName;
    QVariantList m_availableLlmModels;
    QVariantList m_availableTtsVoices;
    double m_downloadProgress{0.0};
    bool m_downloading{false};
    QString m_downloadStatus;
    QNetworkReply *m_downloadReply{nullptr};
    int m_maxHistoryPairs{20};
    int m_wakeBufferSeconds{2};
    int m_voiceCmdMaxSeconds{8};
    int m_silenceTimeoutMs{640};
    bool m_autoStartWakeWord{true};
    QString m_whisperModel{QStringLiteral("tiny")};
    QString m_wakeWord;
    bool m_continuousMode{false};
    QString m_personalityPrompt;
    double m_ttsRate{0.05};
    double m_ttsPitch{-0.1};
    double m_ttsVolume{0.85};
    bool m_ttsMuted{false};
    QString m_piperModelPath;
    QVariantList m_cloudModelChoices;
};
