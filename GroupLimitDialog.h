#ifndef GROUPLIMITDIALOG_H
#define GROUPLIMITDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QCloseEvent>

class GroupLimitDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GroupLimitDialog(const QString& groupName, const QString& processName, QWidget* parent = nullptr);

    QString groupName() const;

protected:
    void closeEvent(QCloseEvent* event) override;

signals:
    void extendRequested(const QString& groupName);

private slots:
    void onExtendClicked();
    void onGotItClicked();
    void onPasswordSubmitted();

private:
    QString m_groupName;
    QString m_processName;

    QLabel* m_iconLabel;
    QLabel* m_messageLabel;
    QPushButton* m_extendBtn;
    QPushButton* m_gotItBtn;

    QWidget* m_passwordArea;
    QLineEdit* m_passwordEdit;
    QPushButton* m_submitBtn;
    QLabel* m_passwordHintLabel;
};

#endif // GROUPLIMITDIALOG_H
