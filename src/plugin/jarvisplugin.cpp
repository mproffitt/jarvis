#include "jarvisplugin.h"
#include "jarvisbackend.h"

void JarvisPlugin::registerTypes(const char *uri)
{
    Q_ASSERT(QLatin1String(uri) == QLatin1String("org.kde.plasma.jarvis"));
    qmlRegisterSingletonType<JarvisBackend>(uri, 1, 0, "JarvisBackend",
        [](QQmlEngine *engine, QJSEngine *scriptEngine) -> QObject * {
            Q_UNUSED(scriptEngine)
            static JarvisBackend *instance = nullptr;
            if (!instance) {
                instance = new JarvisBackend();
                engine->setObjectOwnership(instance, QQmlEngine::CppOwnership);
            }
            return instance;
        });
}
