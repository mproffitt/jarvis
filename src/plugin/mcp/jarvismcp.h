#pragma once

#include <QObject>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>

#include <functional>

/// MCP (Model Context Protocol) client that manages stdio-based MCP servers.
/// Loads server configs from ~/.local/share/jarvis/mcp-servers.json, spawns
/// QProcess instances, and speaks JSON-RPC 2.0 over newline-delimited JSON.
class JarvisMcp : public QObject
{
    Q_OBJECT

public:
    explicit JarvisMcp(QObject *parent = nullptr);
    ~JarvisMcp() override;

    /// Spawn all configured MCP server processes and initialize them.
    void startAllServers();

    /// Gracefully shut down all running server processes.
    void stopAllServers();

    /// Returns all discovered tools formatted for Claude's tool_use API.
    /// Each entry: {name, description, input_schema}
    [[nodiscard]] QJsonArray allToolsForClaude() const;

    /// Returns all discovered tools formatted for OpenAI's function calling API.
    /// Each entry: {type: "function", function: {name, description, parameters}}
    [[nodiscard]] QJsonArray allToolsForOpenAI() const;

    /// Check whether a tool with the given name exists across all servers.
    [[nodiscard]] bool hasTool(const QString &name) const;

    /// Execute a tool by name. The callback receives (content array, isError).
    using ToolCallback = std::function<void(QJsonArray content, bool isError)>;
    void callTool(const QString &name, const QJsonObject &arguments,
                  ToolCallback callback);

signals:
    /// Emitted when the set of available tools changes (after discovery or re-discovery).
    void toolsChanged();

    /// Emitted when a tool call completes — useful for fire-and-forget integration.
    void toolCallComplete(const QString &toolUseId, const QJsonArray &content, bool isError);

    /// Emitted when any server's running state changes.
    void serverStatusChanged();

private:
    struct McpTool {
        QString name;
        QString description;
        QJsonObject inputSchema;
        QString serverName;  // which server owns this tool
    };

    struct McpServer {
        QString name;
        QString command;
        QStringList args;
        QProcessEnvironment env;
        QProcess *process{nullptr};
        bool initialized{false};
        QByteArray readBuffer;
    };

    // Config
    void loadConfig();
    [[nodiscard]] QString configFilePath() const;

    // Server lifecycle
    void startServer(McpServer &server);
    void initializeServer(McpServer &server);
    void discoverTools(McpServer &server);

    // JSON-RPC transport
    int sendRequest(McpServer &server, const QString &method,
                    const QJsonObject &params = {});
    void sendNotification(McpServer &server, const QString &method,
                          const QJsonObject &params = {});
    void processIncomingData(McpServer &server);
    void handleMessage(McpServer &server, const QJsonObject &msg);

    // Pending request tracking
    struct PendingRequest {
        QString method;
        QString serverName;
        ToolCallback callback;  // only for tool calls
        QTimer *timeoutTimer{nullptr};
    };

    int m_nextRequestId{1};
    QHash<int, PendingRequest> m_pendingRequests;

    // Servers and tools
    QHash<QString, McpServer> m_servers;
    QHash<QString, McpTool> m_tools;        // tool name -> tool
    QHash<QString, QString> m_toolToServer; // tool name -> server name
};
