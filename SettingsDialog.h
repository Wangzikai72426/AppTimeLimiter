#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QCheckBox>
#include <QDateEdit>
#include <QDate>

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private slots:
    void onConfirm();
    void onCancel();
    void uninstall();
    void onAutoStartToggled(bool checked);   // 开机自启动（Windows 服务）开关
    void onClearHistory();                   // 清除除今天外所有历史记录
    void onSaveAutoClean();                  // 保存自动清理日期设置

signals:
    void showHiddenAppsRequested();   // 点击“管理隐藏应用”按钮时发射
    void clearHistoryRequested();     // 请求清除除今天外所有历史记录
    void autoCleanDateApplied(const QDate& cutoff);  // 已保存自动清理日期，立即清理 older 记录

private:
    void loadSettings();
    void saveSettings();
    bool createShortcutForTask(const QString& taskName, const QString& shortcutPath);
    bool createScheduledTask(const QString& taskName, const QString& appPath, bool isAutoStart);
    void onCreateShortcutClicked();

    // 自启动（Windows 服务）相关
    bool isServiceInstalled();          // 查询服务是否已安装
    bool installAutoStartService();     // 安装并启动服务
    bool uninstallAutoStartService();   // 停止并删除服务
    bool isProcessElevated();           // 检查当前进程是否已提权

    QSpinBox* m_totalLimitSpin;
    QLineEdit* m_passwordEdit;
    QPushButton* m_uninstall;
    QCheckBox* isAutoStart;
    QCheckBox* isInTask;

    // 历史记录管理
    QDateEdit* m_autoCleanDateEdit;
    QPushButton* m_clearHistoryBtn;
    QPushButton* m_applyCleanBtn;
};

#endif // SETTINGSDIALOG_H
