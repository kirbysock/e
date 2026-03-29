#include "remoteapiclient.h"

#include "serverconfig.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace {

constexpr int kNetworkTimeoutMs = 15000;

void setErrorMessage(QString *target, const QString &message)
{
    if (target) {
        *target = message;
    }
}

UserRecord userFromJson(const QJsonObject &object)
{
    return UserRecord{
        object.value("id").toInteger(-1),
        object.value("nickname").toString(),
        object.value("email").toString(),
    };
}

MessageRecord messageFromJson(const QJsonObject &object)
{
    return MessageRecord{
        object.value("id").toInteger(-1),
        object.value("sender_id").toInteger(-1),
        object.value("receiver_id").toInteger(-1),
        object.value("body").toString(),
        QDateTime::fromString(object.value("created_at").toString(), Qt::ISODate),
    };
}

FriendRequestRecord friendRequestFromJson(const QJsonObject &object)
{
    return FriendRequestRecord{
        object.value("id").toInteger(-1),
        object.value("sender_id").toInteger(-1),
        object.value("sender_nickname").toString(),
        object.value("sender_email").toString(),
        QDateTime::fromString(object.value("created_at").toString(), Qt::ISODate),
    };
}

QString configuredBaseUrl()
{
    const QString overrideUrl = qEnvironmentVariable("CHAT_APP_API_BASE_URL").trimmed();
    if (!overrideUrl.isEmpty()) {
        return overrideUrl;
    }

    return QString::fromLatin1(ServerConfig::kDefaultCentralApiBaseUrl);
}

QUrl buildApiUrl(const QString &path)
{
    const QString normalizedBaseUrl = configuredBaseUrl().endsWith('/')
                                          ? configuredBaseUrl().chopped(1)
                                          : configuredBaseUrl();
    const QString normalizedPath = path.startsWith('/') ? path : QStringLiteral("/%1").arg(path);
    return QUrl(normalizedBaseUrl + normalizedPath);
}

} // namespace

RemoteApiClient::RemoteApiClient(QObject *parent)
    : QObject(parent)
{
}

QString RemoteApiClient::endpointSummary() const
{
    return configuredBaseUrl();
}

bool RemoteApiClient::ping(QString *errorMessage)
{
    return performRequest("GET", "/api/status", QJsonObject{}, errorMessage).has_value();
}

bool RemoteApiClient::registerUser(const QString &nickname,
                                   const QString &email,
                                   const QString &password,
                                   QString *errorMessage)
{
    return performRequest("POST",
                          "/api/register",
                          QJsonObject{
                              {"nickname", nickname},
                              {"email", email},
                              {"password", password},
                          },
                          errorMessage)
        .has_value();
}

std::optional<UserRecord> RemoteApiClient::authenticateUser(const QString &nickname,
                                                            const QString &password,
                                                            QString *errorMessage)
{
    const auto response = performRequest("POST",
                                         "/api/login",
                                         QJsonObject{
                                             {"nickname", nickname},
                                             {"password", password},
                                         },
                                         errorMessage);
    if (!response.has_value()) {
        return std::nullopt;
    }

    const UserRecord user = userFromJson(response->value("user").toObject());
    sessionToken_ = response->value("session_token").toString();
    currentUser_ = user;
    return user;
}

bool RemoteApiClient::sendFriendRequest(const QString &friendNickname, QString *errorMessage)
{
    if (!ensureSession(errorMessage)) {
        return false;
    }

    return performRequest("POST",
                          "/api/friend-requests",
                          QJsonObject{{"nickname", friendNickname}},
                          errorMessage,
                          true)
        .has_value();
}

bool RemoteApiClient::acceptFriendRequest(qint64 requestId, QString *errorMessage)
{
    if (!ensureSession(errorMessage)) {
        return false;
    }

    return performRequest("POST",
                          QStringLiteral("/api/friend-requests/%1/accept").arg(requestId),
                          QJsonObject{},
                          errorMessage,
                          true)
        .has_value();
}

QVector<UserRecord> RemoteApiClient::friendsForCurrentUser(QString *errorMessage)
{
    QVector<UserRecord> friends;
    if (!ensureSession(errorMessage)) {
        return friends;
    }

    const auto response = performRequest("GET", "/api/friends", QJsonObject{}, errorMessage, true);
    if (!response.has_value()) {
        return friends;
    }

    const QJsonArray items = response->value("friends").toArray();
    friends.reserve(items.size());
    for (const QJsonValue &value : items) {
        friends.append(userFromJson(value.toObject()));
    }

    return friends;
}

