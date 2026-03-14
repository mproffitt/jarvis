#include "jarvisaudio.h"
#include "micmonitor.h"
#include "../settings/jarvissettings.h"

#include <QDir>
#include <QFileInfo>
#include <QAudioDevice>
#include <QRegularExpression>
#include <QtConcurrent>
#include <QtMath>
#include <thread>
#include <QDebug>

JarvisAudio::JarvisAudio(JarvisSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_audioProcessTimer(new QTimer(this))
    , m_voiceCmdTimer(new QTimer(this))
    , m_silenceTimer(new QTimer(this))
{
    initAudioCapture();
    initWhisper();

    // Check for wake word every second, even though the buffer holds wakeBufferSeconds of audio
    m_audioProcessTimer->setInterval(1000);
    connect(m_audioProcessTimer, &QTimer::timeout, this, &JarvisAudio::processAudioBuffer);

    m_voiceCmdTimer->setSingleShot(true);
    m_voiceCmdTimer->setInterval(m_settings->voiceCmdMaxSeconds() * 1000);
    connect(m_voiceCmdTimer, &QTimer::timeout, this, &JarvisAudio::processVoiceCommand);

    // Silence detection — checks audio energy during voice command mode
    m_silenceTimer->setInterval(SILENCE_CHECK_MS);
    connect(m_silenceTimer, &QTimer::timeout, this, [this]() {
        if (!m_voiceCommandMode) return;

        // Check energy of the most recent audio chunk
        const double level = m_audioLevel;
        if (level > SILENCE_THRESHOLD) {
            m_speechStarted = true;
            m_silentChunks = 0;
        } else if (m_speechStarted) {
            ++m_silentChunks;
            if (m_silentChunks >= SILENCE_CHUNKS_NEEDED) {
                qDebug() << "[JARVIS] Silence detected, stopping voice command";
                m_voiceCmdTimer->stop();
                m_silenceTimer->stop();
                processVoiceCommand();
            }
        }
    });

    // PipeWire mic-busy detection
    m_micMonitor = new MicMonitor(this);
    connect(m_micMonitor, &MicMonitor::micBusyChanged, this, &JarvisAudio::micBusyChanged);
    m_micMonitor->start();

    // Auto-start wake word detection
    if (m_settings->autoStartWakeWord() && m_whisperCtx && m_audioSource) {
        m_wakeWordActive = true;
        emit wakeWordActiveChanged();
        startListening();
        qDebug() << "[JARVIS] Wake word detection auto-started.";
    }
}

JarvisAudio::~JarvisAudio()
{
    stopListening();
    if (m_whisperCtx) {
        whisper_free(m_whisperCtx);
        m_whisperCtx = nullptr;
    }
}

// ─────────────────────────────────────────────
// Audio Capture
// ─────────────────────────────────────────────

void JarvisAudio::initAudioCapture()
{
    m_mediaDevices = new QMediaDevices(this);

    auto setupAudioDevice = [this]() {
        const bool wasListening = m_listening.load();
        if (wasListening) {
            stopListening();
        }

        delete m_audioSource;
        m_audioSource = nullptr;

        QAudioFormat format;
        format.setSampleRate(JARVIS_SAMPLE_RATE);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);

        const auto defaultDevice = QMediaDevices::defaultAudioInput();
        if (!defaultDevice.isNull() && defaultDevice.isFormatSupported(format)) {
            m_audioSource = new QAudioSource(defaultDevice, format, this);
            m_audioSource->setVolume(1.0);
            qDebug() << "[JARVIS] Audio capture initialized on:" << defaultDevice.description();
        } else {
            qWarning() << "[JARVIS] Default audio input not available or format unsupported.";
        }

        if (wasListening && m_audioSource) {
            startListening();
        }
    };

    setupAudioDevice();
    connect(m_mediaDevices, &QMediaDevices::audioInputsChanged, this, setupAudioDevice);
}

