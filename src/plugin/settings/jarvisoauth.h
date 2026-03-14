#pragma once

#include <QObject>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTcpServer>
#include <QFile>
#include <QString>

class JarvisOAuth : public QObject
{
    Q_OBJECT

public:
    explicit JarvisOAuth(QNetworkAccessManager *nam, QObject *parent = nullptr);

    /// Returns a valid access token for the provider, reading from cached state
    /// or CLI credential files. Returns empty if expired and not yet refreshed.
    [[nodiscard]] QString accessToken(const QString &provider);

    /// Whether OAuth credentials exist (either cached or on disk) for this provider.
    [[nodiscard]] bool hasCredentials(const QString &provider) const;

    /// Whether a valid (non-expired) token is currently available.
    [[nodiscard]] bool hasValidToken(const QString &provider);

    /// Gemini Cloud Code project ID (obtained from loadCodeAssist).
    [[nodiscard]] QString geminiProjectId() const { return m_geminiProjectId; }

    /// Whether Gemini is using OAuth (Cloud Code) mode vs API key.
    [[nodiscard]] bool isGeminiOAuthMode() const { return !m_gemini.accessToken.isEmpty() || QFile::exists(geminiCredPath()); }

    /// Kick off an async token refresh if the cached token is expired.
    void ensureValidToken(const QString &provider);

    /// Start a browser-based OAuth login flow for the provider.
    void startLogin(const QString &provider);

    /// Complete the Claude login by submitting the pasted auth code.
    void completeClaudeLogin(const QString &pastedCode);

    /// Whether we're waiting for the user to paste a Claude auth code.
    [[nodiscard]] bool awaitingClaudeCode() const { return m_awaitingClaudeCode; }

    /// Cancel any in-progress login flow.
    void cancelLogin();

    /// Logout — clear cached tokens and delete credential file.
    void logout(const QString &provider);

signals:
    void tokenRefreshed(const QString &provider);
    void tokenError(const QString &provider, const QString &error);
    void loginStarted(const QString &provider);
    void loginFinished(const QString &provider, bool success);
    void loginStatusChanged();
    void geminiProjectReady();

private:
    struct OAuthToken {
        QString accessToken;
        QString refreshToken;
        qint64 expiresAtMs{0};
        bool refreshing{false};

        [[nodiscard]] bool isExpired() const {
            return QDateTime::currentMSecsSinceEpoch() >= (expiresAtMs - 60000);
        }
        [[nodiscard]] bool isValid() const {
            return !accessToken.isEmpty() && !isExpired();
        }
    };

    // Credential file I/O
    void loadClaudeCredentials();
    void loadGeminiCredentials();
    void saveClaudeCredentials();
    void saveGeminiCredentials();

    // Token refresh
    void refreshClaudeToken();
    void refreshGeminiToken();

    // Browser login flow
    void startGeminiLogin();
    void startClaudeLogin();
    void fetchGeminiProject();
    void startLocalServer(const std::function<void(const QString &code)> &onCode);
    void stopLocalServer();

    // PKCE helpers (for Claude)
    static QByteArray generateCodeVerifier();
    static QString computeS256Challenge(const QByteArray &verifier);

    [[nodiscard]] static QString claudeCredPath();
    [[nodiscard]] static QString geminiCredPath();

    QNetworkAccessManager *m_networkManager;
    OAuthToken m_claude;
    OAuthToken m_gemini;
    QString m_geminiProjectId;

    // Local redirect server for OAuth flows
    QTcpServer *m_redirectServer{nullptr};
    QString m_activeLoginProvider;
    QByteArray m_pkceVerifier; // For Claude PKCE flow
    QString m_claudeOAuthState; // state param for CSRF validation
    bool m_awaitingClaudeCode{false};

    // Gemini OAuth — Google's public "installed app" credentials from Gemini CLI
    static constexpr auto GEMINI_CLIENT_ID =
        "681255809395-oo8ft2oprdrnp9e3aqf6av3hmdib135j.apps.googleusercontent.com";
    static constexpr auto GEMINI_CLIENT_SECRET =
        "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";
    static constexpr auto GEMINI_AUTH_URL =
        "https://accounts.google.com/o/oauth2/v2/auth";
    static constexpr auto GOOGLE_TOKEN_URL =
        "https://oauth2.googleapis.com/token";
    static constexpr auto GEMINI_SCOPES =
        "https://www.googleapis.com/auth/cloud-platform "
        "https://www.googleapis.com/auth/userinfo.email "
        "https://www.googleapis.com/auth/userinfo.profile";

    // Claude OAuth
    static constexpr auto CLAUDE_CLIENT_ID =
        "9d1c250a-e61b-44d9-88ed-5944d1962f5e";
    static constexpr auto CLAUDE_AUTH_URL =
        "https://claude.ai/oauth/authorize";
    static constexpr auto CLAUDE_TOKEN_URL =
        "https://console.anthropic.com/v1/oauth/token";
    static constexpr auto CLAUDE_REDIRECT_URI =
        "https://console.anthropic.com/oauth/code/callback";
    static constexpr auto CLAUDE_SCOPES =
        "user:profile user:inference";

    // Gemini Cloud Code Assist (consumer OAuth endpoint)
    static constexpr auto GEMINI_CLOUDCODE_URL =
        "https://cloudcode-pa.googleapis.com/v1internal";
};
