#include "pwcapture.h"

#include <QDebug>
#include <cstring>
#include <chrono>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw.h>
#include <spa/utils/hook.h>
#include <spa/debug/types.h>

#include <cerrno>

// ─────────────────────────────────────────────
// PipeWire callbacks (C linkage)
// ─────────────────────────────────────────────

static void onStreamProcess(void *data)
{
    static_cast<PwCapture *>(data)->onProcess();
}

static void onStreamStateChanged(void *data, enum pw_stream_state old,
                                  enum pw_stream_state state, const char *error)
{
    static_cast<PwCapture *>(data)->onStreamStateChanged(
        static_cast<int>(old), static_cast<int>(state), error);
}

static void onCoreError(void *data, uint32_t id, int /*seq*/, int res, const char *message)
{
    static_cast<PwCapture *>(data)->onCoreError(id, res, message);
}

static const pw_stream_events streamEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .state_changed = ::onStreamStateChanged,
    .process = ::onStreamProcess,
};

static const pw_core_events coreEvents = {
    .version = PW_VERSION_CORE_EVENTS,
    .error = ::onCoreError,
};

// ─────────────────────────────────────────────
// PwCapture implementation
// ─────────────────────────────────────────────

PwCapture::PwCapture(QObject *parent)
    : QObject(parent)
{
}

PwCapture::~PwCapture()
{
    stop();
}

void PwCapture::start()
{
    if (m_shouldRun.load())
        return;

    m_shouldRun = true;
    m_thread = std::thread(&PwCapture::run, this);
}

void PwCapture::stop()
{
    if (!m_shouldRun.load())
        return;

    m_shouldRun = false;

    if (m_loop)
        pw_main_loop_quit(m_loop);

    if (m_thread.joinable())
        m_thread.join();
}

bool PwCapture::setup()
{
    m_loop = pw_main_loop_new(nullptr);
    if (!m_loop) {
        qWarning() << "[JARVIS] PwCapture: failed to create main loop";
        return false;
    }

    m_context = pw_context_new(pw_main_loop_get_loop(m_loop), nullptr, 0);
    if (!m_context) {
        qWarning() << "[JARVIS] PwCapture: failed to create context";
        teardown();
        return false;
    }

    m_core = pw_context_connect(m_context, nullptr, 0);
    if (!m_core) {
        qWarning() << "[JARVIS] PwCapture: failed to connect to PipeWire";
        teardown();
        return false;
    }

    // Listen for core errors (PipeWire daemon restart = -EPIPE)
    m_coreListener = new spa_hook{};
    spa_zero(*m_coreListener);
    pw_core_add_listener(m_core, m_coreListener, &coreEvents, this);

    // Create capture stream
    auto *props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE, "Communication",
        PW_KEY_MEDIA_CLASS, "Stream/Input/Audio",
        PW_KEY_NODE_NAME, "jarvis-capture",
        PW_KEY_APP_NAME, "Jarvis",
        PW_KEY_NODE_LOOP_CLASS, "main",
        "node.async", "true",
        nullptr);

    m_stream = pw_stream_new(m_core, "jarvis-audio-capture", props);
    if (!m_stream) {
        qWarning() << "[JARVIS] PwCapture: failed to create stream";
        teardown();
        return false;
    }

    m_streamListener = new spa_hook{};
    spa_zero(*m_streamListener);
    pw_stream_add_listener(m_stream, m_streamListener, &streamEvents, this);

    // Request 16 kHz mono s16le
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
        PW_DIRECTION_INPUT,
        PW_ID_ANY,
        static_cast<pw_stream_flags>(
            PW_STREAM_FLAG_AUTOCONNECT |
            PW_STREAM_FLAG_MAP_BUFFERS),
        params, 1);

    if (res < 0) {
        qWarning() << "[JARVIS] PwCapture: stream connect failed:" << strerror(-res);
        teardown();
        return false;
    }

    return true;
}

void PwCapture::teardown()
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

    if (m_running.exchange(false)) {
        QMetaObject::invokeMethod(this, [this]() {
            emit disconnected();
        }, Qt::QueuedConnection);
    }
}

void PwCapture::run()
{
    while (m_shouldRun.load()) {
        if (!setup()) {
            qWarning() << "[JARVIS] PwCapture: setup failed, retrying in 2s";
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        m_running = true;
        QMetaObject::invokeMethod(this, [this]() {
            emit connected();
        }, Qt::QueuedConnection);

        qDebug() << "[JARVIS] PwCapture: stream running";
        pw_main_loop_run(m_loop);

        // Loop exited — tear down and retry if we should still be running
        teardown();

        if (m_shouldRun.load()) {
            qDebug() << "[JARVIS] PwCapture: disconnected, reconnecting in 1s";
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

void PwCapture::onProcess()
{
    struct pw_buffer *pwBuf = pw_stream_dequeue_buffer(m_stream);
    if (!pwBuf)
        return;

    struct spa_buffer *buf = pwBuf->buffer;
    if (buf->datas[0].data && buf->datas[0].chunk->size > 0) {
        const auto *samples = static_cast<const int16_t *>(buf->datas[0].data);
        const uint32_t size = buf->datas[0].chunk->size;
        const int numSamples = static_cast<int>(size / sizeof(int16_t));

        QByteArray pcm(reinterpret_cast<const char *>(samples), static_cast<int>(size));

        // Compute audio level
        double sum = 0.0;
        for (int i = 0; i < numSamples; ++i)
            sum += qAbs(static_cast<double>(samples[i]));
        double level = numSamples > 0 ? sum / (numSamples * 32768.0) : 0.0;

        QMetaObject::invokeMethod(this, [this, pcm = std::move(pcm), level]() {
            emit audioData(pcm);
            emit audioLevel(level);
        }, Qt::QueuedConnection);
    }

    pw_stream_queue_buffer(m_stream, pwBuf);
}

void PwCapture::onCoreError(uint32_t id, int res, const char *message)
{
    qWarning() << "[JARVIS] PwCapture: core error" << id << res << message;

    if (id == PW_ID_CORE && res == -EPIPE) {
        // PipeWire daemon restarted
        pw_main_loop_quit(m_loop);
    }
}

void PwCapture::onStreamStateChanged(int old, int state, const char *error)
{
    qDebug() << "[JARVIS] PwCapture:"
             << pw_stream_state_as_string(static_cast<pw_stream_state>(old))
             << "->"
             << pw_stream_state_as_string(static_cast<pw_stream_state>(state));
    if (error)
        qWarning() << "[JARVIS] PwCapture: error:" << error;
}
