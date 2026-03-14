#include "jarvisoauth.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QDebug>

JarvisOAuth::JarvisOAuth(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent)
    , m_networkManager(nam)
{
    loadClaudeCredentials();
    loadGeminiCredentials();

    // If we have Gemini OAuth creds, fetch the project ID
    if (!m_gemini.accessToken.isEmpty()) {
        fetchGeminiProject();
    }
}

// ─────────────────────────────────────────────
// Credential file paths
// ─────────────────────────────────────────────

QString JarvisOAuth::claudeCredPath()
{
    return QDir::homePath() + QStringLiteral("/.claude/.credentials.json");
}

QString JarvisOAuth::geminiCredPath()
{
    return QDir::homePath() + QStringLiteral("/.gemini/oauth_creds.json");
}

// ─────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────

bool JarvisOAuth::hasCredentials(const QString &provider) const
{
    if (provider == QStringLiteral("claude"))
        return !m_claude.accessToken.isEmpty() || QFile::exists(claudeCredPath());
    if (provider == QStringLiteral("gemini"))
        return !m_gemini.accessToken.isEmpty() || QFile::exists(geminiCredPath());
    return false;
}

bool JarvisOAuth::hasValidToken(const QString &provider)
{
    if (provider == QStringLiteral("claude")) {
        if (m_claude.accessToken.isEmpty()) loadClaudeCredentials();
        return m_claude.isValid();
    }
    if (provider == QStringLiteral("gemini")) {
        if (m_gemini.accessToken.isEmpty()) loadGeminiCredentials();
        return m_gemini.isValid();
    }
    return false;
}

QString JarvisOAuth::accessToken(const QString &provider)
{
    OAuthToken *tok = nullptr;

    if (provider == QStringLiteral("claude")) {
        tok = &m_claude;
        if (tok->accessToken.isEmpty()) loadClaudeCredentials();
    } else if (provider == QStringLiteral("gemini")) {
        tok = &m_gemini;
        if (tok->accessToken.isEmpty()) loadGeminiCredentials();
    } else {
        return {};
    }

    if (tok->isValid())
        return tok->accessToken;

    // Token expired — kick off a refresh but don't return the stale token
    if (!tok->refreshing && !tok->refreshToken.isEmpty()) {
        qDebug() << "[JARVIS] OAuth token expired for" << provider << "- starting refresh";
        ensureValidToken(provider);
    }

    return {};
}

void JarvisOAuth::ensureValidToken(const QString &provider)
{
    if (provider == QStringLiteral("claude")) {
        if (m_claude.refreshToken.isEmpty()) loadClaudeCredentials();
        if (!m_claude.isExpired()) return;
        refreshClaudeToken();
    } else if (provider == QStringLiteral("gemini")) {
        if (m_gemini.refreshToken.isEmpty()) loadGeminiCredentials();
        if (!m_gemini.isExpired()) return;
        refreshGeminiToken();
    }
}

void JarvisOAuth::logout(const QString &provider)
{
    if (provider == QStringLiteral("claude")) {
        m_claude = {};
        // Don't delete the CLI's credential file — just clear our cached state
    } else if (provider == QStringLiteral("gemini")) {
        m_gemini = {};
    }
    emit loginStatusChanged();
}

// ─────────────────────────────────────────────
// Load credentials from CLI config files
// ─────────────────────────────────────────────

void JarvisOAuth::loadClaudeCredentials()
{
    QFile file(claudeCredPath());
    if (!file.open(QIODevice::ReadOnly)) return;

    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isNull()) return;

    const auto oauth = doc.object()[QStringLiteral("claudeAiOauth")].toObject();
    if (oauth.isEmpty()) return;

    m_claude.accessToken = oauth[QStringLiteral("accessToken")].toString();
    m_claude.refreshToken = oauth[QStringLiteral("refreshToken")].toString();
    m_claude.expiresAtMs = static_cast<qint64>(oauth[QStringLiteral("expiresAt")].toDouble());

    if (m_claude.isValid()) {
        qDebug() << "[JARVIS] Claude OAuth token loaded, expires at"
                 << QDateTime::fromMSecsSinceEpoch(m_claude.expiresAtMs).toString();
    } else if (!m_claude.refreshToken.isEmpty()) {
        qDebug() << "[JARVIS] Claude OAuth token expired, will refresh";
    }
}

