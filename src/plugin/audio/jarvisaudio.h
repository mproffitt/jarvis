#pragma once

#include <QObject>
#include <QTimer>
#include <QMutex>
#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <atomic>
#include <vector>
#include <functional>

#include <whisper.h>
#include <rnnoise.h>

class JarvisSettings;
class MicMonitor;
class PwCapture;

class JarvisAudio : public QObject
{
    Q_OBJECT

public:
    explicit JarvisAudio(JarvisSettings *settings, QObject *parent = nullptr);
    ~JarvisAudio() override;

    [[nodiscard]] bool isListening() const { return m_listening.load(); }
    [[nodiscard]] bool isWakeWordActive() const { return m_wakeWordActive.load(); }
    [[nodiscard]] double audioLevel() const { return m_audioLevel; }
    [[nodiscard]] bool isVoiceCommandMode() const { return m_voiceCommandMode.load(); }
    [[nodiscard]] bool isMicBusy() const;
    [[nodiscard]] QString lastTranscription() const { return m_lastTranscription; }

    void toggleWakeWord();
    void reloadWhisperModel();
    void startVoiceCommand();
    void stopVoiceCommand();
    void setTtsSpeaking(bool speaking);

    // Dynamic settings
    void updateWakeBufferInterval(int seconds);
    void updateVoiceCmdTimeout(int seconds);
    void updateSilenceTimeout(int ms);

signals:
    void listeningChanged();
    void wakeWordActiveChanged();
    void audioLevelChanged();
    void wakeWordDetected();
    void wakeWordMatch(const QString &word);
    void ttsInterrupted();
    void voiceCommandModeChanged();
    void lastTranscriptionChanged();
    void voiceCommandTranscribed(const QString &text);
    void micBusyChanged(bool busy);
    void modelDownloadStatus(const QString &status);
    void whisperHeard(const QString &source, const QString &text);

private slots:
    void processAudioBuffer();
    void processVoiceCommand();

private:
    void initAudioCapture();
    void initWhisper();
    void startListening();
    void stopListening();
    QString detectWakeWord(const QByteArray &audioData);
    QString transcribeAudio(const QByteArray &audioData);
    std::vector<float> pcm16ToFloat(const QByteArray &audioData) const;
    QString findWhisperModel() const;
    QString findVadModel() const;
    void initVad();
    void ensureModelsDownloaded();
    void downloadModel(const QString &url, const QString &destPath, const std::function<void()> &onComplete);
    void initRnnoise();
    QByteArray denoiseAudio(const QByteArray &pcm16) const;

    static constexpr int JARVIS_SAMPLE_RATE = 16000;

    JarvisSettings *m_settings{nullptr};

    PwCapture *m_capture{nullptr};
    QTimer *m_audioProcessTimer{nullptr};
    QTimer *m_voiceCmdTimer{nullptr};
    QTimer *m_silenceTimer{nullptr};
    QTimer *m_vadCheckTimer{nullptr};
    QByteArray m_audioBuffer;
    QMutex m_audioMutex;

    // Silence detection state
    int m_silentChunks{0};
    bool m_speechStarted{false};
    static constexpr int SILENCE_CHECK_MS = 80;       // Check every 80ms
    static constexpr double SILENCE_THRESHOLD = 0.008; // Energy threshold (normalized 0-1)
    int m_silenceChunksNeeded{8};                      // 640ms default (configurable)
    static constexpr int MIN_SPEECH_CHUNKS = 3;        // Need 240ms of speech before silence detection

    whisper_context *m_whisperCtx{nullptr};
    struct whisper_vad_context *m_vadCtx{nullptr};
    QMutex m_whisperMutex;
    std::atomic<bool> m_whisperBusy{false};

    std::atomic<bool> m_listening{false};
    std::atomic<bool> m_wakeWordActive{false};
    std::atomic<bool> m_voiceCommandMode{false};
    std::atomic<bool> m_ttsSpeaking{false};
    double m_audioLevel{0.0};
    QString m_lastTranscription;
    QString m_lastWakeTranscript;

    MicMonitor *m_micMonitor{nullptr};
    QNetworkAccessManager *m_downloadManager{nullptr};
    DenoiseState *m_rnnoise{nullptr};
};
