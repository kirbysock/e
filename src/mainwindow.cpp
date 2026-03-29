#include "mainwindow.h"

#include "remoteapiclient.h"

#include <QAbstractItemView>
#include <QColor>
#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>

namespace {

constexpr auto kPageMargin = 30;
constexpr auto kPanelRadius = 30;

QWidget *buildCenteredPage(QWidget *content)
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch();
    layout->addWidget(content, 0, Qt::AlignCenter);
    layout->addStretch();
    return page;
}

} // namespace

MainWindow::MainWindow(RemoteApiClient *apiClient, QWidget *parent)
    : QWidget(parent)
    , apiClient_(apiClient)
    , pages_(new QStackedWidget(this))
    , refreshTimer_(new QTimer(this))
{
    setObjectName("AppRoot");
    setWindowTitle("Chatting App");
    setMinimumSize(1120, 720);
    resize(1280, 800);

    applyTheme();

    welcomePage_ = buildWelcomePage();
    registerPage_ = buildRegisterPage();
    loginPage_ = buildLoginPage();
    chatPage_ = buildChatPage();

    pages_->addWidget(welcomePage_);
    pages_->addWidget(registerPage_);
    pages_->addWidget(loginPage_);
    pages_->addWidget(chatPage_);

    serverStatusLabel_ = new QLabel(this);
    serverStatusLabel_->setAlignment(Qt::AlignCenter);
    serverStatusLabel_->setWordWrap(true);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, 20);
    rootLayout->setSpacing(16);
    rootLayout->addWidget(pages_, 1);
    rootLayout->addWidget(serverStatusLabel_);

    connect(refreshTimer_, &QTimer::timeout, this, &MainWindow::refreshChatData);
    refreshTimer_->setInterval(1500);

    refreshServerStatus();
    pages_->setCurrentWidget(welcomePage_);
}

QWidget *MainWindow::buildWelcomePage()
{
    auto *container = new QWidget;
    container->setMinimumWidth(320);
    container->setMaximumWidth(420);

    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(18);

    auto *loginButton = createBlackButton("Login");
    auto *registerButton = createBlackButton("Register");

    layout->addWidget(loginButton);
    layout->addWidget(registerButton);

    connect(loginButton, &QPushButton::clicked, this, &MainWindow::showLoginPage);
    connect(registerButton, &QPushButton::clicked, this, &MainWindow::showRegisterPage);

    return buildCenteredPage(container);
}

QWidget *MainWindow::buildRegisterPage()
{
    auto *container = new QWidget;
    container->setMinimumWidth(320);
    container->setMaximumWidth(420);

    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    registerNicknameEdit_ = createPrimaryInput("Nickname");
    registerEmailEdit_ = createPrimaryInput("Email");
    registerPasswordEdit_ = createPrimaryInput("Password", true);

    auto *applyButton = createBlackButton("Apply");
    auto *backButton = createBlackButton("Back", QSize(280, 52));
    registerFeedbackLabel_ = new QLabel;
    registerFeedbackLabel_->setAlignment(Qt::AlignCenter);
    registerFeedbackLabel_->setWordWrap(true);

    layout->addWidget(registerNicknameEdit_);
    layout->addWidget(registerEmailEdit_);
    layout->addWidget(registerPasswordEdit_);
    layout->addWidget(applyButton);
    layout->addWidget(backButton);
    layout->addWidget(registerFeedbackLabel_);

    connect(applyButton, &QPushButton::clicked, this, &MainWindow::handleRegister);
    connect(backButton, &QPushButton::clicked, this, &MainWindow::showWelcomePage);
    connect(registerPasswordEdit_, &QLineEdit::returnPressed, this, &MainWindow::handleRegister);

    return buildCenteredPage(container);
}