void JarvisOAuth::loadGeminiCredentials()
{
    QFile file(geminiCredPath());
    if (!file.open(QIODevice::ReadOnly)) return;

    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isNull()) return;

    const auto obj = doc.object();
    m_gemini.accessToken = obj[QStringLiteral("access_token")].toString();
    m_gemini.refreshToken = obj[QStringLiteral("refresh_token")].toString();
    m_gemini.expiresAtMs = static_cast<qint64>(obj[QStringLiteral("expiry_date")].toDouble());

    if (m_gemini.isValid()) {
        qDebug() << "[JARVIS] Gemini OAuth token loaded, expires at"
                 << QDateTime::fromMSecsSinceEpoch(m_gemini.expiresAtMs).toString();
    } else if (!m_gemini.refreshToken.isEmpty()) {
        qDebug() << "[JARVIS] Gemini OAuth token expired, will refresh";
    }
}

// ─────────────────────────────────────────────
// Token refresh
// ─────────────────────────────────────────────

void JarvisOAuth::refreshGeminiToken()
{
    if (m_gemini.refreshing || m_gemini.refreshToken.isEmpty()) return;
    m_gemini.refreshing = true;

    qDebug() << "[JARVIS] Refreshing Gemini OAuth token...";

    QNetworkRequest request(QUrl(QString::fromLatin1(GOOGLE_TOKEN_URL)));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));

    QUrlQuery params;
    params.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    params.addQueryItem(QStringLiteral("refresh_token"), m_gemini.refreshToken);
    params.addQueryItem(QStringLiteral("client_id"), QString::fromLatin1(GEMINI_CLIENT_ID));
    params.addQueryItem(QStringLiteral("client_secret"), QString::fromLatin1(GEMINI_CLIENT_SECRET));

    auto *reply = m_networkManager->post(request, params.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_gemini.refreshing = false;

        const QByteArray body = reply->readAll();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[JARVIS] Gemini token refresh failed:" << httpStatus << reply->errorString();
            qWarning() << "[JARVIS] Response:" << body;
            emit tokenError(QStringLiteral("gemini"), reply->errorString());
            return;
        }

        const auto doc = QJsonDocument::fromJson(body);
        const auto obj = doc.object();

        m_gemini.accessToken = obj[QStringLiteral("access_token")].toString();
        if (obj.contains(QStringLiteral("refresh_token")))
            m_gemini.refreshToken = obj[QStringLiteral("refresh_token")].toString();
        const int expiresIn = obj[QStringLiteral("expires_in")].toInt();
        m_gemini.expiresAtMs = QDateTime::currentMSecsSinceEpoch() + (expiresIn * 1000LL);

        qDebug() << "[JARVIS] Gemini token refreshed, valid for" << expiresIn << "s";

        saveGeminiCredentials();
        if (m_geminiProjectId.isEmpty()) fetchGeminiProject();
        emit tokenRefreshed(QStringLiteral("gemini"));
        emit loginStatusChanged();
    });
}

void JarvisOAuth::refreshClaudeToken()
{
    if (m_claude.refreshing || m_claude.refreshToken.isEmpty()) return;
    m_claude.refreshing = true;

    qDebug() << "[JARVIS] Refreshing Claude OAuth token...";

    QNetworkRequest request(QUrl(QString::fromLatin1(CLAUDE_TOKEN_URL)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("anthropic-beta", "oauth-2025-04-20");

    QJsonObject body;
    body[QStringLiteral("grant_type")] = QStringLiteral("refresh_token");
    body[QStringLiteral("refresh_token")] = m_claude.refreshToken;
    body[QStringLiteral("client_id")] = QString::fromLatin1(CLAUDE_CLIENT_ID);

    auto *reply = m_networkManager->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_claude.refreshing = false;

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[JARVIS] Claude token refresh failed:" << reply->errorString();
            emit tokenError(QStringLiteral("claude"), reply->errorString());
            return;
        }

        const auto doc = QJsonDocument::fromJson(reply->readAll());
        const auto obj = doc.object();

        const QString newAccess = obj[QStringLiteral("access_token")].toString();
        if (newAccess.isEmpty()) {
            qWarning() << "[JARVIS] Claude token refresh: no access_token in response";
            emit tokenError(QStringLiteral("claude"), QStringLiteral("No access_token in response"));
            return;
        }

        m_claude.accessToken = newAccess;
        if (obj.contains(QStringLiteral("refresh_token")))
            m_claude.refreshToken = obj[QStringLiteral("refresh_token")].toString();
        const int expiresIn = obj[QStringLiteral("expires_in")].toInt(3600);
        m_claude.expiresAtMs = QDateTime::currentMSecsSinceEpoch() + (expiresIn * 1000LL);

        qDebug() << "[JARVIS] Claude token refreshed, valid for" << expiresIn << "s";

        saveClaudeCredentials();
        emit tokenRefreshed(QStringLiteral("claude"));
        emit loginStatusChanged();
    });
}

