#ifndef APPMONITOR_H
#define APPMONITOR_H

#include <QObject>
#include <QTimer>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QDate>

class AppMonitor : public QObject
{
    Q_OBJECT

public:
    explicit AppMonitor(QObject* parent = nullptr);
    ~AppMonitor();

    void start();
    void stop();
    void setMonitoringPaused(bool paused);

    // Get all tracked app usage (process name -> seconds used today)
    QMap<QString, int> getAllUsage() const;

    // Get total computer usage time today (seconds)
    int getTotalUsedSeconds() const;

    // Get limited apps list
    QStringList getLimitedApps() const;

    // Get limit for a specific app (minutes), 0 = not limited
    int getAppLimitMinutes(const QString& processName) const;

    // Get used seconds for a limited app
    int getAppUsedSeconds(const QString& processName) const;

    // Add/remove limited app
    void addLimitedApp(const QString& processName, int limitMinutes);
    void removeLimitedApp(const QString& processName);

    // Get/set total limit (minutes), 0 = unlimited
    int getTotalLimitMinutes() const;
    void setTotalLimitMinutes(int minutes);

    // Called when lock screen is unlocked (prevents re-triggering same limit)
    void onLockScreenUnlocked();

    // Forcefully terminate a process by name (e.g. "chrome.exe")
    bool killProcess(const QString& processName);

    // Clear the override flag for a specific app (allows re-triggering after extension)
    void clearAppLimitOverride(const QString& processName);

    // Reset the usage counter for a specific app (used when extending time)
    void resetAppUsage(const QString& processName);

    // Reset daily usage
    void resetDailyUsage();

    // get total shown apps usage
    int getShownTotalUsedSeconds() const;

    // ---------- 应用分组 ----------
    // 组管理
    QStringList getGroups() const;
    QStringList getGroupApps(const QString& groupName) const;
    int getGroupLimitMinutes(const QString& groupName) const;
    int getGroupUsedSeconds(const QString& groupName) const;
    void addGroup(const QString& groupName);
    void removeGroup(const QString& groupName);
    void setGroupLimit(const QString& groupName, int limitMinutes);

    // 应用-组关系
    void addAppToGroup(const QString& processName, const QString& groupName);
    void removeAppFromGroup(const QString& processName);
    QString getAppGroup(const QString& processName) const;

    // 组限时延长
    void clearGroupLimitOverride(const QString& groupName);
    void extendGroupLimit(const QString& groupName);

    // Save/load usage data
    void saveUsage();
    void loadUsage();

    // ---------- 历史记录接口 ----------
    // 所有有记录的日期(yyyy-MM-dd)，降序排列
    QStringList getHistoryDates() const;
    // 某天的进程名->使用秒数
    QMap<QString, int> getHistoryByDate(const QString& date) const;
    // 清除除今天外所有历史记录
    void clearHistoryExceptToday();
    // 清除早于 cutoff 日期的历史记录
    void clearHistoryBeforeDate(const QDate& cutoff);
    // 读取 HistoryCleanupDate 设置并自动清理（仅当设置了有效日期）
    void autoCleanHistory();

    // ----------            Ѻ   Ϣ ----------
    //   ȡ   ̵     ·         "C:\Program Files\Google\Chrome\Application\chrome.exe"  
    QString getProcessPath(const QString& processName) const;

    //   ȡ   ̵  Ѻ    ƣ      "chrome.exe"    "Google Chrome"  
    QString getFriendlyAppName(const QString& processName) const;

    //   ȡ   ̵ ͼ  
    QIcon getAppIcon(const QString& processName) const;

signals:
    void appUsageUpdated(const QString& processName, int usedSeconds);
    void totalUsageUpdated(int usedSeconds);
    void appLimitReached(const QString& processName);
    void totalLimitReached();
    void groupLimitReached(const QString& groupName, const QString& processName);
    void appOpened(const QString& processName);

private slots:
    void onPoll();

private:
    QTimer* m_pollTimer;
    QString m_lastForegroundProcess;

    // Usage tracking: process name -> seconds used today
    QMap<QString, int> m_appUsage;

    // Total usage today (seconds)
    int m_totalUsedSeconds;

    // Date string for daily reset
    QString m_currentDate;

    // Pause flag (when lock screen is active)
    bool m_paused;

    // Override flags: prevent re-triggering same limit after unlock
    bool m_totalLimitOverridden;
    QStringList m_appLimitOverridden;
    QStringList m_groupLimitOverridden;  // 已触发组限时的组名列表

    // Check and perform daily reset if needed
    void checkDailyReset();

    // 从持久化存储中加载进程路径（如果存在）
    QString loadProcessPathFromSettings(const QString& processName) const;

    // 将进程路径保存到持久化存储
    void saveProcessPathToSettings(const QString& processName, const QString& path);

    // Get foreground window process name
    QString getForegroundProcessName();

    mutable QMap<QString, QString> m_nameCache;   //            Ѻ     
    mutable QMap<QString, QIcon>  m_iconCache;    //           ͼ  
    mutable QMap<QString, QString> m_pathCache;   //               ·  
};

#endif // APPMONITOR_H
