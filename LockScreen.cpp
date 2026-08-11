#include "LockScreen.h"
#include <QApplication>
#include <QScreen>
#include <QGuiApplication>
#include <QVBoxLayout>
#include <QSettings>
#include <QDebug>
#include <Windows.h>

#pragma comment(lib, "User32.lib")

LockScreen::LockScreen(QWidget* parent)
    : QWidget(parent)
{
    initWindow();

    m_topmostTimer = new QTimer(this);
    connect(m_topmostTimer, &QTimer::timeout, [=]() {
        SetWindowPos((HWND)this->winId(), HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    });
}

void LockScreen::initWindow()
{
    setWindowFlags(Qt::FramelessWindowHint);
    setStyleSheet("background-color: black;");
    setWindowOpacity(0.75);
    setFocusPolicy(Qt::StrongFocus);

    QRect rect = QGuiApplication::primaryScreen()->geometry();
    setGeometry(rect);
    setFixedSize(rect.size());

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setAlignment(Qt::AlignCenter);

    // Title
    m_titleLabel = new QLabel("屏幕已锁定", this);
    m_titleLabel->setStyleSheet("color: white; font-size: 36px; font-weight: bold;");
    mainLayout->addWidget(m_titleLabel, 0, Qt::AlignCenter);

    // Reason
    m_reasonLabel = new QLabel("", this);
    m_reasonLabel->setStyleSheet("color: #FFA500; font-size: 16px;");
    m_reasonLabel->setAlignment(Qt::AlignCenter);
    m_reasonLabel->setFixedWidth(400);
    m_reasonLabel->setWordWrap(true);
    mainLayout->addWidget(m_reasonLabel, 0, Qt::AlignCenter);

    // Password input
    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setPlaceholderText("请输入解锁密码");
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setStyleSheet("font-size: 18px; padding: 5px; background-color: white;");
    m_passwordEdit->setFixedWidth(250);
    mainLayout->addWidget(m_passwordEdit, 0, Qt::AlignCenter);

    // Unlock button
    m_unlockBtn = new QPushButton("解锁", this);
    m_unlockBtn->setStyleSheet(
        "font-size: 18px; padding: 8px 20px; "
        "background-color: #4CAF50; color: white; "
        "border: none; border-radius: 4px;"
    );
    m_unlockBtn->setFixedWidth(150);
    mainLayout->addWidget(m_unlockBtn, 0, Qt::AlignCenter);

    // Connect signals
    connect(m_unlockBtn, &QPushButton::clicked, this, &LockScreen::onPasswordEntered);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &LockScreen::onPasswordEntered);
}

void LockScreen::lockScreen(const QString& reason)
{
    if (!reason.isEmpty()) {
        m_reasonLabel->setText(reason);
    } else {
        m_reasonLabel->setText("使用时间已达限制");
    }
    m_passwordEdit->clear();
    show();
    raise();
    activateWindow();
    setFocus();
}

void LockScreen::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_topmostTimer && !m_topmostTimer->isActive()) {
        m_topmostTimer->start(1000);
        qDebug() << "[LockScreen] Locked - topmost timer started";
    }
    // Focus the password edit
    m_passwordEdit->setFocus();
}

void LockScreen::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (m_topmostTimer && m_topmostTimer->isActive()) {
        m_topmostTimer->stop();
        qDebug() << "[LockScreen] Unlocked - topmost timer stopped";
    }
}

void LockScreen::closeEvent(QCloseEvent* event)
{
    // Prevent closing via Alt+F4 or task manager "close window"
    event->ignore();
}

void LockScreen::onPasswordEntered()
{
    QString input = m_passwordEdit->text().trimmed();
    if (input.isEmpty()) return;

    QSettings settings("YourCompany", "AppTimeLimiter");
    QString correctPassword = settings.value("Password", "2026888").toString();

    if (input == correctPassword) {
        qDebug() << "[LockScreen] Password correct, unlocking";
        m_passwordEdit->clear();
        hide();
        emit unlocked();
    } else {
        qDebug() << "[LockScreen] Wrong password";
        m_passwordEdit->clear();
        m_passwordEdit->setPlaceholderText("密码错误，请重试");
        QTimer::singleShot(1500, [=]() {
            m_passwordEdit->setPlaceholderText("请输入解锁密码");
        });
    }
}