// ─────────────────────────────────────────────
// Save refreshed tokens back to CLI config files
// ─────────────────────────────────────────────

void JarvisOAuth::saveClaudeCredentials()
{
    const QString path = claudeCredPath();
    QDir().mkpath(QFileInfo(path).path());

    // Read existing file to preserve other fields
    QJsonObject root;
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const auto doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isNull()) root = doc.object();
        file.close();
    }

    QJsonObject oauth = root[QStringLiteral("claudeAiOauth")].toObject();
    oauth[QStringLiteral("accessToken")] = m_claude.accessToken;
    oauth[QStringLiteral("refreshToken")] = m_claude.refreshToken;
    oauth[QStringLiteral("expiresAt")] = static_cast<double>(m_claude.expiresAtMs);
    root[QStringLiteral("claudeAiOauth")] = oauth;

    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson());
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    }
}

void JarvisOAuth::saveGeminiCredentials()
{
    const QString path = geminiCredPath();
    QDir().mkpath(QFileInfo(path).path());

    // Read existing file to preserve other fields
    QJsonObject obj;
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const auto doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isNull()) obj = doc.object();
        file.close();
    }

    obj[QStringLiteral("access_token")] = m_gemini.accessToken;
    if (!m_gemini.refreshToken.isEmpty())
        obj[QStringLiteral("refresh_token")] = m_gemini.refreshToken;
    obj[QStringLiteral("expiry_date")] = static_cast<double>(m_gemini.expiresAtMs);

    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(obj).toJson());
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    }
}

// ─────────────────────────────────────────────
// Browser-based OAuth login flow
// ─────────────────────────────────────────────

void JarvisOAuth::startLogin(const QString &provider)
{
    if (!m_activeLoginProvider.isEmpty()) {
        emit tokenError(provider, QStringLiteral("Another login is already in progress"));
        return;
    }

    if (provider == QStringLiteral("gemini")) {
        startGeminiLogin();
    } else if (provider == QStringLiteral("claude")) {
        startClaudeLogin();
    }
}

void JarvisOAuth::cancelLogin()
{
    stopLocalServer();
    m_awaitingClaudeCode = false;
    m_pkceVerifier.clear();
    m_claudeOAuthState.clear();
    if (!m_activeLoginProvider.isEmpty()) {
        const QString provider = m_activeLoginProvider;
        m_activeLoginProvider.clear();
        emit loginFinished(provider, false);
    }
}