void JarvisAudio::startListening()
{
    if (!m_audioSource) {
        qWarning() << "[JARVIS] Cannot start listening: no audio source";
        return;
    }

    {
        QMutexLocker lock(&m_audioMutex);
        m_audioBuffer.clear();
    }

    if (m_audioDevice) {
        disconnect(m_audioDevice, nullptr, this, nullptr);
    }

    m_audioDevice = m_audioSource->start();

    if (m_audioDevice) {
        connect(m_audioDevice, &QIODevice::readyRead, this, [this]() {
            const auto data = m_audioDevice->readAll();

            {
                QMutexLocker lock(&m_audioMutex);
                m_audioBuffer.append(data);
            }

            const auto *samples = reinterpret_cast<const int16_t*>(data.constData());
            const int numSamples = data.size() / static_cast<int>(sizeof(int16_t));
            double sum = 0.0;
            for (int i = 0; i < numSamples; ++i) {
                sum += qAbs(static_cast<double>(samples[i]));
            }
            m_audioLevel = numSamples > 0 ? sum / (numSamples * 32768.0) : 0.0;
            emit audioLevelChanged();
        });
        m_audioProcessTimer->start();
        qDebug() << "[JARVIS] Listening started, audio process timer active";
    } else {
        qWarning() << "[JARVIS] Failed to start audio device";
    }

    m_listening = true;
    emit listeningChanged();
}

void JarvisAudio::stopListening()
{
    if (m_audioSource) {
        m_audioSource->stop();
    }
    m_audioProcessTimer->stop();
    m_audioDevice = nullptr;
    {
        QMutexLocker lock(&m_audioMutex);
        m_audioBuffer.clear();
    }
    m_audioLevel = 0.0;

    m_listening = false;
    emit listeningChanged();
    emit audioLevelChanged();
}

// ─────────────────────────────────────────────
// Wake Word Detection
// ─────────────────────────────────────────────

void JarvisAudio::setTtsSpeaking(bool speaking)
{
    m_ttsSpeaking = speaking;
    if (speaking) {
        // Clear audio buffer so we don't process TTS output as speech
        QMutexLocker lock(&m_audioMutex);
        m_audioBuffer.clear();
    }
}

void JarvisAudio::processAudioBuffer()
{
    if (m_ttsSpeaking.load()) return; // Don't process while TTS is playing
    if (m_micMonitor && m_micMonitor->isMicBusy()) return;
    if (m_voiceCommandMode) return;
    if (!m_wakeWordActive || !m_whisperCtx) return;
    if (m_whisperBusy.load()) return;

    QByteArray bufferCopy;
    {
        QMutexLocker lock(&m_audioMutex);
        // Use 1.5 seconds of audio — enough for a wake word, fast to process
        const int wakeBufferSize = JARVIS_SAMPLE_RATE * 3; // 1.5s * 2 bytes/sample
        if (m_audioBuffer.size() < wakeBufferSize) return;
        bufferCopy = m_audioBuffer.right(wakeBufferSize);
        // Keep half the buffer for overlap — prevents missing wake words at boundaries
        const int keepSize = wakeBufferSize / 2;
        if (m_audioBuffer.size() > keepSize)
            m_audioBuffer = m_audioBuffer.right(keepSize);
    }

    const auto *samples = reinterpret_cast<const int16_t*>(bufferCopy.constData());
    const int numSamples = bufferCopy.size() / static_cast<int>(sizeof(int16_t));
    double sumSquares = 0.0;
    for (int i = 0; i < numSamples; ++i) {
        const double s = static_cast<double>(samples[i]);
        sumSquares += s * s;
    }
    const double rms = qSqrt(sumSquares / numSamples);
    if (rms < 1500) return;

    m_whisperBusy = true;
    [[maybe_unused]] auto f = QtConcurrent::run([this, bufferCopy]() {
        const bool detected = detectWakeWord(bufferCopy);
        m_whisperBusy = false;

        if (detected) {
            QMetaObject::invokeMethod(this, [this]() {
                emit wakeWordDetected();
                startVoiceCommand();
            }, Qt::QueuedConnection);
        }
    });
}