QVector<FriendRequestRecord> RemoteApiClient::incomingFriendRequests(QString *errorMessage)
{
    QVector<FriendRequestRecord> requests;
    if (!ensureSession(errorMessage)) {
        return requests;
    }

    const auto response =
        performRequest("GET", "/api/friend-requests", QJsonObject{}, errorMessage, true);
    if (!response.has_value()) {
        return requests;
    }

    const QJsonArray items = response->value("requests").toArray();
    requests.reserve(items.size());
    for (const QJsonValue &value : items) {
        requests.append(friendRequestFromJson(value.toObject()));
    }

    return requests;
}

QVector<MessageRecord> RemoteApiClient::conversation(qint64 friendId, QString *errorMessage)
{
    QVector<MessageRecord> messages;
    if (!ensureSession(errorMessage)) {
        return messages;
    }

    const auto response = performRequest("GET",
                                         QStringLiteral("/api/conversations/%1").arg(friendId),
                                         QJsonObject{},
                                         errorMessage,
                                         true);
    if (!response.has_value()) {
        return messages;
    }

    const QJsonArray items = response->value("messages").toArray();
    messages.reserve(items.size());
    for (const QJsonValue &value : items) {
        messages.append(messageFromJson(value.toObject()));
    }

    return messages;
}

bool RemoteApiClient::storeMessage(qint64 receiverId,
                                   const QString &body,
                                   QString *errorMessage)
{
    if (!ensureSession(errorMessage)) {
        return false;
    }

    return performRequest("POST",
                          "/api/messages",
                          QJsonObject{
                              {"receiverId", receiverId},
                              {"body", body},
                          },
                          errorMessage,
                          true)
        .has_value();
}

void RemoteApiClient::clearSession()
{
    sessionToken_.clear();
    currentUser_.reset();
}

bool RemoteApiClient::hasSession() const
{
    return !sessionToken_.isEmpty();
}

std::optional<QJsonObject> RemoteApiClient::performRequest(const QByteArray &method,
                                                           const QString &path,
                                                           const QJsonObject &requestBody,
                                                           QString *errorMessage,
                                                           bool authenticated)
{
    QNetworkRequest request(buildApiUrl(path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");
    if (authenticated) {
        request.setRawHeader("Authorization", authorizationHeader());
    }

    QNetworkReply *reply = nullptr;
    const QByteArray payload = QJsonDocument(requestBody).toJson(QJsonDocument::Compact);
    if (method == "GET") {
        reply = networkAccessManager_.get(request);
    } else if (method == "POST") {
        reply = networkAccessManager_.post(request, payload);
    } else {
        reply = networkAccessManager_.sendCustomRequest(request, method, payload);
    }

    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    bool timedOut = false;

    connect(&timeoutTimer, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        if (reply) {
            reply->abort();
        }
        loop.quit();
    });
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    timeoutTimer.start(kNetworkTimeoutMs);
    loop.exec();
    timeoutTimer.stop();

    const QByteArray responseBytes = reply->readAll();
    const QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);

    if (reply->error() != QNetworkReply::NoError) {
        QString message = reply->errorString();
        if (timedOut) {
            message = QStringLiteral("The server timed out at %1").arg(endpointSummary());
        }

        const QJsonDocument errorDocument = QJsonDocument::fromJson(responseBytes);
        if (errorDocument.isObject()) {
            const QString apiError = errorDocument.object().value("error").toString();
            if (!apiError.isEmpty()) {
                message = apiError;
            }
        }

        setErrorMessage(errorMessage, message);
        reply->deleteLater();
        return std::nullopt;
    }

    if (!statusCode.isValid() || statusCode.toInt() < 200 || statusCode.toInt() >= 300) {
        const QJsonDocument errorDocument = QJsonDocument::fromJson(responseBytes);
        QString message = QStringLiteral("Unexpected server response from %1").arg(endpointSummary());
        if (errorDocument.isObject()) {
            const QString apiError = errorDocument.object().value("error").toString();
            if (!apiError.isEmpty()) {
                message = apiError;
            }
        }

        setErrorMessage(errorMessage, message);
        reply->deleteLater();
        return std::nullopt;
    }

    const QJsonDocument responseDocument = QJsonDocument::fromJson(responseBytes);
    if (!responseDocument.isObject()) {
        setErrorMessage(errorMessage, "The server returned invalid JSON.");
        reply->deleteLater();
        return std::nullopt;
    }

    const QJsonObject responseObject = responseDocument.object();
    reply->deleteLater();
    return responseObject;
}

bool RemoteApiClient::ensureSession(QString *errorMessage) const
{
    if (!sessionToken_.isEmpty()) {
        return true;
    }

    setErrorMessage(errorMessage, "Log in again to continue.");
    return false;
}

QByteArray RemoteApiClient::authorizationHeader() const
{
    return QByteArray("Bearer ") + sessionToken_.toUtf8();
}
