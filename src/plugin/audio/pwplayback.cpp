#include "pwplayback.h"

#include <QDebug>
#include <cstring>
#include <chrono>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw.h>
#include <spa/utils/hook.h>

// ─────────────────────────────────────────────
// PipeWire callbacks (C linkage)
// ─────────────────────────────────────────────

static void onPlaybackProcess(void *data)
{
    static_cast<PwPlayback *>(data)->onProcess();
}

static void onPlaybackStateChanged(void *data, enum pw_stream_state old,
                                    enum pw_stream_state state, const char *error)
{
    static_cast<PwPlayback *>(data)->onStreamStateChanged(
        static_cast<int>(old), static_cast<int>(state), error);
}

static void onPlaybackCoreError(void *data, uint32_t id, int /*seq*/, int res, const char *message)
{
    static_cast<PwPlayback *>(data)->onCoreError(id, res, message);
}

static const pw_stream_events playbackStreamEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .state_changed = ::onPlaybackStateChanged,
    .process = ::onPlaybackProcess,
};

static const pw_core_events playbackCoreEvents = {
    .version = PW_VERSION_CORE_EVENTS,
    .error = ::onPlaybackCoreError,
};

// ─────────────────────────────────────────────
// PwPlayback implementation
// ─────────────────────────────────────────────

PwPlayback::PwPlayback(QObject *parent)
    : QObject(parent)
{
}

PwPlayback::~PwPlayback()
{
    stop();
}

void PwPlayback::start()
{
    if (m_shouldRun.load())
        return;

    m_shouldRun = true;
    m_thread = std::thread(&PwPlayback::run, this);
}

void PwPlayback::stop()
{
    if (!m_shouldRun.load())
        return;

    m_shouldRun = false;

    if (m_loop)
        pw_main_loop_quit(m_loop);

    if (m_thread.joinable())
        m_thread.join();

    QMutexLocker lock(&m_bufferMutex);
    m_buffer.clear();
}

void PwPlayback::write(const QByteArray &pcm16)
{
    if (pcm16.isEmpty()) return;

    QMutexLocker lock(&m_bufferMutex);
    m_buffer.append(pcm16);
    m_draining = false;
}

void PwPlayback::flush()
{
    QMutexLocker lock(&m_bufferMutex);
    m_buffer.clear();
    m_draining = false;
}

void PwPlayback::drain()
{
    m_draining = true;
}

bool PwPlayback::setup()
{
    m_loop = pw_main_loop_new(nullptr);
    if (!m_loop) {
        qWarning() << "[JARVIS] PwPlayback: failed to create main loop";
        return false;
    }

    m_context = pw_context_new(pw_main_loop_get_loop(m_loop), nullptr, 0);
    if (!m_context) {
        qWarning() << "[JARVIS] PwPlayback: failed to create context";
        teardown();
        return false;
    }

    m_core = pw_context_connect(m_context, nullptr, 0);
    if (!m_core) {
        qWarning() << "[JARVIS] PwPlayback: failed to connect to PipeWire";
        teardown();
        return false;
    }

    m_coreListener = new spa_hook{};
    spa_zero(*m_coreListener);
    pw_core_add_listener(m_core, m_coreListener, &playbackCoreEvents, this);

    auto *props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Playback",
        PW_KEY_MEDIA_ROLE, "Communication",
        PW_KEY_MEDIA_CLASS, "Stream/Output/Audio",
        PW_KEY_NODE_NAME, "jarvis-tts",
        PW_KEY_APP_NAME, "J.A.R.V.I.S.",
        PW_KEY_NODE_LOOP_CLASS, "main",
        "node.async", "true",
        nullptr);

    m_stream = pw_stream_new(m_core, "jarvis-tts-playback", props);
    if (!m_stream) {
        qWarning() << "[JARVIS] PwPlayback: failed to create stream";
        teardown();
        return false;
    }

    m_streamListener = new spa_hook{};
    spa_zero(*m_streamListener);
    pw_stream_add_listener(m_stream, m_streamListener, &playbackStreamEvents, this);

    // Request 22050 Hz mono s16le
    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

    struct spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_S16_LE;
    info.rate = SAMPLE_RATE;
    info.channels = 1;
    info.position[0] = SPA_AUDIO_CHANNEL_MONO;

    const struct spa_pod *params[1];
    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

    int res = pw_stream_connect(m_stream,
        PW_DIRECTION_OUTPUT,
        PW_ID_ANY,
        static_cast<pw_stream_flags>(
            PW_STREAM_FLAG_AUTOCONNECT |
            PW_STREAM_FLAG_MAP_BUFFERS),
        params, 1);

    if (res < 0) {
        qWarning() << "[JARVIS] PwPlayback: stream connect failed:" << strerror(-res);
        teardown();
        return false;
    }

    return true;
}