void JarvisOAuth::startLocalServer(const std::function<void(const QString &code)> &onCode)
{
    stopLocalServer();

    m_redirectServer = new QTcpServer(this);
    // Listen on any available port on localhost
    if (!m_redirectServer->listen(QHostAddress::LocalHost, 0)) {
        qWarning() << "[JARVIS] Failed to start OAuth redirect server:" << m_redirectServer->errorString();
        emit tokenError(m_activeLoginProvider, QStringLiteral("Failed to start local server"));
        m_activeLoginProvider.clear();
        return;
    }

    connect(m_redirectServer, &QTcpServer::newConnection, this, [this, onCode]() {
        auto *socket = m_redirectServer->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, onCode]() {
            const QString request = QString::fromUtf8(socket->readAll());

            // Parse the GET request for the authorization code
            // Format: GET /?code=xxx&state=... HTTP/1.1
            QString code;
            QString error;
            const int queryStart = request.indexOf('?');
            const int queryEnd = request.indexOf(' ', queryStart);
            if (queryStart >= 0 && queryEnd > queryStart) {
                const QUrlQuery query(request.mid(queryStart + 1, queryEnd - queryStart - 1));
                code = query.queryItemValue(QStringLiteral("code"));
                error = query.queryItemValue(QStringLiteral("error"));
            }

            // Send response to browser
            const QByteArray html = error.isEmpty() && !code.isEmpty()
                ? QByteArrayLiteral(
                    "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                    "<!DOCTYPE html><html><body style='font-family:sans-serif;text-align:center;padding:60px'>"
                    "<h2>&#10003; Authentication successful</h2>"
                    "<p>You can close this tab and return to J.A.R.V.I.S.</p>"
                    "</body></html>")
                : QByteArrayLiteral(
                    "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                    "<!DOCTYPE html><html><body style='font-family:sans-serif;text-align:center;padding:60px'>"
                    "<h2>&#10007; Authentication failed</h2>"
                    "<p>Please try again from the J.A.R.V.I.S. settings.</p>"
                    "</body></html>");

            socket->write(html);
            socket->flush();
            connect(socket, &QTcpSocket::bytesWritten, socket, [socket]() {
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);

            if (!code.isEmpty()) {
                onCode(code);
            } else {
                const QString provider = m_activeLoginProvider;
                m_activeLoginProvider.clear();
                stopLocalServer();
                emit loginFinished(provider, false);
                emit tokenError(provider, error.isEmpty() ? QStringLiteral("No auth code received") : error);
            }
        });
    });
}

void JarvisOAuth::stopLocalServer()
{
    if (m_redirectServer) {
        m_redirectServer->close();
        m_redirectServer->deleteLater();
        m_redirectServer = nullptr;
    }
}

// ─────────────────────────────────────────────
// Gemini login (Google OAuth2 installed-app flow)
// ─────────────────────────────────────────────

void JarvisOAuth::startGeminiLogin()
{
    m_activeLoginProvider = QStringLiteral("gemini");
    emit loginStarted(m_activeLoginProvider);

    startLocalServer([this](const QString &code) {
        const quint16 port = m_redirectServer->serverPort();
        stopLocalServer();

        // Exchange authorization code for tokens
        QNetworkRequest request(QUrl(QString::fromLatin1(GOOGLE_TOKEN_URL)));
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/x-www-form-urlencoded"));

        QUrlQuery params;
        params.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
        params.addQueryItem(QStringLiteral("code"), code);
        params.addQueryItem(QStringLiteral("client_id"), QString::fromLatin1(GEMINI_CLIENT_ID));
        params.addQueryItem(QStringLiteral("client_secret"), QString::fromLatin1(GEMINI_CLIENT_SECRET));
        params.addQueryItem(QStringLiteral("redirect_uri"),
                            QStringLiteral("http://localhost:%1").arg(port));

        auto *reply = m_networkManager->post(request, params.toString(QUrl::FullyEncoded).toUtf8());
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();

            if (reply->error() != QNetworkReply::NoError) {
                qWarning() << "[JARVIS] Gemini token exchange failed:" << reply->errorString();
                m_activeLoginProvider.clear();
                emit loginFinished(QStringLiteral("gemini"), false);
                emit tokenError(QStringLiteral("gemini"), reply->errorString());
                return;
            }

            const auto doc = QJsonDocument::fromJson(reply->readAll());
            const auto obj = doc.object();

            m_gemini.accessToken = obj[QStringLiteral("access_token")].toString();
            m_gemini.refreshToken = obj[QStringLiteral("refresh_token")].toString();
            const int expiresIn = obj[QStringLiteral("expires_in")].toInt(3600);
            m_gemini.expiresAtMs = QDateTime::currentMSecsSinceEpoch() + (expiresIn * 1000LL);

            qDebug() << "[JARVIS] Gemini login successful, token valid for" << expiresIn << "s";

            saveGeminiCredentials();
            m_activeLoginProvider.clear();
            emit loginFinished(QStringLiteral("gemini"), true);
            emit tokenRefreshed(QStringLiteral("gemini"));
            emit loginStatusChanged();
        });
    });

    // Now open the browser with the auth URL
    const quint16 port = m_redirectServer->serverPort();
    QUrl authUrl(QString::fromLatin1(GEMINI_AUTH_URL));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), QString::fromLatin1(GEMINI_CLIENT_ID));
    query.addQueryItem(QStringLiteral("redirect_uri"),
                       QStringLiteral("http://localhost:%1").arg(port));
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("scope"), QString::fromLatin1(GEMINI_SCOPES));
    query.addQueryItem(QStringLiteral("access_type"), QStringLiteral("offline"));
    query.addQueryItem(QStringLiteral("prompt"), QStringLiteral("consent"));
    authUrl.setQuery(query);

    qDebug() << "[JARVIS] Opening browser for Gemini login on port" << port;
    QDesktopServices::openUrl(authUrl);
}