QWidget *MainWindow::buildLoginPage()
{
    auto *container = new QWidget;
    container->setMinimumWidth(320);
    container->setMaximumWidth(420);

    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    loginNicknameEdit_ = createPrimaryInput("Nickname");
    loginPasswordEdit_ = createPrimaryInput("Password", true);

    auto *loginButton = createBlackButton("Login");
    auto *backButton = createBlackButton("Back", QSize(280, 52));
    loginFeedbackLabel_ = new QLabel;
    loginFeedbackLabel_->setAlignment(Qt::AlignCenter);
    loginFeedbackLabel_->setWordWrap(true);

    layout->addWidget(loginNicknameEdit_);
    layout->addWidget(loginPasswordEdit_);
    layout->addWidget(loginButton);
    layout->addWidget(backButton);
    layout->addWidget(loginFeedbackLabel_);

    connect(loginButton, &QPushButton::clicked, this, &MainWindow::handleLogin);
    connect(backButton, &QPushButton::clicked, this, &MainWindow::showWelcomePage);
    connect(loginPasswordEdit_, &QLineEdit::returnPressed, this, &MainWindow::handleLogin);

    return buildCenteredPage(container);
}

QWidget *MainWindow::buildChatPage()
{
    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(22);

    auto *leftColumn = new QWidget;
    leftColumn->setMinimumWidth(290);
    leftColumn->setMaximumWidth(360);

    auto *leftColumnLayout = new QVBoxLayout(leftColumn);
    leftColumnLayout->setContentsMargins(0, 0, 0, 0);
    leftColumnLayout->setSpacing(16);

    auto *leftPanel = new QFrame;
    leftPanel->setObjectName("PanelFrame");

    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(24, 24, 24, 24);
    leftLayout->setSpacing(18);

    auto *accountRowLayout = new QHBoxLayout;
    accountRowLayout->setContentsMargins(0, 0, 0, 0);
    accountRowLayout->setSpacing(12);

    currentUserLabel_ = new QLabel("Signed in as");
    currentUserLabel_->setObjectName("SectionTitle");
    currentUserLabel_->setWordWrap(true);

    addFriendButton_ = createRoundButton("+");
    auto *divider = new QFrame;
    divider->setObjectName("SectionDivider");
    divider->setFrameShape(QFrame::HLine);

    auto *friendsTitle = new QLabel("Friends");
    friendsTitle->setObjectName("SectionTitle");

    friendsList_ = new QListWidget;
    friendsList_->setObjectName("FriendsList");
    configureListWidget(friendsList_, true);

    auto *requestsTitle = new QLabel("Requests");
    requestsTitle->setObjectName("SectionTitle");

    incomingRequestsList_ = new QListWidget;
    incomingRequestsList_->setObjectName("RequestsList");
    incomingRequestsList_->setMaximumHeight(210);
    configureListWidget(incomingRequestsList_, true);

    acceptRequestButton_ = createBlackButton("Accept Request", QSize(240, 52));

    accountRowLayout->addWidget(currentUserLabel_, 1);
    accountRowLayout->addWidget(addFriendButton_, 0, Qt::AlignTop);

    leftLayout->addLayout(accountRowLayout);
    leftLayout->addWidget(divider);
    leftLayout->addWidget(friendsTitle);
    leftLayout->addWidget(friendsList_, 1);
    leftLayout->addWidget(requestsTitle);
    leftLayout->addWidget(incomingRequestsList_);
    leftLayout->addWidget(acceptRequestButton_, 0, Qt::AlignLeft);

    auto *logoutButton = createBlackButton("Logout", QSize(220, 52));
    leftColumnLayout->addWidget(leftPanel, 1);
    leftColumnLayout->addWidget(logoutButton, 0, Qt::AlignHCenter);

    auto *rightPanel = new QFrame;
    rightPanel->setObjectName("PanelFrame");

    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(26, 26, 26, 26);
    rightLayout->setSpacing(18);

    activeConversationLabel_ = new QLabel("Pick a friend from the left panel.");
    activeConversationLabel_->setObjectName("SectionTitle");
    activeConversationLabel_->setWordWrap(true);

    messagesList_ = new QListWidget;
    messagesList_->setObjectName("MessagesList");
    configureListWidget(messagesList_, false);

    auto *composerLayout = new QHBoxLayout;
    composerLayout->setSpacing(14);
    messageInput_ = createPrimaryInput("Write a message");
    messageInput_->setMinimumHeight(52);
    sendButton_ = createBlackButton("Send", QSize(150, 52));
    composerLayout->addWidget(messageInput_, 1);
    composerLayout->addWidget(sendButton_);

    chatFeedbackLabel_ = new QLabel;
    chatFeedbackLabel_->setWordWrap(true);

    rightLayout->addWidget(activeConversationLabel_);
    rightLayout->addWidget(messagesList_, 1);
    rightLayout->addLayout(composerLayout);
    rightLayout->addWidget(chatFeedbackLabel_);

    layout->addWidget(leftColumn);
    layout->addWidget(rightPanel, 1);

    connect(logoutButton, &QPushButton::clicked, this, &MainWindow::logoutCurrentUser);
    connect(addFriendButton_, &QPushButton::clicked, this, &MainWindow::showAddFriendDialog);
    connect(friendsList_, &QListWidget::itemSelectionChanged,
            this, &MainWindow::handleFriendSelectionChanged);
    connect(incomingRequestsList_, &QListWidget::itemSelectionChanged,
            this, &MainWindow::updateRequestActions);
    connect(acceptRequestButton_, &QPushButton::clicked,
            this, &MainWindow::acceptSelectedFriendRequest);
    connect(sendButton_, &QPushButton::clicked, this, &MainWindow::sendCurrentMessage);
    connect(messageInput_, &QLineEdit::returnPressed, this, &MainWindow::sendCurrentMessage);

    messageInput_->setEnabled(false);
    sendButton_->setEnabled(false);
    acceptRequestButton_->setEnabled(false);

    return page;
}

