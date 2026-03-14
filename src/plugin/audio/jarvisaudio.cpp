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
    initRnnoise();
    m_downloadManager = new QNetworkAccessManager(this);
    ensureModelsDownloaded();

    // Check for wake word every second, even though the buffer holds wakeBufferSeconds of audio
    m_audioProcessTimer->setInterval(1000);
    connect(m_audioProcessTimer, &QTimer::timeout, this, &JarvisAudio::processAudioBuffer);

    m_voiceCmdTimer->setSingleShot(true);
    m_voiceCmdTimer->setInterval(m_settings->voiceCmdMaxSeconds() * 1000);
    connect(m_voiceCmdTimer, &QTimer::timeout, this, &JarvisAudio::processVoiceCommand);

    // Silence detection — checks audio energy during voice command mode
    m_silenceChunksNeeded = m_settings->silenceTimeoutMs() / SILENCE_CHECK_MS;
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
            if (m_silentChunks >= m_silenceChunksNeeded) {
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
    if (m_rnnoise) {
        rnnoise_destroy(m_rnnoise);
        m_rnnoise = nullptr;
    }
    if (m_vadCtx) {
        whisper_vad_free(m_vadCtx);
        m_vadCtx = nullptr;
    }
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

bool JarvisAudio::isMicBusy() const
{
    return m_micMonitor && m_micMonitor->isMicBusy();
}

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

    // Denoise before speech detection for cleaner VAD/whisper input
    const QByteArray cleanAudio = (m_rnnoise && m_settings->noiseSuppressionEnabled()) ? denoiseAudio(bufferCopy) : bufferCopy;
    const auto floatBuf = pcm16ToFloat(cleanAudio);
    if (m_vadCtx) {
        if (!whisper_vad_detect_speech(m_vadCtx, floatBuf.data(), static_cast<int>(floatBuf.size())))
            return;
    } else {
        // Fallback: RMS energy threshold
        double sumSquares = 0.0;
        for (const float s : floatBuf) sumSquares += s * s;
        const double rms = qSqrt(sumSquares / floatBuf.size()) * 32768.0;
        if (rms < 1500) return;
    }

    m_whisperBusy = true;
    [[maybe_unused]] auto f = QtConcurrent::run([this, cleanAudio]() {
        const QString matched = detectWakeWord(cleanAudio);
        m_whisperBusy = false;

        if (!matched.isEmpty()) {
            QMetaObject::invokeMethod(this, [this, matched]() {
                emit wakeWordMatch(matched);
            }, Qt::QueuedConnection);
        }
    });
}

QString JarvisAudio::detectWakeWord(const QByteArray &audioData)
{
    if (!m_whisperCtx) return {};

    auto floatSamples = pcm16ToFloat(audioData);
    if (floatSamples.empty()) return {};

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
        return {};
    }

    const int nSegments = whisper_full_n_segments(m_whisperCtx);
    for (int i = 0; i < nSegments; ++i) {
        const char *text = whisper_full_get_segment_text(m_whisperCtx, i);
        if (!text) continue;

        QString transcript = QString::fromUtf8(text).toLower().trimmed();
        qDebug() << "[JARVIS] Whisper heard:" << transcript;

        // Check primary wake word
        const QString wakeWord = m_settings->wakeWord().toLower();
        if (transcript.contains(wakeWord))
            return wakeWord;
        if (wakeWord.length() >= 3) {
            const QString prefix = wakeWord.left(wakeWord.length() - 1);
            if (transcript.contains(prefix)) return wakeWord;
        }

        // Check provider wake words (e.g. "claude", "gemini", "ollama")
        static const QStringList providerWords = {
            QStringLiteral("claude"), QStringLiteral("gemini"),
            QStringLiteral("ollama"), QStringLiteral("openai"),
            QStringLiteral("chatgpt"),
        };
        for (const auto &pw : providerWords) {
            if (pw == wakeWord) continue; // Already checked above
            if (transcript.contains(pw)) return pw;
        }

        // Check model names that might be used as wake words
        static const QStringList modelWords = {
            QStringLiteral("qwen"), QStringLiteral("llama"),
            QStringLiteral("mistral"), QStringLiteral("opus"),
            QStringLiteral("sonnet"), QStringLiteral("haiku"),
        };
        for (const auto &mw : modelWords) {
            if (transcript.contains(mw)) return mw;
        }
    }

    return {};
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
    // Search for the configured model size first, then fall back to smaller models
    const QString preferred = m_settings->whisperModel(); // "tiny", "base", "small"
    const QStringList sizes = {preferred, QStringLiteral("tiny"), QStringLiteral("base"), QStringLiteral("small")};
    const QStringList dirs = {
        QDir::homePath() + QStringLiteral("/.local/share/jarvis/"),
        QStringLiteral("/usr/share/jarvis/"),
        QStringLiteral("/usr/local/share/jarvis/"),
        QDir::homePath() + QStringLiteral("/.local/share/whisper/"),
        QStringLiteral("/usr/share/whisper/"),
    };

    for (const auto &size : sizes) {
        for (const auto &dir : dirs) {
            // Try multilingual first, then english-only
            const QString multi = dir + QStringLiteral("ggml-%1.bin").arg(size);
            if (QFileInfo::exists(multi)) return multi;
            const QString en = dir + QStringLiteral("ggml-%1.en.bin").arg(size);
            if (QFileInfo::exists(en)) return en;
        }
    }
    return {};
}

