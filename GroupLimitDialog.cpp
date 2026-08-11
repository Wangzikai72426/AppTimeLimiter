#include "GroupLimitDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSettings>
#include <QMessageBox>
#include <QDebug>
#include <QTimer>

GroupLimitDialog::GroupLimitDialog(const QString& groupName, const QString& processName, QWidget* parent)
    : QDialog(parent)
    , m_groupName(groupName)
    , m_processName(processName)
{
    setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowCloseButtonHint);
    setWindowTitle("应用组限时提醒");
    setFixedSize(420, 280);
    setModal(true);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    // --- Icon + Message ---
    QHBoxLayout* msgLayout = new QHBoxLayout();

    m_iconLabel = new QLabel(this);
    m_iconLabel->setStyleSheet(
        "font-size: 48px; color: #FF5722; font-weight: bold;"
    );
    m_iconLabel->setText("!");
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setFixedWidth(60);

    m_messageLabel = new QLabel(this);
    m_messageLabel->setStyleSheet("font-size: 16px; color: #333;");
    m_messageLabel->setWordWrap(true);
    m_messageLabel->setText(
        QString("应用组 \"%1\" 的使用时间已用完！\n应用 \"%2\" 已被强制关闭。")
            .arg(groupName)
            .arg(processName)
    );
    m_messageLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    msgLayout->addWidget(m_iconLabel);
    msgLayout->addWidget(m_messageLabel, 1);
    mainLayout->addLayout(msgLayout);

    mainLayout->addSpacing(10);

    // --- Password area (hidden initially) ---
    m_passwordArea = new QWidget(this);
    QVBoxLayout* pwdLayout = new QVBoxLayout(m_passwordArea);
    pwdLayout->setSpacing(6);

    m_passwordHintLabel = new QLabel("请输入管理员密码以延长组使用时间:", m_passwordArea);
    m_passwordHintLabel->setStyleSheet("font-size: 13px; color: #555;");

    m_passwordEdit = new QLineEdit(m_passwordArea);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText("请输入密码");
    m_passwordEdit->setStyleSheet("font-size: 14px; padding: 4px;");

    m_submitBtn = new QPushButton("确认密码", m_passwordArea);
    m_submitBtn->setStyleSheet(
        "font-size: 14px; padding: 6px 16px; "
        "background-color: #4CAF50; color: white; "
        "border: none; border-radius: 4px;"
    );

    pwdLayout->addWidget(m_passwordHintLabel);
    pwdLayout->addWidget(m_passwordEdit);
    pwdLayout->addWidget(m_submitBtn, 0, Qt::AlignLeft);

    m_passwordArea->setVisible(false);
    mainLayout->addWidget(m_passwordArea);

    // --- Buttons ---
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_gotItBtn = new QPushButton("知道了", this);
    m_gotItBtn->setStyleSheet(
        "font-size: 14px; padding: 8px 24px; "
        "background-color: #9E9E9E; color: white; "
        "border: none; border-radius: 4px;"
    );
    m_gotItBtn->setFixedWidth(120);

    m_extendBtn = new QPushButton("延长使用时间", this);
    m_extendBtn->setStyleSheet(
        "font-size: 14px; padding: 8px 24px; "
        "background-color: #2196F3; color: white; "
        "border: none; border-radius: 4px;"
    );
    m_extendBtn->setFixedWidth(150);

    btnLayout->addWidget(m_gotItBtn);
    btnLayout->addWidget(m_extendBtn);
    mainLayout->addLayout(btnLayout);

    // Connect signals
    connect(m_gotItBtn, &QPushButton::clicked, this, &GroupLimitDialog::onGotItClicked);
    connect(m_extendBtn, &QPushButton::clicked, this, &GroupLimitDialog::onExtendClicked);
    connect(m_submitBtn, &QPushButton::clicked, this, &GroupLimitDialog::onPasswordSubmitted);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &GroupLimitDialog::onPasswordSubmitted);
}

QString GroupLimitDialog::groupName() const
{
    return m_groupName;
}

void GroupLimitDialog::onGotItClicked()
{
    qDebug() << "[GroupLimitDialog] User acknowledged for group" << m_groupName;
    reject();
}

void GroupLimitDialog::onExtendClicked()
{
    qDebug() << "[GroupLimitDialog] User requested extension for group" << m_groupName;
    m_passwordArea->setVisible(true);
    m_extendBtn->setVisible(false);
    m_gotItBtn->setVisible(false);
    m_passwordEdit->setFocus();
}

void GroupLimitDialog::onPasswordSubmitted()
{
    QString input = m_passwordEdit->text().trimmed();
    if (input.isEmpty()) return;

    QSettings settings("YourCompany", "AppTimeLimiter");
    QString correctPassword = settings.value("Password", "2026888").toString();

    if (input == correctPassword) {
        qDebug() << "[GroupLimitDialog] Password correct, extending group" << m_groupName;
        emit extendRequested(m_groupName);
        accept();
    } else {
        qDebug() << "[GroupLimitDialog] Wrong password";
        m_passwordEdit->clear();
        m_passwordHintLabel->setText("密码错误！请重试:");
        m_passwordHintLabel->setStyleSheet("font-size: 13px; color: red;");
        QTimer::singleShot(2000, [=]() {
            m_passwordHintLabel->setText("请输入管理员密码以延长组使用时间:");
            m_passwordHintLabel->setStyleSheet("font-size: 13px; color: #555;");
        });
    }
}

void GroupLimitDialog::closeEvent(QCloseEvent* event)
{
    event->accept();
}
