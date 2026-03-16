#pragma once

#include <QObject>
#include <QByteArray>
#include <QMutex>
#include <atomic>
#include <thread>

struct pw_main_loop;
struct pw_context;
struct pw_core;
struct pw_stream;
struct spa_hook;

/// Native PipeWire audio playback stream for TTS output.
/// Runs its own PipeWire event loop on a dedicated thread.
/// Accepts 22050 Hz mono s16le PCM data via write().
class PwPlayback : public QObject
{
    Q_OBJECT

public:
    explicit PwPlayback(QObject *parent = nullptr);
    ~PwPlayback() override;

    void start();
    void stop();
    [[nodiscard]] bool isRunning() const { return m_running.load(); }

    /// Write PCM data to the playback stream. Thread-safe.
    void write(const QByteArray &pcm16);

    /// Clear all buffered audio immediately (for interruption).
    void flush();

    /// Signal the stream that no more data is coming for this batch.
    /// The stream stays alive but drains its buffer.
    void drain();

    // Called from PipeWire thread — do not call directly
    void onProcess();
    void onCoreError(uint32_t id, int res, const char *message);
    void onStreamStateChanged(int old, int state, const char *error);

signals:
    void drained();  // Emitted when buffer is fully played after drain()

private:
    bool setup();
    void teardown();
    void run();

    static constexpr int SAMPLE_RATE = 22050;

    std::thread m_thread;
    pw_main_loop *m_loop{nullptr};
    pw_context *m_context{nullptr};
    pw_core *m_core{nullptr};
    pw_stream *m_stream{nullptr};

    spa_hook *m_coreListener{nullptr};
    spa_hook *m_streamListener{nullptr};

    // Ring buffer for PCM data (written from Qt thread, read from PW thread)
    QByteArray m_buffer;
    QMutex m_bufferMutex;
    std::atomic<bool> m_draining{false};

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_shouldRun{false};
};