void JarvisAudio::initVad()
{
    const QString modelPath = findVadModel();
    if (modelPath.isEmpty()) {
        qDebug() << "[JARVIS] VAD model not found — using RMS energy fallback.";
        qDebug() << "[JARVIS] Download silero-vad.onnx from whisper.cpp and place in ~/.local/share/jarvis/";
        return;
    }

    qDebug() << "[JARVIS] Loading VAD model from:" << modelPath;

    whisper_vad_context_params params = whisper_vad_default_context_params();
    params.use_gpu = false;
    params.n_threads = 1;

    m_vadCtx = whisper_vad_init_from_file_with_params(
        modelPath.toUtf8().constData(), params);

    if (!m_vadCtx) {
        qWarning() << "[JARVIS] Failed to initialize VAD from:" << modelPath;
    } else {
        qDebug() << "[JARVIS] VAD model loaded successfully (Silero).";
    }
}

QString JarvisAudio::findVadModel() const
{
    const QStringList searchPaths = {
        QDir::homePath() + QStringLiteral("/.local/share/jarvis/silero-vad.onnx"),
        QStringLiteral("/usr/share/jarvis/silero-vad.onnx"),
        QStringLiteral("/usr/local/share/jarvis/silero-vad.onnx"),
        QDir::homePath() + QStringLiteral("/.local/share/whisper/silero-vad.onnx"),
        QStringLiteral("/usr/share/whisper/silero-vad.onnx"),
    };

    for (const auto &path : searchPaths) {
        if (QFileInfo::exists(path)) return path;
    }
    return {};
}

void JarvisAudio::initRnnoise()
{
    m_rnnoise = rnnoise_create(nullptr);
    if (m_rnnoise) {
        qDebug() << "[JARVIS] RNNoise initialized (frame size:" << rnnoise_get_frame_size() << ")";
    } else {
        qWarning() << "[JARVIS] Failed to initialize RNNoise";
    }
}

QByteArray JarvisAudio::denoiseAudio(const QByteArray &pcm16) const
{
    if (!m_rnnoise) return pcm16;

    // RNNoise expects 480-sample frames at 48kHz (float).
    // Our audio is 16kHz PCM16. We upsample 3x, denoise, downsample.
    constexpr int RNNOISE_FRAME = 480;
    constexpr int INPUT_FRAME = RNNOISE_FRAME / 3; // 160 samples at 16kHz = 10ms

    const auto *in = reinterpret_cast<const int16_t*>(pcm16.constData());
    const int totalSamples = pcm16.size() / static_cast<int>(sizeof(int16_t));

    QByteArray result;
    result.reserve(pcm16.size());

    // Process in 160-sample (10ms) chunks
    for (int offset = 0; offset + INPUT_FRAME <= totalSamples; offset += INPUT_FRAME) {
        // Upsample 16kHz → 48kHz with linear interpolation
        // RNNoise expects float samples at int16 scale (±32768)
        float upsampled[RNNOISE_FRAME];
        for (int i = 0; i < INPUT_FRAME; ++i) {
            const float s0 = static_cast<float>(in[offset + i]);
            const float s1 = (i + 1 < INPUT_FRAME)
                ? static_cast<float>(in[offset + i + 1]) : s0;
            upsampled[i * 3]     = s0;
            upsampled[i * 3 + 1] = s0 + (s1 - s0) * (1.0f / 3.0f);
            upsampled[i * 3 + 2] = s0 + (s1 - s0) * (2.0f / 3.0f);
        }

        // Denoise
        float denoised[RNNOISE_FRAME];
        rnnoise_process_frame(m_rnnoise, denoised, upsampled);

        // Downsample 48kHz → 16kHz (average 3 samples per output sample)
        for (int i = 0; i < INPUT_FRAME; ++i) {
            const float avg = (denoised[i * 3] + denoised[i * 3 + 1] + denoised[i * 3 + 2]) / 3.0f;
            const auto s = static_cast<int16_t>(qBound(-32768.0f, avg, 32767.0f));
            result.append(reinterpret_cast<const char*>(&s), 2);
        }
    }

    // Append any remaining samples unprocessed
    const int processed = (totalSamples / INPUT_FRAME) * INPUT_FRAME;
    if (processed < totalSamples) {
        result.append(reinterpret_cast<const char*>(in + processed),
                      (totalSamples - processed) * sizeof(int16_t));
    }

    return result;
}

