#ifndef LOCKSCREEN_H
#define LOCKSCREEN_H

#include <QWidget>
#include <QTimer>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QCloseEvent>
#include <QShowEvent>
#include <QHideEvent>

class LockScreen : public QWidget
{
    Q_OBJECT

public:
    explicit LockScreen(QWidget* parent = nullptr);

    // Show the lock screen with an optional reason message
    void lockScreen(const QString& reason = QString());

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

signals:
    // Emitted when the user enters the correct password
    void unlocked();

private slots:
    void onPasswordEntered();

private:
    void initWindow();

    QLabel* m_titleLabel;
    QLabel* m_reasonLabel;
    QLineEdit* m_passwordEdit;
    QPushButton* m_unlockBtn;
    QTimer* m_topmostTimer;
};

#endif // LOCKSCREEN_H
