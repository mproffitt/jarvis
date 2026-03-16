#include "jarvisaudio.h"
#include "pwcapture.h"
#include "micmonitor.h"
#include "../settings/jarvissettings.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtConcurrent>
#include <QtMath>
#include <QDebug>

#include <pipewire/pipewire.h>

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

    // Fallback wake word check every 1.5s
    m_audioProcessTimer->setInterval(1500);
    connect(m_audioProcessTimer, &QTimer::timeout, this, &JarvisAudio::processAudioBuffer);

    // Fast VAD pre-screen every 200ms — triggers immediate whisper when speech detected
    m_vadCheckTimer = new QTimer(this);
    m_vadCheckTimer->setInterval(200);
    connect(m_vadCheckTimer, &QTimer::timeout, this, [this]() {
        if (!m_vadCtx || !m_wakeWordActive || m_voiceCommandMode || m_whisperBusy.load()) return;
        if (m_micMonitor && m_micMonitor->isMicBusy()) return;

        // Check the latest audio for speech
        QByteArray chunk;
        {
            QMutexLocker lock(&m_audioMutex);
            // Need at least 0.5s of audio for VAD
            const int minSize = JARVIS_SAMPLE_RATE; // 0.5s * 2 bytes/sample
            if (m_audioBuffer.size() < minSize) return;
            chunk = m_audioBuffer.right(minSize);
        }

        const auto floats = pcm16ToFloat(chunk);
        if (whisper_vad_detect_speech(m_vadCtx, floats.data(), static_cast<int>(floats.size()))) {
            // Speech detected — trigger wake word processing immediately
            m_audioProcessTimer->stop();
            processAudioBuffer();
            m_audioProcessTimer->start();
        }
    });

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
    if (m_settings->autoStartWakeWord() && m_whisperCtx) {
        m_wakeWordActive = true;
        emit wakeWordActiveChanged();
        startListening();
        qDebug() << "[JARVIS] Wake word detection auto-started.";
    }
}

JarvisAudio::~JarvisAudio()
{
    stopListening();
    pw_deinit();
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
    pw_init(nullptr, nullptr);

    m_capture = new PwCapture(this);

    connect(m_capture, &PwCapture::audioData, this, [this](const QByteArray &pcm) {
        QMutexLocker lock(&m_audioMutex);
        m_audioBuffer.append(pcm);
    });

    connect(m_capture, &PwCapture::audioLevel, this, [this](double level) {
        m_audioLevel = level;
        emit audioLevelChanged();
    });

    connect(m_capture, &PwCapture::connected, this, [this]() {
        qDebug() << "[JARVIS] PipeWire capture connected";
    });

    connect(m_capture, &PwCapture::disconnected, this, [this]() {
        qDebug() << "[JARVIS] PipeWire capture disconnected, will reconnect";
    });
}

void JarvisAudio::startListening()
{
    {
        QMutexLocker lock(&m_audioMutex);
        m_audioBuffer.clear();
    }

    // Start the PipeWire capture if not already running — keep it
    // running across start/stop cycles to avoid stream churn.
    if (!m_capture->isRunning())
        m_capture->start();

    m_audioProcessTimer->start();
    if (m_vadCtx) m_vadCheckTimer->start();
    qDebug() << "[JARVIS] Listening started";

    m_listening = true;
    emit listeningChanged();
}

void JarvisAudio::stopListening()
{
    // Don't stop the PipeWire capture — just stop processing.
    // The stream stays alive so we don't get stream churn in PipeWire.
    m_audioProcessTimer->stop();
    m_vadCheckTimer->stop();
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
    const bool wasSpeaking = m_ttsSpeaking.exchange(speaking);
    if (speaking && !wasSpeaking) {
        // TTS started — clear buffer but keep processing for wake word interruption
        QMutexLocker lock(&m_audioMutex);
        m_audioBuffer.clear();
    } else if (!speaking && wasSpeaking) {
        // TTS stopped — clear any TTS echo from the buffer
        QMutexLocker lock(&m_audioMutex);
        m_audioBuffer.clear();
    }
}

