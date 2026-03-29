#pragma once

#include "databasemanager.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QJsonObject>

#include <optional>

class RemoteApiClient : public QObject {
    Q_OBJECT

public:
    explicit RemoteApiClient(QObject *parent = nullptr);

    QString endpointSummary() const;
    bool ping(QString *errorMessage = nullptr);

    bool registerUser(const QString &nickname,
                      const QString &email,
                      const QString &password,
                      QString *errorMessage = nullptr);
    std::optional<UserRecord> authenticateUser(const QString &nickname,
                                               const QString &password,
                                               QString *errorMessage = nullptr);
    bool sendFriendRequest(const QString &friendNickname, QString *errorMessage = nullptr);
    bool acceptFriendRequest(qint64 requestId, QString *errorMessage = nullptr);
    QVector<UserRecord> friendsForCurrentUser(QString *errorMessage = nullptr);
    QVector<FriendRequestRecord> incomingFriendRequests(QString *errorMessage = nullptr);
    QVector<MessageRecord> conversation(qint64 friendId, QString *errorMessage = nullptr);
    bool storeMessage(qint64 receiverId,
                      const QString &body,
                      QString *errorMessage = nullptr);

    void clearSession();
    bool hasSession() const;

private:
    std::optional<QJsonObject> performRequest(const QByteArray &method,
                                              const QString &path,
                                              const QJsonObject &requestBody = QJsonObject{},
                                              QString *errorMessage = nullptr,
                                              bool authenticated = false);
    bool ensureSession(QString *errorMessage = nullptr) const;
    QByteArray authorizationHeader() const;

    QNetworkAccessManager networkAccessManager_;
    QString sessionToken_;
    std::optional<UserRecord> currentUser_;
};
