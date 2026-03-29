#pragma once

#include <QDateTime>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include <optional>

struct UserRecord {
    qint64 id = -1;
    QString nickname;
    QString email;
};

struct MessageRecord {
    qint64 id = -1;
    qint64 senderId = -1;
    qint64 receiverId = -1;
    QString body;
    QDateTime createdAt;
};

struct FriendRequestRecord {
    qint64 id = -1;
    qint64 senderId = -1;
    QString senderNickname;
    QString senderEmail;
    QDateTime createdAt;
};

class DatabaseManager {
public:
    static DatabaseManager &instance();

    bool initialize(QString *errorMessage = nullptr);
    bool registerUser(const QString &nickname,
                      const QString &email,
                      const QString &password,
                      QString *errorMessage = nullptr);
    std::optional<UserRecord> authenticateUser(const QString &nickname,
                                               const QString &password,
                                               QString *errorMessage = nullptr);
    bool sendFriendRequest(qint64 senderId,
                           const QString &friendNickname,
                           QString *errorMessage = nullptr);
    bool acceptFriendRequest(qint64 requestId,
                             qint64 currentUserId,
                             QString *errorMessage = nullptr);
    QString createSession(qint64 userId, QString *errorMessage = nullptr);
    std::optional<UserRecord> userForSessionToken(const QString &sessionToken,
                                                  QString *errorMessage = nullptr);
    QVector<UserRecord> friendsForUser(qint64 currentUserId, QString *errorMessage = nullptr);
    QVector<FriendRequestRecord> incomingFriendRequestsForUser(qint64 currentUserId,
                                                               QString *errorMessage = nullptr);
    QVector<MessageRecord> conversation(qint64 firstUserId,
                                        qint64 secondUserId,
                                        QString *errorMessage = nullptr);
    bool storeMessage(qint64 senderId,
                      qint64 receiverId,
                      const QString &body,
                      QString *errorMessage = nullptr);
    QString databasePath() const;

private:
    DatabaseManager() = default;

    bool ensureConnected(QString *errorMessage = nullptr);
    bool executeStatement(const QString &statement, QString *errorMessage = nullptr);
    QString hashPassword(const QString &password) const;
    bool usersAreFriends(qint64 firstUserId,
                         qint64 secondUserId,
                         QString *errorMessage = nullptr);

    QString databasePath_;
    QSqlDatabase database_;
};