void JarvisAudio::processAudioBuffer()
{
    // During TTS: still process for wake word interruption, but only check wake word
    // (skip voice command mode entry from other triggers)
    if (m_micMonitor && m_micMonitor->isMicBusy()) return;
    if (m_voiceCommandMode) return;
    if (!m_wakeWordActive || !m_whisperCtx) return;
    if (m_whisperBusy.load()) return;

    QByteArray bufferCopy;
    {
        QMutexLocker lock(&m_audioMutex);
        // Use 2 seconds of audio for better wake word recognition
        const int wakeBufferSize = JARVIS_SAMPLE_RATE * 2 * 2; // 2s * 2 bytes/sample
        if (m_audioBuffer.size() < wakeBufferSize) return;
        bufferCopy = m_audioBuffer.right(wakeBufferSize);
        // Keep half the buffer for overlap — prevents missing wake words at boundaries
        const int keepSize = wakeBufferSize / 2;
        if (m_audioBuffer.size() > keepSize)
            m_audioBuffer = m_audioBuffer.right(keepSize);
    }

    // Skip denoising for wake word — it adds latency and VAD handles noise filtering
    const auto floatBuf = pcm16ToFloat(bufferCopy);
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
    [[maybe_unused]] auto f = QtConcurrent::run([this, bufferCopy]() {
        const QString matched = detectWakeWord(bufferCopy);
        m_whisperBusy = false;

        // Log what whisper heard (for diagnostics)
        const QString heard = m_lastWakeTranscript;
        QMetaObject::invokeMethod(this, [this, heard]() {
            emit whisperHeard(QStringLiteral("wake"),
                heard.isEmpty() ? QStringLiteral("(silence)") : heard);
        }, Qt::QueuedConnection);

        if (!matched.isEmpty()) {
            const bool wasTtsSpeaking = m_ttsSpeaking.load();
            QMetaObject::invokeMethod(this, [this, matched, wasTtsSpeaking]() {
                if (wasTtsSpeaking) emit ttsInterrupted();
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
    params.audio_ctx        = 768; // Wider context for better wake word recognition
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

        // Use whisper's own no-speech probability to filter hallucinations
        const float noSpeechProb = whisper_full_get_segment_no_speech_prob(m_whisperCtx, i);
        if (noSpeechProb > 0.6f) {
            m_lastWakeTranscript = QStringLiteral("(no_speech %.0f%%: %1)")
                .arg(static_cast<double>(noSpeechProb * 100.0f), 0, 'f', 0).arg(transcript);
            qDebug() << "[JARVIS] Whisper hallucination (no_speech_prob:" << noSpeechProb << "):" << transcript;
            continue;
        }

        m_lastWakeTranscript = QStringLiteral("%1 [%.0f%%]")
            .arg(transcript).arg(static_cast<double>((1.0f - noSpeechProb) * 100.0f), 0, 'f', 0);
        qDebug() << "[JARVIS] Whisper heard:" << transcript << "no_speech_prob:" << noSpeechProb;

        // Check primary wake word with fuzzy matching
        const QString wakeWord = m_settings->wakeWord().toLower();
        if (transcript.contains(wakeWord))
            return wakeWord;
        // Prefix match (e.g. "jarvi" matches "jarvis")
        if (wakeWord.length() >= 3) {
            const QString prefix = wakeWord.left(wakeWord.length() - 1);
            if (transcript.contains(prefix)) return wakeWord;
        }
        // Levenshtein fuzzy match: allow edit distance <= 2 for words >= 4 chars
        if (wakeWord.length() >= 4) {
            // Extract words from transcript and check each
            const auto words = transcript.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            for (const auto &word : words) {
                const int wLen = wakeWord.length();
                const int tLen = word.length();
                // Skip words too different in length
                if (qAbs(wLen - tLen) > 2) continue;

                // Levenshtein distance
                QList<int> prev(tLen + 1), curr(tLen + 1);
                for (int j = 0; j <= tLen; ++j) prev[j] = j;
                for (int i = 1; i <= wLen; ++i) {
                    curr[0] = i;
                    for (int j = 1; j <= tLen; ++j) {
                        int cost = (wakeWord[i - 1] == word[j - 1]) ? 0 : 1;
                        curr[j] = std::min({prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost});
                    }
                    std::swap(prev, curr);
                }
                if (prev[tLen] <= 2) return wakeWord;
            }
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
        const float noSpeechProb = whisper_full_get_segment_no_speech_prob(m_whisperCtx, i);
        if (noSpeechProb > 0.6f) {
            qDebug() << "[JARVIS] Transcription: skipping hallucinated segment (no_speech:" << noSpeechProb << ")";
            continue;
        }
        const char *text = whisper_full_get_segment_text(m_whisperCtx, i);
        if (text) {
            if (!result.isEmpty()) result += QStringLiteral(" ");
            result += QString::fromUtf8(text).trimmed();
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
    cparams.use_gpu = m_settings->whisperGpu();

    m_whisperCtx = whisper_init_from_file_with_params(
        modelPath.toUtf8().constData(), cparams);

    if (!m_whisperCtx) {
        qWarning() << "[JARVIS] Failed to initialize whisper context from:" << modelPath;
    } else {
        qDebug() << "[JARVIS] Whisper model loaded successfully.";
    }
}

void JarvisAudio::reloadWhisperModel()
{
    if (m_whisperBusy.load()) {
        QTimer::singleShot(200, this, &JarvisAudio::reloadWhisperModel);
        return;
    }

    QMutexLocker lock(&m_whisperMutex);
    if (m_whisperCtx) {
        whisper_free(m_whisperCtx);
        m_whisperCtx = nullptr;
    }
    initWhisper();
    qDebug() << "[JARVIS] Whisper model reloaded:" << (m_whisperCtx ? "OK" : "FAILED");
}

QString JarvisAudio::findWhisperModel() const
{
    const QString preferred = m_settings->whisperModel();
    const QStringList dirs = {
        QDir::homePath() + QStringLiteral("/.local/share/jarvis/"),
        QStringLiteral("/usr/share/jarvis/"),
        QStringLiteral("/usr/local/share/jarvis/"),
        QDir::homePath() + QStringLiteral("/.local/share/whisper/"),
        QStringLiteral("/usr/share/whisper/"),
    };

    // Try exact match first (e.g. "small.en-q5_1" → "ggml-small.en-q5_1.bin")
    for (const auto &dir : dirs) {
        const QString exact = dir + QStringLiteral("ggml-%1.bin").arg(preferred);
        if (QFileInfo::exists(exact)) return exact;
    }

    // Fall back to base name variants then smaller models
    const QString baseName = preferred.section('.', 0, 0).section('-', 0, 0);
    const QStringList sizes = {baseName, QStringLiteral("tiny"), QStringLiteral("base"), QStringLiteral("small")};
    for (const auto &size : sizes) {
        for (const auto &dir : dirs) {
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
            if (m_settings->autoStartWakeWord() && m_whisperCtx && m_capture) {
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
    if (!m_whisperCtx || !m_capture) return;

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
            emit whisperHeard(QStringLiteral("cmd"), text.isEmpty() ? QStringLiteral("(empty)") : text);
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
        m_capture->stop(); // Fully release the PipeWire stream
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
