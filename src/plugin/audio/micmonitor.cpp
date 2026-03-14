#include "micmonitor.h"

#include <QDebug>
#include <cstring>

#include <pipewire/pipewire.h>
#include <spa/utils/hook.h>

// ─────────────────────────────────────────────
// PipeWire registry callbacks (C linkage)
// ─────────────────────────────────────────────

static void registryEventGlobal(void *data, uint32_t id,
                                 uint32_t /*permissions*/, const char *type,
                                 uint32_t /*version*/, const struct spa_dict *props)
{
    if (!props || !type) return;
    if (strcmp(type, PW_TYPE_INTERFACE_Node) != 0) return;

    const char *mediaClass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
    if (!mediaClass) return;

    // Only care about audio capture streams — exact match, not internal/bluetooth
    if (strcmp(mediaClass, "Stream/Input/Audio") != 0) return;

    const char *appName = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    if (!appName) appName = spa_dict_lookup(props, PW_KEY_APP_NAME);
    if (!appName) appName = "unknown";

    auto *monitor = static_cast<MicMonitor *>(data);
    monitor->onNodeAdded(id, mediaClass, appName);
}

static void registryEventGlobalRemove(void *data, uint32_t id)
{
    auto *monitor = static_cast<MicMonitor *>(data);
    monitor->onNodeRemoved(id);
}

static const struct pw_registry_events registryEvents = {
    .version = PW_VERSION_REGISTRY_EVENTS,
    .global = registryEventGlobal,
    .global_remove = registryEventGlobalRemove,
};

// ─────────────────────────────────────────────
// MicMonitor implementation
// ─────────────────────────────────────────────

MicMonitor::MicMonitor(QObject *parent)
    : QObject(parent)
{
    pw_init(nullptr, nullptr);
}

MicMonitor::~MicMonitor()
{
    stop();
    pw_deinit();
}

void MicMonitor::start()
{
    if (m_loop) return; // Already running

    // Create PipeWire objects on the main thread, then move the loop to a worker thread
    m_loop = pw_main_loop_new(nullptr);
    if (!m_loop) {
        qWarning() << "[JARVIS] MicMonitor: failed to create PipeWire main loop";
        return;
    }

    m_context = pw_context_new(pw_main_loop_get_loop(m_loop), nullptr, 0);
    if (!m_context) {
        qWarning() << "[JARVIS] MicMonitor: failed to create PipeWire context";
        pw_main_loop_destroy(m_loop);
        m_loop = nullptr;
        return;
    }

    m_core = pw_context_connect(m_context, nullptr, 0);
    if (!m_core) {
        qWarning() << "[JARVIS] MicMonitor: failed to connect to PipeWire";
        pw_context_destroy(m_context);
        pw_main_loop_destroy(m_loop);
        m_context = nullptr;
        m_loop = nullptr;
        return;
    }

    m_registry = pw_core_get_registry(m_core, PW_VERSION_REGISTRY, 0);
    if (!m_registry) {
        qWarning() << "[JARVIS] MicMonitor: failed to get PipeWire registry";
        pw_core_disconnect(m_core);
        pw_context_destroy(m_context);
        pw_main_loop_destroy(m_loop);
        m_core = nullptr;
        m_context = nullptr;
        m_loop = nullptr;
        return;
    }

    // Allocate and zero-initialize the spa_hook
    m_registryListener = new spa_hook{};
    spa_zero(*m_registryListener);
    pw_registry_add_listener(m_registry, m_registryListener, &registryEvents, this);

    qDebug() << "[JARVIS] MicMonitor: PipeWire registry listener started";

    // Run the PipeWire event loop on a dedicated thread
    m_thread = std::thread([this]() {
        pw_main_loop_run(m_loop);
    });
}

void MicMonitor::stop()
{
    if (!m_loop) return;

    // Signal the loop to quit
    pw_main_loop_quit(m_loop);
    if (m_thread.joinable()) m_thread.join();

    if (m_registryListener) {
        spa_hook_remove(m_registryListener);
        delete m_registryListener;
        m_registryListener = nullptr;
    }

    if (m_registry) {
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(m_registry));
        m_registry = nullptr;
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

    qDebug() << "[JARVIS] MicMonitor: stopped";
}

void MicMonitor::onNodeAdded(uint32_t id, const char * /*mediaClass*/, const char *appName)
{
    // Skip our own capture stream
    if (appName && strstr(appName, "plasmashell") != nullptr)
        return;

    qDebug() << "[JARVIS] MicMonitor: capture started by" << appName << "(id:" << id << ")";

    {
        QMutexLocker lock(&m_mutex);
        m_otherCaptureNodes.insert(id);
    }
    updateBusyState();
}

void MicMonitor::onNodeRemoved(uint32_t id)
{
    bool wasTracked = false;
    {
        QMutexLocker lock(&m_mutex);
        wasTracked = m_otherCaptureNodes.remove(id);
    }
    if (wasTracked) {
        qDebug() << "[JARVIS] MicMonitor: capture ended (id:" << id << ")";
        updateBusyState();
    }
}

void MicMonitor::updateBusyState()
{
    bool busy;
    {
        QMutexLocker lock(&m_mutex);
        busy = !m_otherCaptureNodes.isEmpty();
    }

    const bool wasBusy = m_micBusy.exchange(busy);
    if (busy != wasBusy) {
        // Emit on the Qt thread
        QMetaObject::invokeMethod(this, [this, busy]() {
            emit micBusyChanged(busy);
        }, Qt::QueuedConnection);
    }
}