bool JarvisAudio::detectWakeWord(const QByteArray &audioData)
{
    if (!m_whisperCtx) return false;

    auto floatSamples = pcm16ToFloat(audioData);
    if (floatSamples.empty()) return false;

    QMutexLocker lock(&m_whisperMutex);

    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.print_progress   = false;
    params.print_special    = false;
    params.print_realtime   = false;
    params.print_timestamps = false;
    params.single_segment   = true;
    params.no_context       = true;
    params.language         = "en";
    params.n_threads        = std::min(4, static_cast<int>(std::thread::hardware_concurrency()));
    params.audio_ctx        = 768; // ~1.5s context, enough for a wake word
    params.suppress_nst     = true;

    const int ret = whisper_full(m_whisperCtx, params,
                                 floatSamples.data(),
                                 static_cast<int>(floatSamples.size()));
    if (ret != 0) {
        qWarning() << "[JARVIS] Whisper inference failed with code:" << ret;
        return false;
    }

    const int nSegments = whisper_full_n_segments(m_whisperCtx);
    for (int i = 0; i < nSegments; ++i) {
        const char *text = whisper_full_get_segment_text(m_whisperCtx, i);
        if (!text) continue;

        QString transcript = QString::fromUtf8(text).toLower().trimmed();
        qDebug() << "[JARVIS] Whisper heard:" << transcript;

        const QString wakeWord = m_settings->wakeWord().toLower();
        // Check for the wake word and common misheard variants
        if (transcript.contains(wakeWord)) {
            return true;
        }
        // Generate fuzzy variants: drop last char, swap vowels
        if (wakeWord.length() >= 3) {
            const QString prefix = wakeWord.left(wakeWord.length() - 1);
            if (transcript.contains(prefix)) return true;
        }
    }

    return false;
}

QString JarvisAudio::transcribeAudio(const QByteArray &audioData)
{
    if (!m_whisperCtx) return {};

    auto floatSamples = pcm16ToFloat(audioData);
    if (floatSamples.empty()) return {};

    QMutexLocker lock(&m_whisperMutex);

    // Trim trailing silence to reduce audio Whisper needs to process
    int numSamples = static_cast<int>(floatSamples.size());
    while (numSamples > JARVIS_SAMPLE_RATE / 2 && qAbs(floatSamples[numSamples - 1]) < 0.002f) {
        --numSamples;
    }
    // Round up to nearest 100ms boundary
    const int boundary = JARVIS_SAMPLE_RATE / 10;
    numSamples = ((numSamples + boundary - 1) / boundary) * boundary;
    if (numSamples > static_cast<int>(floatSamples.size()))
        numSamples = static_cast<int>(floatSamples.size());

    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.print_progress   = false;
    params.print_special    = false;
    params.print_realtime   = false;
    params.print_timestamps = false;
    params.single_segment   = true;
    params.no_context       = true;
    params.language         = nullptr; // auto-detect language
    params.n_threads        = std::min(4, static_cast<int>(std::thread::hardware_concurrency()));

    const int ret = whisper_full(m_whisperCtx, params,
                                 floatSamples.data(), numSamples);
    if (ret != 0) return {};

    QString result;
    const int nSegments = whisper_full_n_segments(m_whisperCtx);
    for (int i = 0; i < nSegments; ++i) {
        const char *text = whisper_full_get_segment_text(m_whisperCtx, i);
        if (text) {
            result += QString::fromUtf8(text).trimmed();
            if (i < nSegments - 1) result += QStringLiteral(" ");
        }
    }

    return result.trimmed();
}

void JarvisAudio::initWhisper()
{
    const QString modelPath = findWhisperModel();
    if (modelPath.isEmpty()) {
        qWarning() << "[JARVIS] Whisper model not found. Wake word detection unavailable.";
        qWarning() << "[JARVIS] Download ggml-tiny.bin from: https://huggingface.co/ggerganov/whisper.cpp/tree/main";
        qWarning() << "[JARVIS] Place it in ~/.local/share/jarvis/ or /usr/share/jarvis/";
        return;
    }

    qDebug() << "[JARVIS] Loading whisper model from:" << modelPath;

    whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = false;

    m_whisperCtx = whisper_init_from_file_with_params(
        modelPath.toUtf8().constData(), cparams);

    if (!m_whisperCtx) {
        qWarning() << "[JARVIS] Failed to initialize whisper context from:" << modelPath;
    } else {
        qDebug() << "[JARVIS] Whisper model loaded successfully (tiny).";
    }
}