// ─────────────────────────────────────────────
// Claude login (OAuth2 + PKCE flow)
// ─────────────────────────────────────────────

QByteArray JarvisOAuth::generateCodeVerifier()
{
    // RFC 7636: 43-128 chars from unreserved URI characters
    static constexpr auto charset =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    static constexpr int len = 64;

    QByteArray verifier(len, Qt::Uninitialized);
    auto *rng = QRandomGenerator::global();
    for (int i = 0; i < len; ++i)
        verifier[i] = charset[rng->bounded(66)]; // strlen(charset) == 66
    return verifier;
}

QString JarvisOAuth::computeS256Challenge(const QByteArray &verifier)
{
    // SHA-256 hash, then base64url encode (no padding)
    const QByteArray hash = QCryptographicHash::hash(verifier, QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

void JarvisOAuth::startClaudeLogin()
{
    m_activeLoginProvider = QStringLiteral("claude");
    m_pkceVerifier = generateCodeVerifier();
    m_awaitingClaudeCode = true;
    emit loginStarted(m_activeLoginProvider);

    // Generate PKCE challenge and state
    const QString challenge = computeS256Challenge(m_pkceVerifier);
    const QByteArray stateBytes = QCryptographicHash::hash(
        generateCodeVerifier(), QCryptographicHash::Sha256).toBase64(
            QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    m_claudeOAuthState = QString::fromLatin1(stateBytes.left(32));

    // Build auth URL — uses Anthropic's redirect, not localhost
    // code=true tells Anthropic to display the code on screen for copy-paste
    QUrl authUrl(QString::fromLatin1(CLAUDE_AUTH_URL));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), QString::fromLatin1(CLAUDE_CLIENT_ID));
    query.addQueryItem(QStringLiteral("redirect_uri"), QString::fromLatin1(CLAUDE_REDIRECT_URI));
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("scope"), QString::fromLatin1(CLAUDE_SCOPES));
    query.addQueryItem(QStringLiteral("code_challenge"), challenge);
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    query.addQueryItem(QStringLiteral("state"), m_claudeOAuthState);
    query.addQueryItem(QStringLiteral("code"), QStringLiteral("true"));
    authUrl.setQuery(query);

    qDebug() << "[JARVIS] Opening browser for Claude login (code-paste flow)";
    QDesktopServices::openUrl(authUrl);
}