void JarvisAudio::ensureModelsDownloaded()
{
    const QString dataDir = QDir::homePath() + QStringLiteral("/.local/share/jarvis");
    QDir().mkpath(dataDir);

    // Determine which whisper model to download based on settings
    const QString whisperSize = m_settings->whisperModel(); // "tiny", "base", "small"
    const QString whisperFile = QStringLiteral("ggml-%1.bin").arg(whisperSize);
    const QString whisperUrl = QStringLiteral(
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/%1").arg(whisperFile);
    const QString whisperDest = dataDir + QStringLiteral("/") + whisperFile;

    const QString vadUrl = QStringLiteral(
        "https://github.com/ggerganov/whisper.cpp/raw/master/models/silero-vad.onnx");
    const QString vadDest = dataDir + QStringLiteral("/silero-vad.onnx");

    const bool needWhisper = findWhisperModel().isEmpty();
    const bool needVad = findVadModel().isEmpty();

    if (!needWhisper && !needVad) {
        // All models present — init immediately
        initWhisper();
        initVad();
        return;
    }

    // Track pending downloads
    auto *pending = new std::atomic<int>(0);
    if (needWhisper) ++(*pending);
    if (needVad) ++(*pending);

    auto tryInit = [this, pending]() {
        if (--(*pending) == 0) {
            delete pending;
            initWhisper();
            initVad();
            emit modelDownloadStatus(QStringLiteral("Models ready."));

            // Auto-start wake word if configured
            if (m_settings->autoStartWakeWord() && m_whisperCtx && m_audioSource) {
                m_wakeWordActive = true;
                emit wakeWordActiveChanged();
                startListening();
                qDebug() << "[JARVIS] Wake word detection auto-started after model download.";
            }
        }
    };

    if (needWhisper) {
        emit modelDownloadStatus(QStringLiteral("Downloading Whisper %1 model...").arg(whisperSize));
        downloadModel(whisperUrl, whisperDest, tryInit);
    }
    if (needVad) {
        emit modelDownloadStatus(QStringLiteral("Downloading VAD model..."));
        downloadModel(vadUrl, vadDest, tryInit);
    }
}

void JarvisAudio::downloadModel(const QString &url, const QString &destPath,
                                 const std::function<void()> &onComplete)
{
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    auto *reply = m_downloadManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, destPath, onComplete]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[JARVIS] Model download failed:" << reply->errorString();
            emit modelDownloadStatus(QStringLiteral("Download failed: %1").arg(reply->errorString()));
            onComplete(); // Still call to decrement counter
            return;
        }

        QFile file(destPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(reply->readAll());
            file.close();
            qDebug() << "[JARVIS] Model downloaded:" << destPath
                     << "(" << QFileInfo(destPath).size() / 1024 << "KB)";
        } else {
            qWarning() << "[JARVIS] Failed to write model:" << file.errorString();
        }
        onComplete();
    });
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

    // Denoise before transcription
    const QByteArray cleanData = (m_rnnoise && m_settings->noiseSuppressionEnabled()) ? denoiseAudio(audioData) : audioData;

    [[maybe_unused]] auto f = QtConcurrent::run([this, cleanData]() {
        const QString text = transcribeAudio(cleanData);

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

void JarvisAudio::updateSilenceTimeout(int ms)
{
    m_silenceChunksNeeded = qMax(1, ms / SILENCE_CHECK_MS);
}