QString JarvisAudio::findWhisperModel() const
{
    const QStringList searchPaths = {
        QDir::homePath() + QStringLiteral("/.local/share/jarvis/ggml-tiny.bin"),
        QDir::homePath() + QStringLiteral("/.local/share/jarvis/ggml-tiny.en.bin"),
        QStringLiteral("/usr/share/jarvis/ggml-tiny.bin"),
        QStringLiteral("/usr/share/jarvis/ggml-tiny.en.bin"),
        QStringLiteral("/usr/local/share/jarvis/ggml-tiny.bin"),
        QStringLiteral("/usr/local/share/jarvis/ggml-tiny.en.bin"),
        QDir::homePath() + QStringLiteral("/.local/share/whisper/ggml-tiny.bin"),
        QDir::homePath() + QStringLiteral("/.local/share/whisper/ggml-tiny.en.bin"),
        QStringLiteral("/usr/share/whisper/ggml-tiny.bin"),
        QStringLiteral("/usr/share/whisper/ggml-tiny.en.bin"),
    };

    for (const auto &path : searchPaths) {
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return {};
}

std::vector<float> JarvisAudio::pcm16ToFloat(const QByteArray &audioData) const
{
    const auto *samples = reinterpret_cast<const int16_t*>(audioData.constData());
    const int numSamples = audioData.size() / static_cast<int>(sizeof(int16_t));

    std::vector<float> result(numSamples);
    for (int i = 0; i < numSamples; ++i) {
        result[i] = static_cast<float>(samples[i]) / 32768.0f;
    }
    return result;
}

// ─────────────────────────────────────────────
// Voice Command Mode
// ─────────────────────────────────────────────

void JarvisAudio::startVoiceCommand()
{
    if (m_micMonitor && m_micMonitor->isMicBusy()) return;
    if (!m_whisperCtx || !m_audioSource) return;

    m_voiceCommandMode = true;
    emit voiceCommandModeChanged();

    {
        // Keep recent audio — the user may have started speaking right after the wake word
        QMutexLocker lock(&m_audioMutex);
        const int keepSize = JARVIS_SAMPLE_RATE * 2; // keep last ~1s
        if (m_audioBuffer.size() > keepSize)
            m_audioBuffer = m_audioBuffer.right(keepSize);
    }

    if (!m_listening) {
        startListening();
    }

    // Reset silence detection state
    m_silentChunks = 0;
    m_speechStarted = false;

    m_voiceCmdTimer->start();
    m_silenceTimer->start();
}

void JarvisAudio::stopVoiceCommand()
{
    m_voiceCmdTimer->stop();
    m_silenceTimer->stop();
    processVoiceCommand();
}

void JarvisAudio::processVoiceCommand()
{
    if (!m_voiceCommandMode) return;

    m_voiceCommandMode = false;
    emit voiceCommandModeChanged();

    QByteArray audioData;
    {
        QMutexLocker lock(&m_audioMutex);
        audioData = m_audioBuffer;
        m_audioBuffer.clear();
    }

    if (audioData.size() < JARVIS_SAMPLE_RATE * 2) {
        emit voiceCommandTranscribed(QString());
        return;
    }

    const int voiceCmdBufferSize = JARVIS_SAMPLE_RATE * 2 * m_settings->voiceCmdMaxSeconds();
    if (audioData.size() > voiceCmdBufferSize) {
        audioData = audioData.right(voiceCmdBufferSize);
    }

    [[maybe_unused]] auto f = QtConcurrent::run([this, audioData]() {
        const QString text = transcribeAudio(audioData);

        QMetaObject::invokeMethod(this, [this, text]() {
            m_lastTranscription = text;
            emit lastTranscriptionChanged();
            emit voiceCommandTranscribed(text);
        }, Qt::QueuedConnection);
    });
}

void JarvisAudio::toggleWakeWord()
{
    m_wakeWordActive = !m_wakeWordActive.load();
    emit wakeWordActiveChanged();

    if (m_wakeWordActive) {
        startListening();
    } else {
        stopListening();
    }
}

void JarvisAudio::updateWakeBufferInterval(int /* seconds */)
{
    // Check interval stays at 1s for responsiveness; buffer size uses wakeBufferSeconds
    m_audioProcessTimer->setInterval(1000);
}

void JarvisAudio::updateVoiceCmdTimeout(int seconds)
{
    m_voiceCmdTimer->setInterval(seconds * 1000);
}
