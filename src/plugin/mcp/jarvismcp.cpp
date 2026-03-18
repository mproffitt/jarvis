#include "mcp/jarvismcp.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <QDebug>

static constexpr auto PROTOCOL_VERSION = "2025-03-26";
static constexpr auto CLIENT_NAME = "jarvis-plasmoid";
static constexpr auto CLIENT_VERSION = "0.1.1";
static constexpr int TOOL_CALL_TIMEOUT_MS = 30000;

JarvisMcp::JarvisMcp(QObject *parent)
    : QObject(parent)
{
    loadConfig();

    // Watch config file for changes
    m_configWatcher = new QFileSystemWatcher(this);
    const QString path = configFilePath();
    if (QFile::exists(path))
        m_configWatcher->addPath(path);
    // Also watch the directory so we detect file creation
    m_configWatcher->addPath(QFileInfo(path).absolutePath());

    connect(m_configWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &) {
        qDebug() << "[MCP] Config file changed, reloading...";
        // Re-add the file path (Qt removes it after a change signal)
        const QString path = configFilePath();
        if (QFile::exists(path) && !m_configWatcher->files().contains(path))
            m_configWatcher->addPath(path);
        QTimer::singleShot(500, this, &JarvisMcp::reloadConfig); // debounce
    });
    connect(m_configWatcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &) {
        const QString path = configFilePath();
        if (QFile::exists(path) && !m_configWatcher->files().contains(path)) {
            m_configWatcher->addPath(path);
            qDebug() << "[MCP] Config file created, loading...";
            QTimer::singleShot(500, this, &JarvisMcp::reloadConfig);
        }
    });
}

JarvisMcp::~JarvisMcp()
{
    stopAllServers();
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

QString JarvisMcp::configFilePath() const
{
    const auto dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return dataDir + QStringLiteral("/jarvis/mcp-servers.json");
}

void JarvisMcp::loadConfig()
{
    QFile file(configFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "[MCP] No config file at" << configFilePath();
        return;
    }

    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        qWarning() << "[MCP] Invalid config JSON";
        return;
    }

    const auto root = doc.object();
    const auto servers = root[QStringLiteral("mcpServers")].toObject();

    for (auto it = servers.begin(); it != servers.end(); ++it) {
        const auto name = it.key();
        const auto cfg = it.value().toObject();

        McpServer server;
        server.name = name;
        server.command = cfg[QStringLiteral("command")].toString();

        const auto argsArr = cfg[QStringLiteral("args")].toArray();
        for (const auto &a : argsArr)
            server.args.append(a.toString());

        server.env = QProcessEnvironment::systemEnvironment();
        const auto envObj = cfg[QStringLiteral("env")].toObject();
        for (auto eit = envObj.begin(); eit != envObj.end(); ++eit)
            server.env.insert(eit.key(), eit.value().toString());

        m_servers.insert(name, std::move(server));
    }

    qWarning() << "[MCP] Loaded" << m_servers.size() << "server config(s)";
}

void JarvisMcp::reloadConfig()
{
    // Parse the new config
    QFile file(configFilePath());
    if (!file.open(QIODevice::ReadOnly)) return;

    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return;

    const auto root = doc.object();
    const auto servers = root[QStringLiteral("mcpServers")].toObject();

    // Find servers to stop (removed from config)
    QStringList toStop;
    for (auto it = m_servers.constBegin(); it != m_servers.constEnd(); ++it) {
        if (!servers.contains(it.key()))
            toStop.append(it.key());
    }
    for (const auto &name : toStop)
        stopServer(name);

    // Find servers to start (new in config)
    for (auto it = servers.begin(); it != servers.end(); ++it) {
        const auto name = it.key();
        if (m_servers.contains(name)) continue; // already running

        const auto cfg = it.value().toObject();
        McpServer server;
        server.name = name;
        server.command = cfg[QStringLiteral("command")].toString();
        for (const auto &a : cfg[QStringLiteral("args")].toArray())
            server.args.append(a.toString());
        server.env = QProcessEnvironment::systemEnvironment();
        for (auto eit = cfg[QStringLiteral("env")].toObject().begin();
             eit != cfg[QStringLiteral("env")].toObject().end(); ++eit)
            server.env.insert(eit.key(), eit.value().toString());

        m_servers.insert(name, std::move(server));
        startServer(m_servers[name]);
        qDebug() << "[MCP] Started new server:" << name;
    }
}