QPushButton *MainWindow::createBlackButton(const QString &text, const QSize &minimumSize)
{
    auto *button = new QPushButton(text);
    button->setObjectName("BlackButton");
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumSize(minimumSize);
    return button;
}

QPushButton *MainWindow::createRoundButton(const QString &text, int sideLength)
{
    auto *button = new QPushButton(text);
    button->setObjectName("RoundButton");
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(sideLength, sideLength);
    return button;
}

QLineEdit *MainWindow::createPrimaryInput(const QString &placeholder, bool password)
{
    auto *input = new QLineEdit;
    input->setObjectName("PrimaryInput");
    input->setPlaceholderText(placeholder);
    input->setMinimumHeight(58);
    if (password) {
        input->setEchoMode(QLineEdit::Password);
    }
    return input;
}

void MainWindow::configureListWidget(QListWidget *listWidget, bool selectable)
{
    listWidget->setSelectionMode(selectable ? QAbstractItemView::SingleSelection
                                            : QAbstractItemView::NoSelection);
    listWidget->setFocusPolicy(Qt::NoFocus);
    listWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    listWidget->setWordWrap(true);
    listWidget->setTextElideMode(Qt::ElideNone);
    listWidget->setSpacing(8);
    listWidget->verticalScrollBar()->setSingleStep(12);
}