void PwPlayback::teardown()
{
    if (m_streamListener) {
        spa_hook_remove(m_streamListener);
        delete m_streamListener;
        m_streamListener = nullptr;
    }
    if (m_stream) {
        pw_stream_destroy(m_stream);
        m_stream = nullptr;
    }
    if (m_coreListener) {
        spa_hook_remove(m_coreListener);
        delete m_coreListener;
        m_coreListener = nullptr;
    }
    if (m_core) {
        pw_core_disconnect(m_core);
        m_core = nullptr;
    }
    if (m_context) {
        pw_context_destroy(m_context);
        m_context = nullptr;
    }
    if (m_loop) {
        pw_main_loop_destroy(m_loop);
        m_loop = nullptr;
    }
    m_running = false;
}

void PwPlayback::run()
{
    while (m_shouldRun.load()) {
        if (!setup()) {
            qWarning() << "[JARVIS] PwPlayback: setup failed, retrying in 2s";
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        m_running = true;
        qDebug() << "[JARVIS] PwPlayback: stream running";
        pw_main_loop_run(m_loop);

        teardown();

        if (m_shouldRun.load()) {
            qDebug() << "[JARVIS] PwPlayback: disconnected, reconnecting in 1s";
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

void PwPlayback::onProcess()
{
    struct pw_buffer *pwBuf = pw_stream_dequeue_buffer(m_stream);
    if (!pwBuf) return;

    struct spa_buffer *buf = pwBuf->buffer;
    auto *dst = static_cast<uint8_t *>(buf->datas[0].data);
    const uint32_t maxSize = buf->datas[0].maxsize;

    uint32_t written = 0;
    {
        QMutexLocker lock(&m_bufferMutex);
        if (!m_buffer.isEmpty()) {
            written = static_cast<uint32_t>(qMin(static_cast<int>(maxSize), m_buffer.size()));
            memcpy(dst, m_buffer.constData(), written);
            m_buffer.remove(0, static_cast<int>(written));
        }
    }

    if (written == 0) {
        // No data — write silence
        memset(dst, 0, maxSize);
        written = maxSize;

        // If draining and buffer is empty, signal completion
        if (m_draining.load()) {
            m_draining = false;
            QMetaObject::invokeMethod(this, [this]() {
                emit drained();
            }, Qt::QueuedConnection);
        }
    }

    buf->datas[0].chunk->offset = 0;
    buf->datas[0].chunk->stride = sizeof(int16_t);
    buf->datas[0].chunk->size = written;

    pw_stream_queue_buffer(m_stream, pwBuf);
}

void PwPlayback::onCoreError(uint32_t id, int res, const char *message)
{
    qWarning() << "[JARVIS] PwPlayback: core error" << id << res << message;
    if (id == PW_ID_CORE && res == -EPIPE)
        pw_main_loop_quit(m_loop);
}

void PwPlayback::onStreamStateChanged(int old, int state, const char *error)
{
    qDebug() << "[JARVIS] PwPlayback:"
             << pw_stream_state_as_string(static_cast<pw_stream_state>(old))
             << "->"
             << pw_stream_state_as_string(static_cast<pw_stream_state>(state));
    if (error)
        qWarning() << "[JARVIS] PwPlayback: error:" << error;
}
