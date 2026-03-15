#include "jarvisTts.h"
#include "../settings/jarvissettings.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QLocale>
#include <QVoice>
#include <QTimer>
#include <QDebug>

JarvisTts::JarvisTts(JarvisSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    initTts();
}

JarvisTts::~JarvisTts()
{
    stop();
}

void JarvisTts::initTts()
{
    const QStringList piperPaths = {
        QDir::homePath() + QStringLiteral("/.local/bin/piper"),
        QStringLiteral("/usr/lib/piper-tts/bin/piper"),
        QStringLiteral("/usr/bin/piper"),
        QStringLiteral("/usr/local/bin/piper"),
    };

    for (const auto &path : piperPaths) {
        if (QFile::exists(path)) { m_piperBin = path; break; }
    }

    const QString modelPath = m_settings->piperModelPath();

    if (!m_piperBin.isEmpty() && !modelPath.isEmpty()) {
        m_usePiper = true;
        qDebug() << "[JARVIS] Using piper-tts for speech:" << m_piperBin << "model:" << modelPath;
        qDebug() << "[JARVIS] Piper ready for per-sentence synthesis";
    } else {
        m_usePiper = false;
        qDebug() << "[JARVIS] Piper not found, falling back to espeak-ng";

        m_tts = new QTextToSpeech(this);
        const auto voices = m_tts->availableVoices();
        for (const auto &voice : voices) {
            if (voice.locale().language() == QLocale::English &&
                voice.gender() == QVoice::Male) {
                m_tts->setVoice(voice);
                break;
            }
        }
        m_tts->setRate(m_settings->ttsRate());
        m_tts->setPitch(m_settings->ttsPitch());
        m_tts->setVolume(m_settings->ttsVolume());

        connect(m_tts, &QTextToSpeech::stateChanged, this, [this](QTextToSpeech::State state) {
            const bool wasSpeaking = m_speaking.load();
            m_speaking = (state == QTextToSpeech::Speaking);
            if (wasSpeaking != m_speaking.load()) {
                emit speakingChanged();
            }
        });
    }
}

// ─────────────────────────────────────────────
// Sentence Splitting
// ─────────────────────────────────────────────

QStringList JarvisTts::splitIntoSentences(const QString &text)
{
    QStringList sentences;
    static const QRegularExpression sentenceRe(
        QStringLiteral("(?<=[.!?;:])\\s+|(?<=[.!?;:])$"));

    const auto parts = text.split(sentenceRe, Qt::SkipEmptyParts);
    for (const auto &part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty()) {
            sentences.append(trimmed);
        }
    }

    if (sentences.isEmpty() && !text.trimmed().isEmpty()) {
        sentences.append(text.trimmed());
    }

    return sentences;
}

// ─────────────────────────────────────────────
// Speak (full text or single sentence)
// ─────────────────────────────────────────────

void JarvisTts::speak(const QString &text)
{
    if (m_settings->ttsMuted()) return;

    QString cleanText = text;
    cleanText.remove(QRegularExpression(QStringLiteral("[*_`#]")));
    cleanText.replace(QStringLiteral("\n"), QStringLiteral(". "));

    if (m_usePiper) {
        const QStringList sentences = splitIntoSentences(cleanText);
        {
            QMutexLocker lock(&m_queueMutex);
            for (const auto &s : sentences) {
                m_sentenceQueue.enqueue(s);
            }
        }
        if (!m_playingBack) {
            processNextSentence();
        }
    } else if (m_tts) {
        m_tts->say(cleanText);
    }
}

void JarvisTts::speakSentence(const QString &sentence)
{
    if (m_settings->ttsMuted()) return;
    if (sentence.trimmed().isEmpty()) return;

    QString cleanText = sentence;
    cleanText.remove(QRegularExpression(QStringLiteral("[*_`#]")));
    cleanText.replace(QStringLiteral("\n"), QStringLiteral(". "));

    if (m_usePiper) {
        {
            QMutexLocker lock(&m_queueMutex);
            m_sentenceQueue.enqueue(cleanText);
        }
        if (!m_playingBack) {
            processNextSentence();
        }
    } else if (m_tts) {
        m_tts->say(cleanText);
    }
}

