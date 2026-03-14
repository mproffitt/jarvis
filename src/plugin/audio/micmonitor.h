#pragma once

#include <QObject>
#include <QSet>
#include <QMutex>
#include <atomic>
#include <thread>

struct pw_main_loop;
struct pw_context;
struct pw_core;
struct pw_registry;
struct spa_hook;

/// Monitors PipeWire registry for other apps capturing from the microphone.
/// Runs PipeWire's event loop on a dedicated thread and emits signals on the Qt thread.
class MicMonitor : public QObject
{
    Q_OBJECT

public:
    explicit MicMonitor(QObject *parent = nullptr);
    ~MicMonitor() override;

    void start();
    void stop();

    [[nodiscard]] bool isMicBusy() const { return m_micBusy.load(); }

    // Called from PipeWire thread — do not call directly
    void onNodeAdded(uint32_t id, const char *mediaClass, const char *appName);
    void onNodeRemoved(uint32_t id);

signals:
    void micBusyChanged(bool busy);

private:
    void updateBusyState();

    std::thread m_thread;
    pw_main_loop *m_loop{nullptr};
    pw_context *m_context{nullptr};
    pw_core *m_core{nullptr};
    pw_registry *m_registry{nullptr};

    // spa_hooks need stable addresses — allocated on heap
    struct spa_hook *m_registryListener{nullptr};

    // Track capture nodes by other apps (id -> app name)
    QMutex m_mutex;
    QSet<uint32_t> m_otherCaptureNodes;
    std::atomic<bool> m_micBusy{false};
};