void JarvisMcp::stopServer(const QString &name)
{
    auto it = m_servers.find(name);
    if (it == m_servers.end()) return;

    auto &server = it.value();
    if (server.process) {
        server.process->closeWriteChannel();
        if (!server.process->waitForFinished(3000)) {
            server.process->terminate();
            if (!server.process->waitForFinished(2000))
                server.process->kill();
        }
        delete server.process;
        server.process = nullptr;
    }

    // Remove tools belonging to this server
    QStringList toRemove;
    for (auto tit = m_toolToServer.begin(); tit != m_toolToServer.end(); ++tit) {
        if (tit.value() == name)
            toRemove.append(tit.key());
    }
    for (const auto &toolName : toRemove) {
        m_tools.remove(toolName);
        m_toolToServer.remove(toolName);
    }

    m_servers.erase(it);

    if (!toRemove.isEmpty())
        Q_EMIT toolsChanged();
    Q_EMIT serverStatusChanged();
    qDebug() << "[MCP] Stopped server:" << name;
}

// ---------------------------------------------------------------------------
// Server management
// ---------------------------------------------------------------------------

void JarvisMcp::startAllServers()
{
    for (auto it = m_servers.begin(); it != m_servers.end(); ++it)
        startServer(it.value());
}

void JarvisMcp::stopAllServers()
{
    // Cancel all pending requests
    for (auto it = m_pendingRequests.begin(); it != m_pendingRequests.end(); ++it) {
        if (it->timeoutTimer) {
            it->timeoutTimer->stop();
            delete it->timeoutTimer;
        }
    }
    m_pendingRequests.clear();

    for (auto it = m_servers.begin(); it != m_servers.end(); ++it) {
        auto &server = it.value();
        if (!server.process)
            continue;

        server.process->closeWriteChannel();
        if (!server.process->waitForFinished(3000)) {
            server.process->terminate();
            if (!server.process->waitForFinished(2000))
                server.process->kill();
        }
        delete server.process;
        server.process = nullptr;
        server.initialized = false;
        server.readBuffer.clear();
    }

    m_tools.clear();
    m_toolToServer.clear();
    Q_EMIT toolsChanged();
    Q_EMIT serverStatusChanged();
}

void JarvisMcp::startServer(McpServer &server)
{
    if (server.process) {
        qDebug() << "[MCP] Server" << server.name << "already running";
        return;
    }

    auto *proc = new QProcess(this);
    proc->setProcessEnvironment(server.env);
    proc->setProcessChannelMode(QProcess::SeparateChannels);
    server.process = proc;

    // Handle stdout data
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, name = server.name]() {
        auto it = m_servers.find(name);
        if (it == m_servers.end()) return;
        processIncomingData(it.value());
    });

    // Handle stderr — log it
    connect(proc, &QProcess::readyReadStandardError, this, [name = server.name, proc]() {
        const auto err = proc->readAllStandardError();
        qDebug() << "[MCP]" << name << "stderr:" << err.trimmed();
    });

    // Handle crash / exit
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, name = server.name](int exitCode, QProcess::ExitStatus status) {
        qWarning() << "[MCP] Server" << name << "exited with code" << exitCode
                    << (status == QProcess::CrashExit ? "(crashed)" : "");

        auto it = m_servers.find(name);
        if (it != m_servers.end()) {
            auto &srv = it.value();
            if (srv.process) {
                srv.process->deleteLater();
                srv.process = nullptr;
            }
            srv.initialized = false;
            srv.readBuffer.clear();
        }

        // Remove tools belonging to this server
        QStringList toRemove;
        for (auto tit = m_toolToServer.begin(); tit != m_toolToServer.end(); ++tit) {
            if (tit.value() == name)
                toRemove.append(tit.key());
        }
        for (const auto &toolName : toRemove) {
            m_tools.remove(toolName);
            m_toolToServer.remove(toolName);
        }

        if (!toRemove.isEmpty())
            Q_EMIT toolsChanged();
        Q_EMIT serverStatusChanged();
    });

    qWarning() << "[MCP] Starting server" << server.name << ":" << server.command << server.args;
    proc->start(server.command, server.args);

    if (!proc->waitForStarted(5000)) {
        qWarning() << "[MCP] Failed to start server" << server.name << proc->errorString();
        delete proc;
        server.process = nullptr;
        return;
    }

    Q_EMIT serverStatusChanged();
    initializeServer(server);
}