void JarvisTts::processNextSentence()
{
    QString sentence;
    {
        QMutexLocker lock(&m_queueMutex);
        if (m_sentenceQueue.isEmpty()) {
            m_playingBack = false;
            m_speaking = false;
            emit speakingChanged();
            return;
        }
        sentence = m_sentenceQueue.dequeue();
    }

    m_playingBack = true;
    if (!m_speaking.load()) {
        m_speaking = true;
        emit speakingChanged();
    }

    if (m_piperBin.isEmpty()) {
        qWarning() << "[JARVIS] Piper binary not found";
        m_playingBack = false;
        m_speaking = false;
        emit speakingChanged();
        return;
    }

    // Stream per sentence: piper --output_raw | pw-cat --playback
    const double lengthScale = 1.0 - (m_settings->ttsRate() * 0.5);
    const QString quotedText = QStringLiteral("'") +
        QString(sentence).replace(QStringLiteral("'"), QStringLiteral("'\\''")) +
        QStringLiteral("'");

    QString cmd;
    cmd += QStringLiteral("printf '%s' ") + quotedText;
    cmd += QStringLiteral(" | '") + m_piperBin + QStringLiteral("'");
    cmd += QStringLiteral(" -m '") + m_settings->piperModelPath() + QStringLiteral("'");
    cmd += QStringLiteral(" --output_raw -q");
    cmd += QStringLiteral(" --length-scale ") + QString::number(lengthScale, 'f', 2);
    cmd += QStringLiteral(" --sentence-silence 0.2");
    cmd += QStringLiteral(" | pw-cat --playback --raw --rate=22050 --channels=1 --format=s16 --quality=10 -");

    m_sentenceProc = new QProcess(this);
    connect(m_sentenceProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
        if (m_sentenceProc) {
            m_sentenceProc->deleteLater();
            m_sentenceProc = nullptr;
        }
        // Don't continue if stop() was called
        if (m_playingBack) processNextSentence();
    });

    m_sentenceProc->start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), cmd});
}

void JarvisTts::stop()
{
    {
        QMutexLocker lock(&m_queueMutex);
        m_sentenceQueue.clear();
    }
    m_playingBack = false;

    // Kill any in-flight sentence process
    if (m_sentenceProc) {
        m_sentenceProc->kill();
        m_sentenceProc->deleteLater();
        m_sentenceProc = nullptr;
    }
    if (m_tts) {
        m_tts->stop();
    }

    if (m_speaking.load()) {
        m_speaking = false;
        emit speakingChanged();
    }
}

void JarvisTts::toggleMute()
{
    m_settings->setTtsMuted(!m_settings->ttsMuted());
    if (m_settings->ttsMuted()) {
        stop();
    }
}

bool JarvisTts::isMuted() const
{
    return m_settings->ttsMuted();
}

void JarvisTts::onTtsRateChanged()
{
    if (!m_usePiper && m_tts) m_tts->setRate(m_settings->ttsRate());
    // Piper uses --length-scale at speak time — no action needed
}

void JarvisTts::onTtsPitchChanged()
{
    if (!m_usePiper && m_tts) m_tts->setPitch(m_settings->ttsPitch());
}

void JarvisTts::onTtsVolumeChanged()
{
    if (!m_usePiper && m_tts) {
        m_tts->setVolume(m_settings->ttsVolume());
    }
}

void JarvisTts::onVoiceActivated(const QString &voiceId, const QString &onnxPath)
{
    Q_UNUSED(voiceId)
    Q_UNUSED(onnxPath)
    // Piper model path is read from settings at speak time
}
