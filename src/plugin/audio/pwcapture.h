#pragma once

#include <QObject>
#include <QByteArray>
#include <atomic>
#include <thread>

struct pw_main_loop;
struct pw_context;
struct pw_core;
struct pw_stream;
struct spa_hook;

/// Native PipeWire audio capture stream.
/// Runs its own PipeWire event loop on a dedicated thread and automatically
/// reconnects after PipeWire daemon restarts or USB device plug/unplug.
/// Outputs 16 kHz mono s16le PCM data via the audioData signal.
class PwCapture : public QObject
{
    Q_OBJECT

public:
    explicit PwCapture(QObject *parent = nullptr);
    ~PwCapture() override;

    void start();
    void stop();
    [[nodiscard]] bool isRunning() const { return m_running.load(); }

    // Called from PipeWire thread — do not call directly
    void onProcess();
    void onCoreError(uint32_t id, int res, const char *message);
    void onStreamStateChanged(int old, int state, const char *error);

signals:
    void audioData(const QByteArray &pcm16);
    void audioLevel(double level);
    void connected();
    void disconnected();

private:
    bool setup();
    void teardown();
    void run();

    static constexpr int SAMPLE_RATE = 16000;

    std::thread m_thread;
    pw_main_loop *m_loop{nullptr};
    pw_context *m_context{nullptr};
    pw_core *m_core{nullptr};
    pw_stream *m_stream{nullptr};

    spa_hook *m_coreListener{nullptr};
    spa_hook *m_streamListener{nullptr};

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_shouldRun{false};
};
