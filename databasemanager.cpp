#include "databasemanager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QUuid>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

namespace {

QString currentTimestampUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

void orderedUserIds(qint64 firstUserId, qint64 secondUserId, qint64 *lowerUserId, qint64 *higherUserId)
{
    if (firstUserId <= secondUserId) {
        *lowerUserId = firstUserId;
        *higherUserId = secondUserId;
        return;
    }

    *lowerUserId = secondUserId;
    *higherUserId = firstUserId;
}

void setErrorMessage(QString *target, const QString &message)
{
    if (target) {
        *target = message;
    }
}

} // namespace

DatabaseManager &DatabaseManager::instance()
{
    static DatabaseManager manager;
    return manager;
}

bool DatabaseManager::initialize(QString *errorMessage)
{
    if (database_.isValid() && database_.isOpen()) {
        return true;
    }

    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (appDataPath.isEmpty()) {
        setErrorMessage(errorMessage, "Could not resolve an application data folder.");
        return false;
    }

    QDir dataDirectory(appDataPath);
    if (!dataDirectory.exists() && !QDir().mkpath(appDataPath)) {
        setErrorMessage(errorMessage, "Could not create the application data folder.");
        return false;
    }

    databasePath_ = dataDirectory.filePath("chatting_app.sqlite");

    if (QSqlDatabase::contains("chat_connection")) {
        database_ = QSqlDatabase::database("chat_connection");
    } else {
        database_ = QSqlDatabase::addDatabase("QSQLITE", "chat_connection");
    }

    database_.setDatabaseName(databasePath_);
    if (!database_.open()) {
        setErrorMessage(errorMessage, QStringLiteral("Could not open SQLite database: %1")
                                          .arg(database_.lastError().text()));
        return false;
    }

    if (!executeStatement(
            "CREATE TABLE IF NOT EXISTS users ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "nickname TEXT NOT NULL UNIQUE,"
            "email TEXT NOT NULL UNIQUE,"
            "password_hash TEXT NOT NULL,"
            "created_at TEXT NOT NULL"
            ");",
            errorMessage)) {
        return false;
    }

    if (!executeStatement(
            "CREATE TABLE IF NOT EXISTS messages ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "sender_id INTEGER NOT NULL,"
            "receiver_id INTEGER NOT NULL,"
            "body TEXT NOT NULL,"
            "created_at TEXT NOT NULL,"
            "FOREIGN KEY(sender_id) REFERENCES users(id),"
            "FOREIGN KEY(receiver_id) REFERENCES users(id)"
            ");",
            errorMessage)) {
        return false;
    }

    if (!executeStatement(
            "CREATE INDEX IF NOT EXISTS idx_messages_pair "
            "ON messages(sender_id, receiver_id, created_at);",
            errorMessage)) {
        return false;
    }

    if (!executeStatement(
            "CREATE TABLE IF NOT EXISTS friendships ("
            "user_one_id INTEGER NOT NULL,"
            "user_two_id INTEGER NOT NULL,"
            "created_at TEXT NOT NULL,"
            "PRIMARY KEY(user_one_id, user_two_id),"
            "FOREIGN KEY(user_one_id) REFERENCES users(id),"
            "FOREIGN KEY(user_two_id) REFERENCES users(id)"
            ");",
            errorMessage)) {
        return false;
    }

    if (!executeStatement(
            "CREATE TABLE IF NOT EXISTS friend_requests ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "sender_id INTEGER NOT NULL,"
            "receiver_id INTEGER NOT NULL,"
            "status TEXT NOT NULL,"
            "created_at TEXT NOT NULL,"
            "responded_at TEXT,"
            "FOREIGN KEY(sender_id) REFERENCES users(id),"
            "FOREIGN KEY(receiver_id) REFERENCES users(id)"
            ");",
            errorMessage)) {
        return false;
    }

    if (!executeStatement(
            "CREATE INDEX IF NOT EXISTS idx_friendships_user_one "
            "ON friendships(user_one_id);",
            errorMessage)) {
        return false;
    }

    if (!executeStatement(
            "CREATE INDEX IF NOT EXISTS idx_friendships_user_two "
            "ON friendships(user_two_id);",
            errorMessage)) {
        return false;
    }

    if (!executeStatement(
            "CREATE INDEX IF NOT EXISTS idx_friend_requests_receiver "
            "ON friend_requests(receiver_id, status, created_at);",
            errorMessage)) {
        return false;
    }

    if (!executeStatement(
            "CREATE INDEX IF NOT EXISTS idx_friend_requests_pair "
            "ON friend_requests(sender_id, receiver_id, status);",
            errorMessage)) {
        return false;
    }

    if (!executeStatement(
            "CREATE TABLE IF NOT EXISTS sessions ("
            "token TEXT PRIMARY KEY,"
            "user_id INTEGER NOT NULL,"
            "created_at TEXT NOT NULL,"
            "last_seen_at TEXT NOT NULL,"
            "FOREIGN KEY(user_id) REFERENCES users(id)"
            ");",
            errorMessage)) {
        return false;
    }

    if (!executeStatement(
            "CREATE INDEX IF NOT EXISTS idx_sessions_user "
            "ON sessions(user_id);",
            errorMessage)) {
        return false;
    }

    return true;
}