// ---------------------------------------------------------------------------
// Protocol lifecycle
// ---------------------------------------------------------------------------

void JarvisMcp::initializeServer(McpServer &server)
{
    QJsonObject clientInfo;
    clientInfo[QStringLiteral("name")] = QLatin1String(CLIENT_NAME);
    clientInfo[QStringLiteral("version")] = QLatin1String(CLIENT_VERSION);

    QJsonObject capabilities;  // empty — we don't advertise any client capabilities yet

    QJsonObject params;
    params[QStringLiteral("protocolVersion")] = QLatin1String(PROTOCOL_VERSION);
    params[QStringLiteral("clientInfo")] = clientInfo;
    params[QStringLiteral("capabilities")] = capabilities;

    const int id = sendRequest(server, QStringLiteral("initialize"), params);

    auto &pending = m_pendingRequests[id];
    pending.method = QStringLiteral("initialize");
    pending.serverName = server.name;
}

void JarvisMcp::discoverTools(McpServer &server)
{
    const int id = sendRequest(server, QStringLiteral("tools/list"));

    auto &pending = m_pendingRequests[id];
    pending.method = QStringLiteral("tools/list");
    pending.serverName = server.name;
}

// ---------------------------------------------------------------------------
// JSON-RPC transport
// ---------------------------------------------------------------------------

