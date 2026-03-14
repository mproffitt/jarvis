#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QProcess>
#include <QTextToSpeech>
#include <QQueue>
#include <QMutex>
#include <atomic>

class JarvisSettings;

class JarvisTts : public QObject
{
    Q_OBJECT

public:
    explicit JarvisTts(JarvisSettings *settings, QObject *parent = nullptr);
    ~JarvisTts() override;

    [[nodiscard]] bool isSpeaking() const { return m_speaking.load(); }
    [[nodiscard]] bool isMuted() const;

    void speak(const QString &text);
    void speakSentence(const QString &sentence);
    void stop();
    void toggleMute();

    // Called when settings change
    void onTtsRateChanged();
    void onTtsPitchChanged();
    void onTtsVolumeChanged();
    void onVoiceActivated(const QString &voiceId, const QString &onnxPath);

    // Sentence splitting for streaming
    static QStringList splitIntoSentences(const QString &text);

signals:
    void speakingChanged();

private:
    void initTts();
    void processNextSentence();

    JarvisSettings *m_settings{nullptr};
    QTextToSpeech *m_tts{nullptr};

    // Per-sentence piper process
    QProcess *m_sentenceProc{nullptr};
    QString m_piperBin;
    bool m_usePiper{false};
    std::atomic<bool> m_speaking{false};

    // Sentence queue for streaming TTS
    QQueue<QString> m_sentenceQueue;
    QMutex m_queueMutex;
    bool m_playingBack{false};
};