bool DatabaseManager::registerUser(const QString &nickname,
                                   const QString &email,
                                   const QString &password,
                                   QString *errorMessage)
{
    if (!ensureConnected(errorMessage)) {
        return false;
    }

    const QString trimmedNickname = nickname.trimmed();
    const QString trimmedEmail = email.trimmed();

    if (trimmedNickname.isEmpty()) {
        setErrorMessage(errorMessage, "Nickname is required.");
        return false;
    }

    if (trimmedEmail.isEmpty() || !trimmedEmail.contains('@')) {
        setErrorMessage(errorMessage, "Enter a valid email address.");
        return false;
    }

    if (password.size() < 6) {
        setErrorMessage(errorMessage, "Password must be at least 6 characters.");
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(
        "INSERT INTO users (nickname, email, password_hash, created_at) "
        "VALUES (?, ?, ?, ?);");
    query.addBindValue(trimmedNickname);
    query.addBindValue(trimmedEmail);
    query.addBindValue(hashPassword(password));
    query.addBindValue(currentTimestampUtc());

    if (!query.exec()) {
        const QString dbError = query.lastError().text();
        if (dbError.contains("users.nickname") || dbError.contains("nickname")) {
            setErrorMessage(errorMessage, "That nickname is already taken.");
        } else if (dbError.contains("users.email") || dbError.contains("email")) {
            setErrorMessage(errorMessage, "That email is already registered.");
        } else {
            setErrorMessage(errorMessage, QStringLiteral("Could not save the account: %1").arg(dbError));
        }
        return false;
    }

    return true;
}

std::optional<UserRecord> DatabaseManager::authenticateUser(const QString &nickname,
                                                            const QString &password,
                                                            QString *errorMessage)
{
    if (!ensureConnected(errorMessage)) {
        return std::nullopt;
    }

    const QString trimmedNickname = nickname.trimmed();
    if (trimmedNickname.isEmpty() || password.isEmpty()) {
        setErrorMessage(errorMessage, "Nickname and password are required.");
        return std::nullopt;
    }

    QSqlQuery query(database_);
    query.prepare(
        "SELECT id, nickname, email, password_hash "
        "FROM users WHERE nickname = ?;");
    query.addBindValue(trimmedNickname);

    if (!query.exec()) {
        setErrorMessage(errorMessage, QStringLiteral("Could not load the account: %1")
                                          .arg(query.lastError().text()));
        return std::nullopt;
    }

    if (!query.next()) {
        setErrorMessage(errorMessage, "No account was found for that nickname.");
        return std::nullopt;
    }

    if (query.value(3).toString() != hashPassword(password)) {
        setErrorMessage(errorMessage, "Password is incorrect.");
        return std::nullopt;
    }

    return UserRecord{
        query.value(0).toLongLong(),
        query.value(1).toString(),
        query.value(2).toString(),
    };
}

bool DatabaseManager::sendFriendRequest(qint64 senderId,
                                        const QString &friendNickname,
                                        QString *errorMessage)
{
    if (!ensureConnected(errorMessage)) {
        return false;
    }

    const QString trimmedNickname = friendNickname.trimmed();
    if (trimmedNickname.isEmpty()) {
        setErrorMessage(errorMessage, "Enter a nickname to send a request.");
        return false;
    }

    QSqlQuery lookupQuery(database_);
    lookupQuery.prepare(
        "SELECT id FROM users WHERE nickname = ?;");
    lookupQuery.addBindValue(trimmedNickname);

    if (!lookupQuery.exec()) {
        setErrorMessage(errorMessage, QStringLiteral("Could not find that user: %1")
                                          .arg(lookupQuery.lastError().text()));
        return false;
    }

    if (!lookupQuery.next()) {
        setErrorMessage(errorMessage, "No user was found with that nickname.");
        return false;
    }

    const qint64 receiverId = lookupQuery.value(0).toLongLong();
    if (receiverId == senderId) {
        setErrorMessage(errorMessage, "You cannot send a friend request to yourself.");
        return false;
    }

    QString friendshipError;
    if (usersAreFriends(senderId, receiverId, &friendshipError)) {
        setErrorMessage(errorMessage, "That user is already in your friends list.");
        return false;
    }
    if (!friendshipError.isEmpty()) {
        setErrorMessage(errorMessage, friendshipError);
        return false;
    }

    QSqlQuery duplicateQuery(database_);
    duplicateQuery.prepare(
        "SELECT sender_id, receiver_id "
        "FROM friend_requests "
        "WHERE status = 'pending' "
        "  AND ((sender_id = ? AND receiver_id = ?) "
        "    OR (sender_id = ? AND receiver_id = ?)) "
        "ORDER BY id DESC "
        "LIMIT 1;");
    duplicateQuery.addBindValue(senderId);
    duplicateQuery.addBindValue(receiverId);
    duplicateQuery.addBindValue(receiverId);
    duplicateQuery.addBindValue(senderId);

    if (!duplicateQuery.exec()) {
        setErrorMessage(errorMessage, QStringLiteral("Could not check existing requests: %1")
                                          .arg(duplicateQuery.lastError().text()));
        return false;
    }

    if (duplicateQuery.next()) {
        const qint64 existingSenderId = duplicateQuery.value(0).toLongLong();
        if (existingSenderId == senderId) {
            setErrorMessage(errorMessage, "You already sent a pending request to that user.");
        } else {
            setErrorMessage(errorMessage,
                            "That user already sent you a request. Accept it from the requests list.");
        }
        return false;
    }

    QSqlQuery insertQuery(database_);
    insertQuery.prepare(
        "INSERT INTO friend_requests (sender_id, receiver_id, status, created_at) "
        "VALUES (?, ?, 'pending', ?);");
    insertQuery.addBindValue(senderId);
    insertQuery.addBindValue(receiverId);
    insertQuery.addBindValue(currentTimestampUtc());

    if (!insertQuery.exec()) {
        setErrorMessage(errorMessage, QStringLiteral("Could not send the friend request: %1")
                                          .arg(insertQuery.lastError().text()));
        return false;
    }

    return true;
}

bool DatabaseManager::acceptFriendRequest(qint64 requestId,
                                          qint64 currentUserId,
                                          QString *errorMessage)
{
    if (!ensureConnected(errorMessage)) {
        return false;
    }

    QSqlQuery requestQuery(database_);
    requestQuery.prepare(
        "SELECT sender_id, receiver_id, status "
        "FROM friend_requests "
        "WHERE id = ?;");
    requestQuery.addBindValue(requestId);

    if (!requestQuery.exec()) {
        setErrorMessage(errorMessage, QStringLiteral("Could not load the request: %1")
                                          .arg(requestQuery.lastError().text()));
        return false;
    }

    if (!requestQuery.next()) {
        setErrorMessage(errorMessage, "That friend request no longer exists.");
        return false;
    }

    const qint64 senderId = requestQuery.value(0).toLongLong();
    const qint64 receiverId = requestQuery.value(1).toLongLong();
    const QString status = requestQuery.value(2).toString();

    if (receiverId != currentUserId) {
        setErrorMessage(errorMessage, "You can only accept requests sent to your account.");
        return false;
    }

    if (status != QLatin1String("pending")) {
        setErrorMessage(errorMessage, "That request was already handled.");
        return false;
    }

    qint64 lowerUserId = -1;
    qint64 higherUserId = -1;
    orderedUserIds(senderId, receiverId, &lowerUserId, &higherUserId);

    if (!database_.transaction()) {
        setErrorMessage(errorMessage, QStringLiteral("Could not start the friendship update: %1")
                                          .arg(database_.lastError().text()));
        return false;
    }

    QSqlQuery friendshipQuery(database_);
    friendshipQuery.prepare(
        "INSERT OR IGNORE INTO friendships (user_one_id, user_two_id, created_at) "
        "VALUES (?, ?, ?);");
    friendshipQuery.addBindValue(lowerUserId);
    friendshipQuery.addBindValue(higherUserId);
    friendshipQuery.addBindValue(currentTimestampUtc());

    if (!friendshipQuery.exec()) {
        database_.rollback();
        setErrorMessage(errorMessage, QStringLiteral("Could not create the friendship: %1")
                                          .arg(friendshipQuery.lastError().text()));
        return false;
    }

    QSqlQuery updateRequestQuery(database_);
    updateRequestQuery.prepare(
        "UPDATE friend_requests "
        "SET status = 'accepted', responded_at = ? "
        "WHERE id = ?;");
    updateRequestQuery.addBindValue(currentTimestampUtc());
    updateRequestQuery.addBindValue(requestId);

    if (!updateRequestQuery.exec()) {
        database_.rollback();
        setErrorMessage(errorMessage, QStringLiteral("Could not update the request: %1")
                                          .arg(updateRequestQuery.lastError().text()));
        return false;
    }

    if (!database_.commit()) {
        database_.rollback();
        setErrorMessage(errorMessage, QStringLiteral("Could not finish the friendship update: %1")
                                          .arg(database_.lastError().text()));
        return false;
    }

    return true;
}

QString DatabaseManager::createSession(qint64 userId, QString *errorMessage)
{
    if (!ensureConnected(errorMessage)) {
        return {};
    }

    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString timestamp = currentTimestampUtc();

    QSqlQuery query(database_);
    query.prepare(
        "INSERT INTO sessions (token, user_id, created_at, last_seen_at) "
        "VALUES (?, ?, ?, ?);");
    query.addBindValue(token);
    query.addBindValue(userId);
    query.addBindValue(timestamp);
    query.addBindValue(timestamp);

    if (!query.exec()) {
        setErrorMessage(errorMessage, QStringLiteral("Could not create a session: %1")
                                          .arg(query.lastError().text()));
        return {};
    }

    return token;
}

std::optional<UserRecord> DatabaseManager::userForSessionToken(const QString &sessionToken,
                                                               QString *errorMessage)
{
    if (!ensureConnected(errorMessage)) {
        return std::nullopt;
    }

    const QString trimmedToken = sessionToken.trimmed();
    if (trimmedToken.isEmpty()) {
        setErrorMessage(errorMessage, "Session token is missing.");
        return std::nullopt;
    }

    QSqlQuery query(database_);
    query.prepare(
        "SELECT u.id, u.nickname, u.email "
        "FROM sessions s "
        "JOIN users u ON u.id = s.user_id "
        "WHERE s.token = ? "
        "LIMIT 1;");
    query.addBindValue(trimmedToken);

    if (!query.exec()) {
        setErrorMessage(errorMessage, QStringLiteral("Could not load the session: %1")
                                          .arg(query.lastError().text()));
        return std::nullopt;
    }

    if (!query.next()) {
        setErrorMessage(errorMessage, "Your session is no longer valid. Please log in again.");
        return std::nullopt;
    }

    QSqlQuery updateQuery(database_);
    updateQuery.prepare(
        "UPDATE sessions SET last_seen_at = ? WHERE token = ?;");
    updateQuery.addBindValue(currentTimestampUtc());
    updateQuery.addBindValue(trimmedToken);
    updateQuery.exec();

    return UserRecord{
        query.value(0).toLongLong(),
        query.value(1).toString(),
        query.value(2).toString(),
    };
}

QVector<UserRecord> DatabaseManager::friendsForUser(qint64 currentUserId, QString *errorMessage)
{
    QVector<UserRecord> friends;

    if (!ensureConnected(errorMessage)) {
        return friends;
    }

    QSqlQuery query(database_);
    query.prepare(
        "SELECT u.id, u.nickname, u.email "
        "FROM friendships f "
        "JOIN users u ON u.id = CASE "
        "    WHEN f.user_one_id = ? THEN f.user_two_id "
        "    ELSE f.user_one_id "
        "END "
        "WHERE f.user_one_id = ? OR f.user_two_id = ? "
        "ORDER BY nickname COLLATE NOCASE ASC;");
    query.addBindValue(currentUserId);
    query.addBindValue(currentUserId);
    query.addBindValue(currentUserId);

    if (!query.exec()) {
        setErrorMessage(errorMessage, QStringLiteral("Could not load the friends list: %1")
                                          .arg(query.lastError().text()));
        return friends;
    }

    while (query.next()) {
        friends.append(UserRecord{
            query.value(0).toLongLong(),
            query.value(1).toString(),
            query.value(2).toString(),
        });
    }

    return friends;
}

QVector<FriendRequestRecord> DatabaseManager::incomingFriendRequestsForUser(qint64 currentUserId,
                                                                            QString *errorMessage)
{
    QVector<FriendRequestRecord> requests;

    if (!ensureConnected(errorMessage)) {
        return requests;
    }

    QSqlQuery query(database_);
    query.prepare(
        "SELECT fr.id, u.id, u.nickname, u.email, fr.created_at "
        "FROM friend_requests fr "
        "JOIN users u ON u.id = fr.sender_id "
        "WHERE fr.receiver_id = ? AND fr.status = 'pending' "
        "ORDER BY fr.created_at ASC, fr.id ASC;");
    query.addBindValue(currentUserId);

    if (!query.exec()) {
        setErrorMessage(errorMessage, QStringLiteral("Could not load friend requests: %1")
                                          .arg(query.lastError().text()));
        return requests;
    }

    while (query.next()) {
        requests.append(FriendRequestRecord{
            query.value(0).toLongLong(),
            query.value(1).toLongLong(),
            query.value(2).toString(),
            query.value(3).toString(),
            QDateTime::fromString(query.value(4).toString(), Qt::ISODate),
        });
    }

    return requests;
}

QVector<MessageRecord> DatabaseManager::conversation(qint64 firstUserId,
                                                     qint64 secondUserId,
                                                     QString *errorMessage)
{
    QVector<MessageRecord> messages;

    if (!ensureConnected(errorMessage)) {
        return messages;
    }

    QSqlQuery query(database_);
    query.prepare(
        "SELECT id, sender_id, receiver_id, body, created_at "
        "FROM messages "
        "WHERE (sender_id = ? AND receiver_id = ?) "
        "   OR (sender_id = ? AND receiver_id = ?) "
        "ORDER BY created_at ASC, id ASC;");
    query.addBindValue(firstUserId);
    query.addBindValue(secondUserId);
    query.addBindValue(secondUserId);
    query.addBindValue(firstUserId);

    if (!query.exec()) {
        setErrorMessage(errorMessage, QStringLiteral("Could not load messages: %1")
                                          .arg(query.lastError().text()));
        return messages;
    }

    while (query.next()) {
        messages.append(MessageRecord{
            query.value(0).toLongLong(),
            query.value(1).toLongLong(),
            query.value(2).toLongLong(),
            query.value(3).toString(),
            QDateTime::fromString(query.value(4).toString(), Qt::ISODate),
        });
    }

    return messages;
}

bool DatabaseManager::storeMessage(qint64 senderId,
                                   qint64 receiverId,
                                   const QString &body,
                                   QString *errorMessage)
{
    if (!ensureConnected(errorMessage)) {
        return false;
    }

    const QString trimmedBody = body.trimmed();
    if (trimmedBody.isEmpty()) {
        setErrorMessage(errorMessage, "Write a message before sending.");
        return false;
    }

    QString friendshipError;
    if (!usersAreFriends(senderId, receiverId, &friendshipError)) {
        if (!friendshipError.isEmpty()) {
            setErrorMessage(errorMessage, friendshipError);
            return false;
        }
        setErrorMessage(errorMessage, "You can only message users who accepted your friend request.");
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(
        "INSERT INTO messages (sender_id, receiver_id, body, created_at) "
        "VALUES (?, ?, ?, ?);");
    query.addBindValue(senderId);
    query.addBindValue(receiverId);
    query.addBindValue(trimmedBody);
    query.addBindValue(currentTimestampUtc());

    if (!query.exec()) {
        setErrorMessage(errorMessage, QStringLiteral("Could not save the message: %1")
                                          .arg(query.lastError().text()));
        return false;
    }

    return true;
}

QString DatabaseManager::databasePath() const
{
    return databasePath_;
}

bool DatabaseManager::ensureConnected(QString *errorMessage)
{
    if (database_.isValid() && database_.isOpen()) {
        return true;
    }

    return initialize(errorMessage);
}

bool DatabaseManager::executeStatement(const QString &statement, QString *errorMessage)
{
    QSqlQuery query(database_);
    if (!query.exec(statement)) {
        setErrorMessage(errorMessage, query.lastError().text());
        return false;
    }

    return true;
}

QString DatabaseManager::hashPassword(const QString &password) const
{
    return QString::fromLatin1(
        QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool DatabaseManager::usersAreFriends(qint64 firstUserId,
                                      qint64 secondUserId,
                                      QString *errorMessage)
{
    if (!ensureConnected(errorMessage)) {
        return false;
    }

    qint64 lowerUserId = -1;
    qint64 higherUserId = -1;
    orderedUserIds(firstUserId, secondUserId, &lowerUserId, &higherUserId);

    QSqlQuery query(database_);
    query.prepare(
        "SELECT 1 "
        "FROM friendships "
        "WHERE user_one_id = ? AND user_two_id = ? "
        "LIMIT 1;");
    query.addBindValue(lowerUserId);
    query.addBindValue(higherUserId);

    if (!query.exec()) {
        setErrorMessage(errorMessage, QStringLiteral("Could not verify the friendship: %1")
                                          .arg(query.lastError().text()));
        return false;
    }

    return query.next();
}