void MainWindow::applyTheme()
{
    setStyleSheet(QStringLiteral(
        "QWidget#AppRoot {"
        "  background-color: #7f7f7f;"
        "  color: #ffffff;"
        "}"
        "QDialog {"
        "  background-color: #7f7f7f;"
        "  color: #ffffff;"
        "}"
        "QLabel {"
        "  color: #ffffff;"
        "  font-size: 14px;"
        "}"
        "QLabel#SectionTitle {"
        "  font-size: 22px;"
        "  font-weight: 600;"
        "}"
        "QPushButton#BlackButton {"
        "  background-color: #111111;"
        "  border: 1px solid #202020;"
        "  border-radius: 24px;"
        "  color: #ffffff;"
        "  font-size: 16px;"
        "  padding: 12px 22px;"
        "}"
        "QPushButton#BlackButton:hover {"
        "  background-color: #1d1d1d;"
        "}"
        "QPushButton#BlackButton:pressed {"
        "  background-color: #090909;"
        "}"
        "QPushButton#BlackButton:disabled {"
        "  color: #9c9c9c;"
        "  background-color: #2a2a2a;"
        "}"
        "QPushButton#RoundButton {"
        "  background-color: #111111;"
        "  border: 1px solid #202020;"
        "  border-radius: 23px;"
        "  color: #ffffff;"
        "  font-size: 24px;"
        "  font-weight: 600;"
        "}"
        "QPushButton#RoundButton:hover {"
        "  background-color: #1d1d1d;"
        "}"
        "QPushButton#RoundButton:pressed {"
        "  background-color: #090909;"
        "}"
        "QLineEdit#PrimaryInput {"
        "  background-color: #111111;"
        "  border: 1px solid #202020;"
        "  border-radius: 24px;"
        "  color: #ffffff;"
        "  padding: 0 18px;"
        "  selection-background-color: #6e6e6e;"
        "  font-size: 15px;"
        "}"
        "QLineEdit#PrimaryInput::placeholder {"
        "  color: #c7c7c7;"
        "}"
        "QFrame#PanelFrame {"
        "  background-color: #696969;"
        "  border: 1px solid rgba(255, 255, 255, 0.12);"
        "  border-radius: %1px;"
        "}"
        "QFrame#SectionDivider {"
        "  background-color: rgba(255, 255, 255, 0.16);"
        "  max-height: 1px;"
        "  border: none;"
        "}"
        "QListWidget#FriendsList, QListWidget#MessagesList, QListWidget#RequestsList {"
        "  background: transparent;"
        "  border: none;"
        "  color: #ffffff;"
        "  outline: none;"
        "  font-size: 15px;"
        "}"
        "QListWidget#FriendsList::item, QListWidget#MessagesList::item, QListWidget#RequestsList::item {"
        "  background-color: rgba(0, 0, 0, 0.12);"
        "  border: 1px solid rgba(255, 255, 255, 0.06);"
        "  border-radius: 18px;"
        "  padding: 13px 14px;"
        "  margin: 3px 0;"
        "}"
        "QListWidget#FriendsList::item:selected, QListWidget#RequestsList::item:selected {"
        "  background-color: #111111;"
        "}"
        "QListWidget#FriendsList::item:hover, QListWidget#RequestsList::item:hover {"
        "  background-color: rgba(17, 17, 17, 0.72);"
        "}"
        "QScrollBar:vertical {"
        "  background: transparent;"
        "  width: 12px;"
        "  margin: 6px 0;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: rgba(17, 17, 17, 0.7);"
        "  border-radius: 6px;"
        "  min-height: 34px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "  background: rgba(17, 17, 17, 0.88);"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
        "  background: transparent;"
        "  border: none;"
        "}").arg(kPanelRadius));
}

void MainWindow::setFeedback(QLabel *label, const QString &message, const QString &color)
{
    if (!label) {
        return;
    }

    label->setStyleSheet(QStringLiteral("color: %1;").arg(color));
    label->setText(message);
}

void MainWindow::clearRegisterForm()
{
    registerNicknameEdit_->clear();
    registerEmailEdit_->clear();
    registerPasswordEdit_->clear();
    registerFeedbackLabel_->clear();
}

void MainWindow::clearLoginForm()
{
    loginPasswordEdit_->clear();
    loginFeedbackLabel_->clear();
}

void MainWindow::showWelcomePage()
{
    pages_->setCurrentWidget(welcomePage_);
    clearRegisterForm();
    clearLoginForm();
}

void MainWindow::showRegisterPage()
{
    registerFeedbackLabel_->clear();
    pages_->setCurrentWidget(registerPage_);
    registerNicknameEdit_->setFocus();
}

void MainWindow::showLoginPage()
{
    loginFeedbackLabel_->clear();
    pages_->setCurrentWidget(loginPage_);
    loginNicknameEdit_->setFocus();
}