int JarvisMcp::sendRequest(McpServer &server, const QString &method, const QJsonObject &params)
{
    const int id = m_nextRequestId++;

    QJsonObject msg;
    msg[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    msg[QStringLiteral("id")] = id;
    msg[QStringLiteral("method")] = method;
    if (!params.isEmpty())
        msg[QStringLiteral("params")] = params;

    const auto data = QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n';
    server.process->write(data);
    return id;
}

void JarvisMcp::sendNotification(McpServer &server, const QString &method, const QJsonObject &params)
{
    QJsonObject msg;
    msg[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    msg[QStringLiteral("method")] = method;
    if (!params.isEmpty())
        msg[QStringLiteral("params")] = params;

    const auto data = QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n';
    server.process->write(data);
}

void JarvisMcp::processIncomingData(McpServer &server)
{
    server.readBuffer.append(server.process->readAllStandardOutput());

    while (true) {
        const int nlPos = server.readBuffer.indexOf('\n');
        if (nlPos < 0)
            break;

        const auto line = server.readBuffer.left(nlPos).trimmed();
        server.readBuffer.remove(0, nlPos + 1);

        if (line.isEmpty())
            continue;

        QJsonParseError err;
        const auto doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError) {
            qWarning() << "[MCP]" << server.name << "invalid JSON:" << err.errorString()
                        << "line:" << line.left(200);
            continue;
        }

        if (!doc.isObject()) continue;
        handleMessage(server, doc.object());
    }
}

void JarvisMcp::handleMessage(McpServer &server, const QJsonObject &msg)
{
    // Check if this is a notification (no "id" field)
    if (!msg.contains(QStringLiteral("id"))) {
        const auto method = msg[QStringLiteral("method")].toString();
        if (method == QStringLiteral("notifications/tools/list_changed")) {
            qDebug() << "[MCP]" << server.name << "tools changed, re-discovering";
            discoverTools(server);
        }
        return;
    }

    // It's a response to one of our requests
    const int id = msg[QStringLiteral("id")].toInt();
    auto pendingIt = m_pendingRequests.find(id);
    if (pendingIt == m_pendingRequests.end()) {
        qWarning() << "[MCP] Received response for unknown request id" << id;
        return;
    }

    auto pending = *pendingIt;
    if (pending.timeoutTimer) {
        pending.timeoutTimer->stop();
        delete pending.timeoutTimer;
    }
    m_pendingRequests.erase(pendingIt);

    // Check for JSON-RPC error
    if (msg.contains(QStringLiteral("error"))) {
        const auto error = msg[QStringLiteral("error")].toObject();
        const auto errorMsg = error[QStringLiteral("message")].toString();
        const int errorCode = error[QStringLiteral("code")].toInt();
        qWarning() << "[MCP]" << server.name << "RPC error for" << pending.method
                    << "code:" << errorCode << "msg:" << errorMsg;

        if (pending.callback) {
            QJsonArray content;
            QJsonObject textContent;
            textContent[QStringLiteral("type")] = QStringLiteral("text");
            textContent[QStringLiteral("text")] = QStringLiteral("MCP error: ") + errorMsg;
            content.append(textContent);
            pending.callback(content, true);
        }
        return;
    }

    const auto result = msg[QStringLiteral("result")].toObject();

    // Handle response based on which method it was
    if (pending.method == QStringLiteral("initialize")) {
        qWarning() << "[MCP]" << server.name << "initialized, server info:"
                 << result[QStringLiteral("serverInfo")].toObject();

        server.initialized = true;

        // Send initialized notification
        sendNotification(server, QStringLiteral("notifications/initialized"));

        // Now discover tools
        discoverTools(server);

    } else if (pending.method == QStringLiteral("tools/list")) {
        const auto toolsArr = result[QStringLiteral("tools")].toArray();

        // Remove old tools from this server
        QStringList toRemove;
        for (auto tit = m_toolToServer.begin(); tit != m_toolToServer.end(); ++tit) {
            if (tit.value() == server.name)
                toRemove.append(tit.key());
        }
        for (const auto &toolName : toRemove) {
            m_tools.remove(toolName);
            m_toolToServer.remove(toolName);
        }

        // Add new tools — skip introspection tools that are redundant when
        // tool definitions are already sent natively via the tools API.
        // These cause models to introspect instead of acting.
        static const QStringList skipTools = {
            QStringLiteral("list_tools"), QStringLiteral("describe_tool"),
            QStringLiteral("filter_tools"),
            QStringLiteral("list_prompts"), QStringLiteral("describe_prompt"),
            QStringLiteral("get_prompt"),
        };
        for (const auto &toolVal : toolsArr) {
            const auto toolObj = toolVal.toObject();
            const QString name = toolObj[QStringLiteral("name")].toString();
            if (skipTools.contains(name))
                continue;

            McpTool tool;
            tool.name = name;
            tool.description = toolObj[QStringLiteral("description")].toString();
            tool.inputSchema = toolObj[QStringLiteral("inputSchema")].toObject();
            tool.serverName = server.name;

            m_tools.insert(tool.name, tool);
            m_toolToServer.insert(tool.name, server.name);
        }

        qWarning() << "[MCP]" << server.name << "discovered" << toolsArr.size()
                   << "tools, total:" << m_tools.size();
        Q_EMIT toolsChanged();

    } else if (pending.method == QStringLiteral("tools/call")) {
        const auto content = result[QStringLiteral("content")].toArray();
        const bool isError = result[QStringLiteral("isError")].toBool(false);

        if (pending.callback)
            pending.callback(content, isError);
    }
}

// ---------------------------------------------------------------------------
// Tool format conversion
// ---------------------------------------------------------------------------

QJsonArray JarvisMcp::allToolsForClaude() const
{
    QJsonArray arr;
    for (auto it = m_tools.constBegin(); it != m_tools.constEnd(); ++it) {
        const auto &tool = it.value();
        QJsonObject obj;
        obj[QStringLiteral("name")] = tool.name;
        obj[QStringLiteral("description")] = tool.description;
        obj[QStringLiteral("input_schema")] = tool.inputSchema;
        arr.append(obj);
    }
    return arr;
}

QJsonArray JarvisMcp::allToolsForOpenAI() const
{
    QJsonArray arr;
    for (auto it = m_tools.constBegin(); it != m_tools.constEnd(); ++it) {
        const auto &tool = it.value();
        QJsonObject fn;
        fn[QStringLiteral("name")] = tool.name;
        fn[QStringLiteral("description")] = tool.description;
        fn[QStringLiteral("parameters")] = tool.inputSchema;

        QJsonObject obj;
        obj[QStringLiteral("type")] = QStringLiteral("function");
        obj[QStringLiteral("function")] = fn;
        arr.append(obj);
    }
    return arr;
}

// ---------------------------------------------------------------------------
// Tool execution
// ---------------------------------------------------------------------------

bool JarvisMcp::hasTool(const QString &name) const
{
    return m_tools.contains(name);
}

void JarvisMcp::callTool(const QString &name, const QJsonObject &arguments, ToolCallback callback)
{
    auto serverIt = m_toolToServer.find(name);
    if (serverIt == m_toolToServer.end()) {
        qWarning() << "[MCP] Tool not found:" << name;
        if (callback) {
            QJsonArray content;
            QJsonObject textContent;
            textContent[QStringLiteral("type")] = QStringLiteral("text");
            textContent[QStringLiteral("text")] = QStringLiteral("Tool not found: ") + name;
            content.append(textContent);
            callback(content, true);
        }
        return;
    }

    auto srvIt = m_servers.find(serverIt.value());
    if (srvIt == m_servers.end() || !srvIt->process || !srvIt->initialized) {
        qWarning() << "[MCP] Server not available for tool:" << name;
        if (callback) {
            QJsonArray content;
            QJsonObject textContent;
            textContent[QStringLiteral("type")] = QStringLiteral("text");
            textContent[QStringLiteral("text")] = QStringLiteral("MCP server not available");
            content.append(textContent);
            callback(content, true);
        }
        return;
    }

    auto &server = srvIt.value();

    QJsonObject params;
    params[QStringLiteral("name")] = name;
    params[QStringLiteral("arguments")] = arguments;

    const int id = sendRequest(server, QStringLiteral("tools/call"), params);

    auto &pending = m_pendingRequests[id];
    pending.method = QStringLiteral("tools/call");
    pending.serverName = server.name;
    pending.callback = std::move(callback);

    // Set up timeout
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    pending.timeoutTimer = timer;

    connect(timer, &QTimer::timeout, this, [this, id]() {
        auto it = m_pendingRequests.find(id);
        if (it == m_pendingRequests.end()) return;

        qWarning() << "[MCP] Tool call timed out, request id:" << id;

        auto cb = std::move(it->callback);
        if (it->timeoutTimer)
            delete it->timeoutTimer;
        m_pendingRequests.erase(it);

        if (cb) {
            QJsonArray content;
            QJsonObject textContent;
            textContent[QStringLiteral("type")] = QStringLiteral("text");
            textContent[QStringLiteral("text")] = QStringLiteral("Tool call timed out after 30 seconds");
            content.append(textContent);
            cb(content, true);
        }
    });

    timer->start(TOOL_CALL_TIMEOUT_MS);
}
