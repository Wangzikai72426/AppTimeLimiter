#ifndef APPLIMITDIALOG_H
#define APPLIMITDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QCloseEvent>

class AppLimitDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AppLimitDialog(const QString& processName, QWidget* parent = nullptr);

    // Get the process name this dialog is about
    QString processName() const;

protected:
    void closeEvent(QCloseEvent* event) override;

signals:
    // Emitted when user clicks "延长使用时间" and password is correct
    void extendRequested(const QString& processName);

private slots:
    void onExtendClicked();
    void onGotItClicked();
    void onPasswordSubmitted();

private:
    QString m_processName;

    QLabel* m_iconLabel;
    QLabel* m_messageLabel;
    QPushButton* m_extendBtn;
    QPushButton* m_gotItBtn;

    // Password input area (hidden initially, shown when "延长使用时间" is clicked)
    QWidget* m_passwordArea;
    QLineEdit* m_passwordEdit;
    QPushButton* m_submitBtn;
    QLabel* m_passwordHintLabel;
};

#endif // APPLIMITDIALOG_H