void MainWindow::handleRegister()
{
    QString errorMessage;
    const bool registered = apiClient_->registerUser(
        registerNicknameEdit_->text(),
        registerEmailEdit_->text(),
        registerPasswordEdit_->text(),
        &errorMessage);

    if (!registered) {
        setFeedback(registerFeedbackLabel_, errorMessage, "#ffd8d8");
        return;
    }

    loginNicknameEdit_->setText(registerNicknameEdit_->text().trimmed());
    clearRegisterForm();
    setFeedback(loginFeedbackLabel_, "Account created. You can log in now.", "#e4ffde");
    pages_->setCurrentWidget(loginPage_);
    loginNicknameEdit_->setFocus();
}

void MainWindow::handleLogin()
{
    QString errorMessage;
    const auto user = apiClient_->authenticateUser(
        loginNicknameEdit_->text(),
        loginPasswordEdit_->text(),
        &errorMessage);

    if (!user.has_value()) {
        setFeedback(loginFeedbackLabel_, errorMessage, "#ffd8d8");
        return;
    }

    currentUserId_ = user->id;
    currentUserNickname_ = user->nickname;
    currentUserLabel_->setText(QStringLiteral("Signed in as\n%1").arg(currentUserNickname_));

    clearLoginForm();
    setFeedback(chatFeedbackLabel_, QString(), "#ffffff");
    pages_->setCurrentWidget(chatPage_);
    populateFriends();
    populateIncomingRequests();
    refreshServerStatus("Connected");
    refreshTimer_->start();
}

void MainWindow::handleFriendSelectionChanged()
{
    populateConversation(selectedFriendId());
}

void MainWindow::sendCurrentMessage()
{
    const qint64 friendId = selectedFriendId();
    if (currentUserId_ < 0 || friendId < 0) {
        setFeedback(chatFeedbackLabel_, "Pick a friend before sending.", "#ffd8d8");
        return;
    }

    QString errorMessage;
    const bool saved = apiClient_->storeMessage(
        friendId,
        messageInput_->text(),
        &errorMessage);

    if (!saved) {
        setFeedback(chatFeedbackLabel_, errorMessage, "#ffd8d8");
        return;
    }

    messageInput_->clear();
    setFeedback(chatFeedbackLabel_, QString(), "#ffffff");
    populateConversation(friendId);
}

void MainWindow::showAddFriendDialog()
{
    if (currentUserId_ < 0) {
        return;
    }

    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(true);
    dialog->setWindowTitle("Add Friend");
    dialog->setMinimumWidth(390);
    dialog->setStyleSheet(styleSheet());

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);

    auto *titleLabel = new QLabel("Send Friend Request", dialog);
    titleLabel->setObjectName("SectionTitle");
    titleLabel->setWordWrap(true);

    auto *nicknameInput = createPrimaryInput("Enter your friend's nickname");
    auto *sendRequestButton = createBlackButton("Send Request", QSize(280, 52));
    auto *closeButton = createBlackButton("Close", QSize(280, 52));
    auto *feedbackLabel = new QLabel(dialog);
    feedbackLabel->setWordWrap(true);
    feedbackLabel->setAlignment(Qt::AlignCenter);

    layout->addWidget(titleLabel);
    layout->addWidget(nicknameInput);
    layout->addWidget(sendRequestButton);
    layout->addWidget(closeButton);
    layout->addWidget(feedbackLabel);

    connect(sendRequestButton, &QPushButton::clicked, dialog, [this, dialog, nicknameInput, feedbackLabel]() {
        QString errorMessage;
        const bool sent = apiClient_->sendFriendRequest(nicknameInput->text(), &errorMessage);
        if (!sent) {
            setFeedback(feedbackLabel, errorMessage, "#ffd8d8");
            return;
        }

        setFeedback(chatFeedbackLabel_, "Friend request sent.", "#e4ffde");
        dialog->accept();
    });
    connect(closeButton, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(nicknameInput, &QLineEdit::returnPressed, sendRequestButton, &QPushButton::click);

    dialog->open();
    nicknameInput->setFocus();
}