void JarvisOAuth::completeClaudeLogin(const QString &pastedCode)
{
    if (!m_awaitingClaudeCode || m_pkceVerifier.isEmpty()) {
        emit tokenError(QStringLiteral("claude"), QStringLiteral("No pending login flow"));
        return;
    }

    m_awaitingClaudeCode = false;

    // Parse "code#state" format
    QString code = pastedCode.trimmed();
    QString returnedState;
    const int hashIdx = code.indexOf('#');
    if (hashIdx >= 0) {
        returnedState = code.mid(hashIdx + 1);
        code = code.left(hashIdx);
    }

    // Validate state if present
    if (!returnedState.isEmpty() && returnedState != m_claudeOAuthState) {
        qWarning() << "[JARVIS] Claude OAuth state mismatch";
        m_pkceVerifier.clear();
        m_claudeOAuthState.clear();
        m_activeLoginProvider.clear();
        emit loginFinished(QStringLiteral("claude"), false);
        emit tokenError(QStringLiteral("claude"), QStringLiteral("OAuth state mismatch"));
        return;
    }

    // Exchange authorization code for tokens (JSON body, not form-urlencoded)
    QNetworkRequest request(QUrl(QString::fromLatin1(CLAUDE_TOKEN_URL)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("anthropic-beta", "oauth-2025-04-20");

    QJsonObject body;
    body[QStringLiteral("grant_type")] = QStringLiteral("authorization_code");
    body[QStringLiteral("code")] = code;
    body[QStringLiteral("state")] = m_claudeOAuthState;
    body[QStringLiteral("code_verifier")] = QString::fromLatin1(m_pkceVerifier);
    body[QStringLiteral("redirect_uri")] = QString::fromLatin1(CLAUDE_REDIRECT_URI);
    body[QStringLiteral("client_id")] = QString::fromLatin1(CLAUDE_CLIENT_ID);

    auto *reply = m_networkManager->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_pkceVerifier.clear();
        m_claudeOAuthState.clear();

        const QByteArray respBody = reply->readAll();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[JARVIS] Claude token exchange failed:" << httpStatus << reply->errorString();
            qWarning() << "[JARVIS] Response:" << respBody;
            m_activeLoginProvider.clear();
            emit loginFinished(QStringLiteral("claude"), false);
            emit tokenError(QStringLiteral("claude"), reply->errorString());
            return;
        }

        const auto doc = QJsonDocument::fromJson(respBody);
        const auto obj = doc.object();

        m_claude.accessToken = obj[QStringLiteral("access_token")].toString();
        m_claude.refreshToken = obj[QStringLiteral("refresh_token")].toString();
        const int expiresIn = obj[QStringLiteral("expires_in")].toInt(3600);
        m_claude.expiresAtMs = QDateTime::currentMSecsSinceEpoch() + (expiresIn * 1000LL);

        if (m_claude.accessToken.isEmpty()) {
            m_activeLoginProvider.clear();
            emit loginFinished(QStringLiteral("claude"), false);
            emit tokenError(QStringLiteral("claude"), QStringLiteral("No access_token in response"));
            return;
        }

        qDebug() << "[JARVIS] Claude login successful, token valid for" << expiresIn << "s";

        saveClaudeCredentials();
        m_activeLoginProvider.clear();
        emit loginFinished(QStringLiteral("claude"), true);
        emit tokenRefreshed(QStringLiteral("claude"));
        emit loginStatusChanged();
    });
}

// ─────────────────────────────────────────────
// Gemini Cloud Code project discovery
// ─────────────────────────────────────────────

void JarvisOAuth::fetchGeminiProject()
{
    if (!m_gemini.isValid() || !m_geminiProjectId.isEmpty()) return;

    qDebug() << "[JARVIS] Fetching Gemini Cloud Code project ID...";

    const QUrl url(QStringLiteral("%1:loadCodeAssist").arg(QString::fromLatin1(GEMINI_CLOUDCODE_URL)));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_gemini.accessToken).toUtf8());

    const QJsonObject body{
        {QStringLiteral("metadata"), QJsonObject{
            {QStringLiteral("ideType"), QStringLiteral("IDE_UNSPECIFIED")},
            {QStringLiteral("platform"), QStringLiteral("PLATFORM_UNSPECIFIED")},
            {QStringLiteral("pluginType"), QStringLiteral("GEMINI")},
        }},
        {QStringLiteral("mode"), QStringLiteral("HEALTH_CHECK")},
    };

    auto *reply = m_networkManager->post(request, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[JARVIS] Gemini project fetch failed:" << reply->errorString();
            return;
        }

        const auto doc = QJsonDocument::fromJson(reply->readAll());
        const auto obj = doc.object();

        m_geminiProjectId = obj[QStringLiteral("cloudaicompanionProject")].toString();
        if (!m_geminiProjectId.isEmpty()) {
            qDebug() << "[JARVIS] Gemini project ID:" << m_geminiProjectId;
            emit geminiProjectReady();
        }
    });
}
