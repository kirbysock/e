#pragma once

#include "databasemanager.h"

#include <QSize>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTimer;
class QFrame;
class RemoteApiClient;

class MainWindow : public QWidget {
    Q_OBJECT

public:
    explicit MainWindow(RemoteApiClient *apiClient, QWidget *parent = nullptr);

private slots:
    void showWelcomePage();
    void showRegisterPage();
    void showLoginPage();
    void handleRegister();
    void handleLogin();
    void handleFriendSelectionChanged();
    void sendCurrentMessage();
    void refreshChatData();
    void logoutCurrentUser();
    void showAddFriendDialog();
    void acceptSelectedFriendRequest();
    void updateRequestActions();

private:
    QWidget *buildWelcomePage();
    QWidget *buildRegisterPage();
    QWidget *buildLoginPage();
    QWidget *buildChatPage();
    QPushButton *createBlackButton(const QString &text, const QSize &minimumSize = QSize(280, 58));
    QPushButton *createRoundButton(const QString &text, int sideLength = 46);
    QLineEdit *createPrimaryInput(const QString &placeholder, bool password = false);
    void configureListWidget(QListWidget *listWidget, bool selectable);
    void applyTheme();
    void setFeedback(QLabel *label, const QString &message, const QString &color);
    void clearRegisterForm();
    void clearLoginForm();
    void populateFriends();
    void populateIncomingRequests();
    void populateConversation(qint64 friendId);
    void refreshServerStatus(const QString &detail = QString());
    qint64 selectedFriendId() const;
    qint64 selectedRequestId() const;
    QString friendNameFor(qint64 friendId) const;

    RemoteApiClient *apiClient_ = nullptr;

    QStackedWidget *pages_ = nullptr;
    QWidget *welcomePage_ = nullptr;
    QWidget *registerPage_ = nullptr;
    QWidget *loginPage_ = nullptr;
    QWidget *chatPage_ = nullptr;

    QLabel *serverStatusLabel_ = nullptr;

    QLineEdit *registerNicknameEdit_ = nullptr;
    QLineEdit *registerEmailEdit_ = nullptr;
    QLineEdit *registerPasswordEdit_ = nullptr;
    QLabel *registerFeedbackLabel_ = nullptr;

    QLineEdit *loginNicknameEdit_ = nullptr;
    QLineEdit *loginPasswordEdit_ = nullptr;
    QLabel *loginFeedbackLabel_ = nullptr;

    QLabel *currentUserLabel_ = nullptr;
    QPushButton *addFriendButton_ = nullptr;
    QListWidget *friendsList_ = nullptr;
    QListWidget *incomingRequestsList_ = nullptr;
    QPushButton *acceptRequestButton_ = nullptr;
    QLabel *activeConversationLabel_ = nullptr;
    QListWidget *messagesList_ = nullptr;
    QLineEdit *messageInput_ = nullptr;
    QPushButton *sendButton_ = nullptr;
    QLabel *chatFeedbackLabel_ = nullptr;

    QTimer *refreshTimer_ = nullptr;

    qint64 currentUserId_ = -1;
    QString currentUserNickname_;
    QVector<UserRecord> friends_;
    QVector<FriendRequestRecord> incomingRequests_;
};