void MainWindow::acceptSelectedFriendRequest()
{
    const qint64 requestId = selectedRequestId();
    if (requestId < 0) {
        setFeedback(chatFeedbackLabel_, "Pick a pending request first.", "#ffd8d8");
        return;
    }

    QString errorMessage;
    const bool accepted = apiClient_->acceptFriendRequest(requestId, &errorMessage);
    if (!accepted) {
        setFeedback(chatFeedbackLabel_, errorMessage, "#ffd8d8");
        return;
    }

    setFeedback(chatFeedbackLabel_, "Friend request accepted.", "#e4ffde");
    populateIncomingRequests();
    populateFriends();
}

void MainWindow::updateRequestActions()
{
    acceptRequestButton_->setEnabled(selectedRequestId() >= 0);
}

void MainWindow::refreshChatData()
{
    if (currentUserId_ < 0 || pages_->currentWidget() != chatPage_) {
        return;
    }

    const qint64 friendId = selectedFriendId();
    populateFriends();
    populateIncomingRequests();
    if (friendId >= 0) {
        populateConversation(friendId);
    }
}

void MainWindow::refreshServerStatus(const QString &detail)
{
    if (!serverStatusLabel_ || !apiClient_) {
        return;
    }

    QString text = QStringLiteral("Central server: %1").arg(apiClient_->endpointSummary());
    if (!detail.isEmpty()) {
        text.append(QStringLiteral(" | %1").arg(detail));
    }

    serverStatusLabel_->setText(text);
}

void MainWindow::logoutCurrentUser()
{
    refreshTimer_->stop();
    if (apiClient_) {
        apiClient_->clearSession();
    }
    currentUserId_ = -1;
    currentUserNickname_.clear();
    friends_.clear();
    incomingRequests_.clear();
    friendsList_->clear();
    incomingRequestsList_->clear();
    messagesList_->clear();
    activeConversationLabel_->setText("Pick a friend from the left panel.");
    messageInput_->clear();
    messageInput_->setEnabled(false);
    sendButton_->setEnabled(false);
    acceptRequestButton_->setEnabled(false);
    chatFeedbackLabel_->clear();
    refreshServerStatus();
    showWelcomePage();
}

void MainWindow::populateFriends()
{
    QString errorMessage;
    const qint64 previouslySelectedId = selectedFriendId();
    friends_ = apiClient_->friendsForCurrentUser(&errorMessage);

    QSignalBlocker blocker(friendsList_);
    friendsList_->clear();

    if (!errorMessage.isEmpty()) {
        setFeedback(chatFeedbackLabel_, errorMessage, "#ffd8d8");
    }

    if (friends_.isEmpty()) {
        auto *item = new QListWidgetItem("No friends yet. Send or accept a request.");
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        friendsList_->addItem(item);
        activeConversationLabel_->setText("No friends available yet.");
        messagesList_->clear();
        messageInput_->setEnabled(false);
        sendButton_->setEnabled(false);
        return;
    }

    int indexToSelect = 0;
    for (int index = 0; index < friends_.size(); ++index) {
        const UserRecord &friendRecord = friends_.at(index);
        auto *item = new QListWidgetItem(friendRecord.nickname);
        item->setData(Qt::UserRole, friendRecord.id);
        item->setToolTip(friendRecord.email);
        friendsList_->addItem(item);

        if (friendRecord.id == previouslySelectedId) {
            indexToSelect = index;
        }
    }

    friendsList_->setCurrentRow(indexToSelect);
    messageInput_->setEnabled(true);
    sendButton_->setEnabled(true);

    blocker.unblock();
    populateConversation(selectedFriendId());
}

