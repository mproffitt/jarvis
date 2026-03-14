#pragma once

#include <QObject>
#include <QAudioSource>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QIODevice>
#include <QTimer>
#include <QMutex>
#include <QByteArray>
#include <atomic>
#include <vector>

#include <whisper.h>

class JarvisSettings;
class MicMonitor;

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
    [[nodiscard]] QString lastTranscription() const { return m_lastTranscription; }

    void toggleWakeWord();
    void startVoiceCommand();
    void stopVoiceCommand();
    void setTtsSpeaking(bool speaking);

    // Dynamic settings
    void updateWakeBufferInterval(int seconds);
    void updateVoiceCmdTimeout(int seconds);

signals:
    void listeningChanged();
    void wakeWordActiveChanged();
    void audioLevelChanged();
    void wakeWordDetected();
    void voiceCommandModeChanged();
    void lastTranscriptionChanged();
    void voiceCommandTranscribed(const QString &text);
    void micBusyChanged(bool busy);

private slots:
    void processAudioBuffer();
    void processVoiceCommand();

private:
    void initAudioCapture();
    void initWhisper();
    void startListening();
    void stopListening();
    bool detectWakeWord(const QByteArray &audioData);
    QString transcribeAudio(const QByteArray &audioData);
    std::vector<float> pcm16ToFloat(const QByteArray &audioData) const;
    QString findWhisperModel() const;

    static constexpr int JARVIS_SAMPLE_RATE = 16000;

    JarvisSettings *m_settings{nullptr};

    QMediaDevices *m_mediaDevices{nullptr};
    QAudioSource *m_audioSource{nullptr};
    QIODevice *m_audioDevice{nullptr};
    QTimer *m_audioProcessTimer{nullptr};
    QTimer *m_voiceCmdTimer{nullptr};
    QTimer *m_silenceTimer{nullptr};
    QByteArray m_audioBuffer;
    QMutex m_audioMutex;

    // Silence detection state
    int m_silentChunks{0};
    bool m_speechStarted{false};
    static constexpr int SILENCE_CHECK_MS = 80;       // Check every 80ms
    static constexpr double SILENCE_THRESHOLD = 0.008; // Energy threshold (normalized 0-1)
    static constexpr int SILENCE_CHUNKS_NEEDED = 8;    // 640ms of silence to stop
    static constexpr int MIN_SPEECH_CHUNKS = 3;        // Need 240ms of speech before silence detection

    whisper_context *m_whisperCtx{nullptr};
    QMutex m_whisperMutex;
    std::atomic<bool> m_whisperBusy{false};

    std::atomic<bool> m_listening{false};
    std::atomic<bool> m_wakeWordActive{false};
    std::atomic<bool> m_voiceCommandMode{false};
    std::atomic<bool> m_ttsSpeaking{false};
    double m_audioLevel{0.0};
    QString m_lastTranscription;

    MicMonitor *m_micMonitor{nullptr};
};
