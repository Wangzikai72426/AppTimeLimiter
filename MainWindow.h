#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QCloseEvent>
#include <QList>
#include "AppMonitor.h"
#include "LockScreen.h"
#include "AppLimitDialog.h"
#include "GroupLimitDialog.h"
#include "HistoryDialog.h"

class QPropertyAnimation;
class QTimer;
class QWidget;
class ToastWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    //QStringList getHiddenApps() const;

    //void setHiddenApps(const QStringList& list);

    //void addHiddenApp(const QString& appName);

    //void removeHiddenApp(const QString& appName);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onAppUsageUpdated(const QString& processName, int usedSeconds);
    void onTotalUsageUpdated(int usedSeconds);
    void onAppLimitReached(const QString& processName);
    void onTotalLimitReached();
    void onLockScreenUnlocked();
    void onAppLimitExtendRequested(const QString& processName);
    void onSettingsClicked();
    void onAddLimitClicked();
    void onRemoveLimitClicked();
    void onRefreshClicked();
    void onHistoryClicked();
    void onUsageTableCustomContextMenu(const QPoint& pos);   //      Ҽ  ˵ 
    void onHideApp();     //     ѡ е Ӧ  
    void onAddToLimited(); //   ӵ   ʱӦ  
    void showHiddenAppsManager();   //   ʾ        Ӧ õĶԻ   

    // 应用分组相关 slots
    void onToggleExpand();
    void onCreateGroup();
    void onDeleteGroup();
    void onSetGroupLimit();
    void onAddToGroup();
    void onRemoveFromGroup();
    void onGroupLimitReached(const QString& groupName, const QString& processName);
    void onGroupLimitExtendRequested(const QString& groupName);
    void onAppOpened(const QString& processName);

private:
    void initUI();
    void initTray();
    void refreshUsageTable();
    void refreshLimitTable();
    void refreshGroupTable();
    void updateStatusLabels();
    bool verifyAdminPassword();

    // 动画 / 提示相关
    void showToast(const QString& title, const QString& message);
    void repositionToasts();
    static QString formatDuration(int secs);
    void startTrayBlink();
    void stopTrayBlink();
    void onTrayBlinkTimeout();
    QIcon createWarningIcon();
    void setHiddenApps(const QStringList& list);
    void addHiddenApp(const QString& appName);
    void removeHiddenApp(const QString& appName);


    QStringList getHiddenApps() const;
    AppMonitor* m_monitor;
    LockScreen* m_lockScreen;
    AppLimitDialog* m_appLimitDialog;
    GroupLimitDialog* m_groupLimitDialog;

    // System tray
    QSystemTrayIcon* m_trayIcon;
    QMenu* m_trayMenu;

    // UI elements
    QWidget* m_centralWidget;
    QTableWidget* m_usageTable;
    QTableWidget* m_limitTable;
    QLabel* m_totalTimeLabel;
    QLabel* m_totalRemainingLabel;
    QPushButton* m_addLimitBtn;
    QPushButton* m_removeLimitBtn;
    QPushButton* m_refreshBtn;
    QPushButton* m_settingsBtn;
    QPushButton* m_historyBtn;

    // 应用分组 UI
    QWidget* m_groupPanel;
    QTableWidget* m_groupTable;
    QPushButton* m_createGroupBtn;
    QPushButton* m_deleteGroupBtn;
    QPushButton* m_setGroupLimitBtn;
    QPushButton* m_addToGroupBtn;
    QPushButton* m_removeFromGroupBtn;
    QPushButton* m_toggleExpandBtn;
    bool m_expanded;

    // 折叠面板展开动画
    QPropertyAnimation* m_expandAnim;

    // 托盘闪烁
    QTimer* m_trayBlinkTimer;
    QIcon m_trayIconNormal;
    QIcon m_trayIconWarning;
    bool m_trayBlinkState;

    // 自定义 Toast 堆叠列表
    QList<ToastWidget*> m_activeToasts;

    bool m_isLocked;
    QString m_lastLimitedApp;
    QString m_contextAppName;   //  Ҽ    ʱ  ¼  Ӧ         ڲ˵       
};

#endif // MAINWINDOW_H