void MainWindow::populateIncomingRequests()
{
    QString errorMessage;
    const qint64 previouslySelectedId = selectedRequestId();
    incomingRequests_ = apiClient_->incomingFriendRequests(&errorMessage);

    QSignalBlocker blocker(incomingRequestsList_);
    incomingRequestsList_->clear();

    if (!errorMessage.isEmpty()) {
        setFeedback(chatFeedbackLabel_, errorMessage, "#ffd8d8");
    }

    if (incomingRequests_.isEmpty()) {
        auto *item = new QListWidgetItem("No pending requests.");
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        incomingRequestsList_->addItem(item);
        acceptRequestButton_->setEnabled(false);
        return;
    }

    int indexToSelect = 0;
    for (int index = 0; index < incomingRequests_.size(); ++index) {
        const FriendRequestRecord &request = incomingRequests_.at(index);
        const QString timestamp = request.createdAt.isValid()
                                      ? request.createdAt.toLocalTime().toString("dd MMM HH:mm")
                                      : QStringLiteral("now");
        auto *item = new QListWidgetItem(
            QStringLiteral("%1 wants to be friends\nSent %2").arg(request.senderNickname, timestamp));
        item->setData(Qt::UserRole, request.id);
        item->setToolTip(request.senderEmail);
        incomingRequestsList_->addItem(item);

        if (request.id == previouslySelectedId) {
            indexToSelect = index;
        }
    }

    incomingRequestsList_->setCurrentRow(indexToSelect);
    blocker.unblock();
    updateRequestActions();
}

void MainWindow::populateConversation(qint64 friendId)
{
    messagesList_->clear();

    if (friendId < 0) {
        activeConversationLabel_->setText("Pick a friend from the left panel.");
        messageInput_->setEnabled(false);
        sendButton_->setEnabled(false);
        return;
    }

    QString errorMessage;
    const QVector<MessageRecord> messages =
        apiClient_->conversation(friendId, &errorMessage);

    if (!errorMessage.isEmpty()) {
        setFeedback(chatFeedbackLabel_, errorMessage, "#ffd8d8");
        return;
    }

    activeConversationLabel_->setText(QStringLiteral("Chat with %1").arg(friendNameFor(friendId)));

    if (messages.isEmpty()) {
        auto *item = new QListWidgetItem("No messages yet. Start the chat.");
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        messagesList_->addItem(item);
        return;
    }

    for (const MessageRecord &message : messages) {
        const bool isMine = message.senderId == currentUserId_;
        const QString author = isMine ? QStringLiteral("You") : friendNameFor(friendId);
        const QString timestamp = message.createdAt.isValid()
                                      ? message.createdAt.toLocalTime().toString("HH:mm")
                                      : QStringLiteral("--:--");
        const QString text = QStringLiteral("[%1] %2: %3").arg(timestamp, author, message.body);

        auto *item = new QListWidgetItem(text);
        item->setTextAlignment(isMine ? (Qt::AlignRight | Qt::AlignVCenter)
                                      : (Qt::AlignLeft | Qt::AlignVCenter));
        item->setForeground(isMine ? QColor("#d7f9ff") : QColor("#ffffff"));
        messagesList_->addItem(item);
    }

    messagesList_->scrollToBottom();
}

qint64 MainWindow::selectedFriendId() const
{
    const QListWidgetItem *item = friendsList_->currentItem();
    if (!item) {
        return -1;
    }

    const QVariant friendId = item->data(Qt::UserRole);
    if (!friendId.isValid()) {
        return -1;
    }

    return friendId.toLongLong();
}

qint64 MainWindow::selectedRequestId() const
{
    const QListWidgetItem *item = incomingRequestsList_->currentItem();
    if (!item) {
        return -1;
    }

    const QVariant requestId = item->data(Qt::UserRole);
    if (!requestId.isValid()) {
        return -1;
    }

    return requestId.toLongLong();
}

QString MainWindow::friendNameFor(qint64 friendId) const
{
    for (const UserRecord &friendRecord : friends_) {
        if (friendRecord.id == friendId) {
            return friendRecord.nickname;
        }
    }

    return QStringLiteral("Friend");
}
