#include "AppMonitor.h"
#include <QSettings>
#include <QDebug>
#include <QFileInfo>
#include <Windows.h>
#include <TlHelp32.h>
#include <QFileIconProvider>   // 需要添加到顶部
#include <QApplication>        // 需要添加到顶部
#include <QStyle>              // 需要添加到顶部

#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Kernel32.lib")

AppMonitor::AppMonitor(QObject* parent)
    : QObject(parent)
    , m_totalUsedSeconds(0)
    , m_paused(false)
    , m_totalLimitOverridden(false)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(1000); // 1 second
    connect(m_pollTimer, &QTimer::timeout, this, &AppMonitor::onPoll);

    m_currentDate = QDate::currentDate().toString("yyyy-MM-dd");
    loadUsage();
    autoCleanHistory();   // 启动时按设置自动清理过期历史
}

AppMonitor::~AppMonitor()
{
    saveUsage();
}

QString AppMonitor::loadProcessPathFromSettings(const QString& processName) const
{
    QSettings settings("YourCompany", "AppTimeLimiter");
    QString key = "ProcessPath_" + processName;
    return settings.value(key).toString();
}

void AppMonitor::saveProcessPathToSettings(const QString& processName, const QString& path)
{
    QSettings settings("YourCompany", "AppTimeLimiter");
    QString key = "ProcessPath_" + processName;
    settings.setValue(key, path);
}

QString AppMonitor::getProcessPath(const QString& processName) const
{
    // 1. 先查内存缓存
    if (m_pathCache.contains(processName))
        return m_pathCache[processName];

    // 2. 再查持久化存储
    QString savedPath = loadProcessPathFromSettings(processName);
    if (!savedPath.isEmpty() && QFile::exists(savedPath)) {
        // 如果路径有效，存入内存缓存并返回
        m_pathCache[processName] = savedPath;
        return savedPath;
    }

    // 3. 最后从当前运行的进程中查找（这是最后的办法）
    QString fullPath;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(hSnapshot, &pe32)) {
            do {
                QString exeName = QString::fromWCharArray(pe32.szExeFile);
                if (exeName.compare(processName, Qt::CaseInsensitive) == 0) {
                    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe32.th32ProcessID);
                    if (hProcess) {
                        WCHAR path[MAX_PATH];
                        DWORD size = MAX_PATH;
                        if (QueryFullProcessImageNameW(hProcess, 0, path, &size)) {
                            fullPath = QString::fromWCharArray(path);
                        }
                        CloseHandle(hProcess);
                        break;
                    }
                }
            } while (Process32NextW(hSnapshot, &pe32));
        }
        CloseHandle(hSnapshot);
    }

    // 如果找到了，则保存到持久化存储和内存缓存
    if (!fullPath.isEmpty()) {
        m_pathCache[processName] = fullPath;

        QSettings settings("YourCompany", "AppTimeLimiter");
        QString key = "ProcessPath_" + processName;
        settings.setValue(key, fullPath);

        //saveProcessPathToSettings(processName, fullPath);
    }

    return fullPath;
}

QString AppMonitor::getFriendlyAppName(const QString& processName) const
{
    // 先查缓存
    if (m_nameCache.contains(processName))
        return m_nameCache[processName];

    QString friendlyName = processName;   // 默认回退值

    QString exePath = getProcessPath(processName);
    if (!exePath.isEmpty()) {
        // 读取文件的版本信息中的 "FileDescription"
        DWORD handle = 0;
        DWORD size = GetFileVersionInfoSizeW(exePath.toStdWString().c_str(), &handle);
        if (size > 0) {
            std::vector<BYTE> buffer(size);
            if (GetFileVersionInfoW(exePath.toStdWString().c_str(), 0, size, buffer.data())) {
                // 获取语言代码页
                struct LANGANDCODEPAGE {
                    WORD wLanguage;
                    WORD wCodePage;
                } *lpTranslate = nullptr;
                UINT cbTranslate = 0;
                if (VerQueryValueW(buffer.data(), L"\\VarFileInfo\\Translation",
                    (LPVOID*)&lpTranslate, &cbTranslate)) {
                    if (cbTranslate >= sizeof(struct LANGANDCODEPAGE)) {
                        // 取第一个语言版本
                        wchar_t subBlock[50];
                        wsprintfW(subBlock, L"\\StringFileInfo\\%04x%04x\\FileDescription",
                            lpTranslate[0].wLanguage, lpTranslate[0].wCodePage);
                        wchar_t* desc = nullptr;
                        UINT descLen = 0;
                        if (VerQueryValueW(buffer.data(), subBlock, (LPVOID*)&desc, &descLen)) {
                            if (desc && descLen > 0) {
                                friendlyName = QString::fromWCharArray(desc, descLen - 1).simplified();
                            }
                        }
                    }
                }
            }
        }
    }

    // 如果读取失败，去掉 .exe 后缀作为显示名
    if (friendlyName == processName) {
        friendlyName = QFileInfo(processName).baseName();  // "chrome.exe" → "chrome"
    }

    m_nameCache[processName] = friendlyName;
    return friendlyName;
}

QIcon AppMonitor::getAppIcon(const QString& processName) const
{
    // 先查缓存
    if (m_iconCache.contains(processName))
        return m_iconCache[processName];

    QIcon icon;

    QString exePath = getProcessPath(processName);
    if (!exePath.isEmpty()) {
        QFileIconProvider provider;
        QFileInfo fileInfo(exePath);
        icon = provider.icon(fileInfo);
        if (!icon.isNull()) {
            // 统一缩放为 32x32，让表格显示整齐
            icon = icon.pixmap(32, 32);
        }
    }

    // 如果还是拿不到，用默认的应用图标
    if (icon.isNull()) {
        icon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);
    }

    m_iconCache[processName] = icon;
    return icon;
}

void AppMonitor::start()
{
    m_pollTimer->start();
    qDebug() << "[AppMonitor] Monitoring started";
}

void AppMonitor::stop()
{
    m_pollTimer->stop();
    saveUsage();
    qDebug() << "[AppMonitor] Monitoring stopped";
}

void AppMonitor::setMonitoringPaused(bool paused)
{
    m_paused = paused;
    if (paused) {
        qDebug() << "[AppMonitor] Monitoring paused (lock screen active)";
    } else {
        qDebug() << "[AppMonitor] Monitoring resumed";
    }
}

void AppMonitor::onPoll()
{
    if (m_paused) return;

    checkDailyReset();

    QString currentProcess = getForegroundProcessName();
    if (currentProcess.isEmpty()) return;

    // If process changed, log it and notify the UI that a (new) app is now active
    if (m_lastForegroundProcess != currentProcess) {
        qDebug() << "[AppMonitor] Foreground changed:" << currentProcess;
        m_lastForegroundProcess = currentProcess;
        emit appOpened(currentProcess);
    }

    // Check limits
    QSettings settings("YourCompany", "AppTimeLimiter");
    QStringList limitedApps = settings.value("LimitedApps").toStringList();

    // *** Priority check: if foreground app has already exceeded its limit today,
    //     kill it immediately and skip all further processing (no usage accumulation) ***
    if (m_appLimitOverridden.contains(currentProcess) && limitedApps.contains(currentProcess)) {
        int appLimit = settings.value("Limit_" + currentProcess, 0).toInt();
        if (appLimit > 0 && m_appUsage.value(currentProcess, 0) >= appLimit * 60) {
            qDebug() << "[AppMonitor] Re-killing blocked app on foreground detection:" << currentProcess;
            killProcess(currentProcess);
            return; // Skip usage accumulation and all other checks
        }
    }

    // *** Priority check: if foreground app's group has exceeded its limit, kill it ***
    QString groupName = getAppGroup(currentProcess);
    if (!groupName.isEmpty() && m_groupLimitOverridden.contains(groupName)) {
        int groupLimit = getGroupLimitMinutes(groupName);
        if (groupLimit > 0 && getGroupUsedSeconds(groupName) >= groupLimit * 60) {
            qDebug() << "[AppMonitor] Re-killing app blocked by group limit:" << currentProcess << "group:" << groupName;
            killProcess(currentProcess);
            return;
        }
    }

    // Accumulate usage for this app (only if not blocked)
    m_appUsage[currentProcess] = m_appUsage.value(currentProcess, 0) + 1;
    m_totalUsedSeconds++;

    // Emit signals
    emit appUsageUpdated(currentProcess, m_appUsage[currentProcess]);
    emit totalUsageUpdated(m_totalUsedSeconds);

    // 1. Check total limit (only if not already overridden today)
    int totalLimit = settings.value("TotalLimit", 0).toInt(); // 0 = unlimited
    if (!m_totalLimitOverridden && totalLimit > 0 && m_totalUsedSeconds >= totalLimit * 60) {
        m_totalLimitOverridden = true;
        emit totalLimitReached();
        return;
    }

    // 2. Check per-app limit (first time reaching limit: emit signal to show dialog)
    if (limitedApps.contains(currentProcess)) {
        int appLimit = settings.value("Limit_" + currentProcess, 0).toInt();
        if (appLimit > 0 && m_appUsage[currentProcess] >= appLimit * 60) {
            if (!m_appLimitOverridden.contains(currentProcess)) {
                // First time reaching limit: emit signal to show dialog + kill process
                m_appLimitOverridden.append(currentProcess);
                emit appLimitReached(currentProcess);
            }
        }
    }

    // 3. Check group limit (if app belongs to a group with a limit)
    if (!groupName.isEmpty()) {
        int groupLimit = getGroupLimitMinutes(groupName);
        if (groupLimit > 0 && !m_groupLimitOverridden.contains(groupName)) {
            int groupUsed = getGroupUsedSeconds(groupName);
            if (groupUsed >= groupLimit * 60) {
                m_groupLimitOverridden.append(groupName);
                emit groupLimitReached(groupName, currentProcess);
                return;
            }
        }
    }

    // Save every 30 seconds
    if (m_totalUsedSeconds % 30 == 0) {
        saveUsage();
    }
}

void AppMonitor::checkDailyReset()
{
    QString today = QDate::currentDate().toString("yyyy-MM-dd");
    if (today != m_currentDate) {
        qDebug() << "[AppMonitor] Date changed, resetting daily usage";
        resetDailyUsage();
        m_currentDate = today;
        autoCleanHistory();   // 日期切换时按设置自动清理过期历史
    }
}

QString AppMonitor::getForegroundProcessName()
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return QString();

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return QString();

    // Try with limited information first (works for most processes)
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) {
        hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    }
    if (!hProcess) return QString();

    WCHAR buffer[MAX_PATH] = { 0 };
    DWORD size = MAX_PATH;
    BOOL ok = QueryFullProcessImageNameW(hProcess, 0, buffer, &size);
    CloseHandle(hProcess);

    if (!ok) return QString();

    QString fullPath = QString::fromWCharArray(buffer);
    QFileInfo fi(fullPath);
    return fi.fileName();
}

void AppMonitor::onLockScreenUnlocked()
{
    // Resume monitoring but keep override flags
    // so the same limit doesn't immediately re-trigger
    setMonitoringPaused(false);
}

bool AppMonitor::killProcess(const QString& processName)
{
    qDebug() << "[AppMonitor] Attempting to kill process:" << processName;

    bool killed = false;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        qDebug() << "[AppMonitor] Failed to create process snapshot";
        return false;
    }

    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(hSnapshot, &pe32)) {
        do {
            QString exeName = QString::fromWCharArray(pe32.szExeFile);
            if (exeName.compare(processName, Qt::CaseInsensitive) == 0) {
                HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe32.th32ProcessID);
                if (hProcess) {
                    if (TerminateProcess(hProcess, 0)) {
                        qDebug() << "[AppMonitor] Successfully terminated PID" << pe32.th32ProcessID << processName;
                        killed = true;
                    } else {
                        qDebug() << "[AppMonitor] TerminateProcess failed for PID" << pe32.th32ProcessID;
                    }
                    CloseHandle(hProcess);
                } else {
                    qDebug() << "[AppMonitor] OpenProcess(PROCESS_TERMINATE) failed for PID" << pe32.th32ProcessID;
                }
            }
        } while (Process32NextW(hSnapshot, &pe32));
    }

    CloseHandle(hSnapshot);

    if (!killed) {
        qDebug() << "[AppMonitor] No matching process found to kill:" << processName;
    }
    return killed;
}

void AppMonitor::clearAppLimitOverride(const QString& processName)
{
    qDebug() << "[AppMonitor] Clearing override flag for" << processName;
    m_appLimitOverridden.removeAll(processName);
}

void AppMonitor::resetAppUsage(const QString& processName)
{
    qDebug() << "[AppMonitor] Resetting usage counter for" << processName;
    m_appUsage.remove(processName);
    saveUsage();
}

void AppMonitor::resetDailyUsage()
{
    m_appUsage.clear();
    m_totalUsedSeconds = 0;
    m_totalLimitOverridden = false;
    m_appLimitOverridden.clear();
    m_groupLimitOverridden.clear();

    // 清空所有组的偏移量（每日重置）
    QSettings settings("YourCompany", "AppTimeLimiter");
    QStringList groups = settings.value("AppGroups").toStringList();
    for (const QString& group : groups) {
        settings.remove("GroupOffset_" + group);
    }

    saveUsage();
}

void AppMonitor::saveUsage()
{
    QSettings settings("YourCompany", "AppTimeLimiter");
    settings.setValue("UsageDate", m_currentDate);
    settings.setValue("TotalUsedSeconds", m_totalUsedSeconds);

    // Save app usage as individual keys in a group
    settings.beginGroup("AppUsage_" + m_currentDate);
    settings.remove(""); // Clear old entries
    for (auto it = m_appUsage.begin(); it != m_appUsage.end(); ++it) {
        settings.setValue(it.key(), it.value());
    }
    settings.endGroup();
}

void AppMonitor::loadUsage()
{
    QSettings settings("YourCompany", "AppTimeLimiter");
    QString savedDate = settings.value("UsageDate").toString();

    if (savedDate == m_currentDate) {
        // Same day, load saved usage
        m_totalUsedSeconds = settings.value("TotalUsedSeconds", 0).toInt();

        settings.beginGroup("AppUsage_" + m_currentDate);
        QStringList keys = settings.childKeys();
        for (const QString& key : keys) {
            m_appUsage[key] = settings.value(key).toInt();
        }
        settings.endGroup();

        qDebug() << "[AppMonitor] Loaded usage for" << m_currentDate
                 << "Total:" << m_totalUsedSeconds << "seconds,"
                 << m_appUsage.size() << "apps tracked";
    } else {
        // Different day, reset
        qDebug() << "[AppMonitor] New day, usage reset";
        m_totalUsedSeconds = 0;
    }
}

QMap<QString, int> AppMonitor::getAllUsage() const
{
    return m_appUsage;
}

int AppMonitor::getTotalUsedSeconds() const
{
    return m_totalUsedSeconds;
}

QStringList AppMonitor::getLimitedApps() const
{
    QSettings settings("YourCompany", "AppTimeLimiter");
    return settings.value("LimitedApps").toStringList();
}

int AppMonitor::getAppLimitMinutes(const QString& processName) const
{
    QSettings settings("YourCompany", "AppTimeLimiter");
    return settings.value("Limit_" + processName, 0).toInt();
}

int AppMonitor::getAppUsedSeconds(const QString& processName) const
{
    return m_appUsage.value(processName, 0);
}

void AppMonitor::addLimitedApp(const QString& processName, int limitMinutes)
{
    QSettings settings("YourCompany", "AppTimeLimiter");
    QStringList limitedApps = settings.value("LimitedApps").toStringList();
    if (!limitedApps.contains(processName)) {
        limitedApps.append(processName);
        settings.setValue("LimitedApps", limitedApps);
    }
    settings.setValue("Limit_" + processName, limitMinutes);
}

void AppMonitor::removeLimitedApp(const QString& processName)
{
    QSettings settings("YourCompany", "AppTimeLimiter");
    QStringList limitedApps = settings.value("LimitedApps").toStringList();
    limitedApps.removeAll(processName);
    settings.setValue("LimitedApps", limitedApps);
    settings.remove("Limit_" + processName);
}

int AppMonitor::getTotalLimitMinutes() const
{
    QSettings settings("YourCompany", "AppTimeLimiter");
    return settings.value("TotalLimit", 0).toInt();
}

void AppMonitor::setTotalLimitMinutes(int minutes)
{
    QSettings settings("YourCompany", "AppTimeLimiter");
    settings.setValue("TotalLimit", minutes);
}

int AppMonitor::getShownTotalUsedSeconds() const
{
    // 读取当前隐藏列表
    QSettings settings("YourCompany", "AppTimeLimiter");
    QStringList hiddenApps = settings.value("HiddenApps").toStringList();

    int total = 0;
    // 遍历所有应用的使用时间
    for (auto it = m_appUsage.begin(); it != m_appUsage.end(); ++it) {
        // 如果当前应用不在隐藏列表中，则累加其时间
        if (!hiddenApps.contains(it.key(), Qt::CaseInsensitive)) {
            total += it.value();
        }
    }
    return total;
}

// ==================== 应用分组 ====================

QStringList AppMonitor::getGroups() const
{
    QSettings settings("YourCompany", "AppTimeLimiter");
    return settings.value("AppGroups").toStringList();
}

QStringList AppMonitor::getGroupApps(const QString& groupName) const
{
    QSettings settings("YourCompany", "AppTimeLimiter");
    return settings.value("GroupApps_" + groupName).toStringList();
}

int AppMonitor::getGroupLimitMinutes(const QString& groupName) const
{
    QSettings settings("YourCompany", "AppTimeLimiter");
    return settings.value("GroupLimit_" + groupName, 0).toInt();
}

int AppMonitor::getGroupUsedSeconds(const QString& groupName) const
{
    QStringList apps = getGroupApps(groupName);
    int total = 0;
    for (const QString& app : apps) {
        total += m_appUsage.value(app, 0);
    }
    QSettings settings("YourCompany", "AppTimeLimiter");
    int offset = settings.value("GroupOffset_" + groupName, 0).toInt();
    total -= offset;
    if (total < 0) total = 0;
    return total;
}

void AppMonitor::addGroup(const QString& groupName)
{
    QSettings settings("YourCompany", "AppTimeLimiter");
    QStringList groups = settings.value("AppGroups").toStringList();
    if (!groups.contains(groupName)) {
        groups.append(groupName);
        settings.setValue("AppGroups", groups);
    }
    qDebug() << "[AppMonitor] Group created:" << groupName;
}

void AppMonitor::removeGroup(const QString& groupName)
{
    QSettings settings("YourCompany", "AppTimeLimiter");

    // 移除组内所有应用的分组关系
    QStringList apps = getGroupApps(groupName);
    settings.remove("GroupApps_" + groupName);
    settings.remove("GroupLimit_" + groupName);
    settings.remove("GroupOffset_" + groupName);

    // 从组列表中移除
    QStringList groups = settings.value("AppGroups").toStringList();
    groups.removeAll(groupName);
    settings.setValue("AppGroups", groups);

    // 清除 override 标记
    m_groupLimitOverridden.removeAll(groupName);

    qDebug() << "[AppMonitor] Group removed:" << groupName << "with" << apps.size() << "apps";
}

void AppMonitor::setGroupLimit(const QString& groupName, int limitMinutes)
{
    QSettings settings("YourCompany", "AppTimeLimiter");
    settings.setValue("GroupLimit_" + groupName, limitMinutes);
    qDebug() << "[AppMonitor] Group limit set:" << groupName << "->" << limitMinutes << "min";
}

void AppMonitor::addAppToGroup(const QString& processName, const QString& groupName)
{
    QSettings settings("YourCompany", "AppTimeLimiter");

    // 如果应用已在其他组中，先从旧组移除
    QString oldGroup = getAppGroup(processName);
    if (!oldGroup.isEmpty() && oldGroup != groupName) {
        removeAppFromGroup(processName);
    }

    // 添加到新组
    QStringList apps = settings.value("GroupApps_" + groupName).toStringList();
    if (!apps.contains(processName)) {
        apps.append(processName);
        settings.setValue("GroupApps_" + groupName, apps);
    }
    qDebug() << "[AppMonitor] App" << processName << "added to group" << groupName;
}

void AppMonitor::removeAppFromGroup(const QString& processName)
{
    QSettings settings("YourCompany", "AppTimeLimiter");
    QStringList groups = settings.value("AppGroups").toStringList();

    for (const QString& group : groups) {
        QStringList apps = settings.value("GroupApps_" + group).toStringList();
        if (apps.contains(processName)) {
            apps.removeAll(processName);
            settings.setValue("GroupApps_" + group, apps);
            qDebug() << "[AppMonitor] App" << processName << "removed from group" << group;
            return;
        }
    }
}

QString AppMonitor::getAppGroup(const QString& processName) const
{
    QSettings settings("YourCompany", "AppTimeLimiter");
    QStringList groups = settings.value("AppGroups").toStringList();

    for (const QString& group : groups) {
        QStringList apps = settings.value("GroupApps_" + group).toStringList();
        if (apps.contains(processName)) {
            return group;
        }
    }
    return QString();
}

void AppMonitor::clearGroupLimitOverride(const QString& groupName)
{
    qDebug() << "[AppMonitor] Clearing group limit override for" << groupName;
    m_groupLimitOverridden.removeAll(groupName);
}

void AppMonitor::extendGroupLimit(const QString& groupName)
{
    // 设置偏移为当前组总用量，使有效已用时间归零
    int currentUsed = getGroupUsedSeconds(groupName);
    QSettings settings("YourCompany", "AppTimeLimiter");
    int oldOffset = settings.value("GroupOffset_" + groupName, 0).toInt();
    settings.setValue("GroupOffset_" + groupName, oldOffset + currentUsed);

    // 清除 override 标记，让限时可以重新触发
    m_groupLimitOverridden.removeAll(groupName);

    qDebug() << "[AppMonitor] Group limit extended:" << groupName
             << "offset adjusted by" << currentUsed << "seconds";
}

// ==================== 历史记录接口 ====================

QStringList AppMonitor::getHistoryDates() const
{
    QSettings settings("YourCompany", "AppTimeLimiter");
    QStringList groups = settings.childGroups();

    QList<QDate> dates;
    for (const QString& g : groups) {
        if (g.startsWith("AppUsage_")) {
            QString dateStr = g.mid(9);   // 去掉 "AppUsage_" 前缀
            QDate d = QDate::fromString(dateStr, "yyyy-MM-dd");
            if (d.isValid()) {
                dates.append(d);
            }
        }
    }

    // 降序排列（最近的日期在前）
    std::sort(dates.begin(), dates.end(), [](const QDate& a, const QDate& b) {
        return a > b;
    });

    QStringList result;
    for (const QDate& d : dates) {
        result.append(d.toString("yyyy-MM-dd"));
    }
    return result;
}

QMap<QString, int> AppMonitor::getHistoryByDate(const QString& date) const
{
    QMap<QString, int> data;
    QSettings settings("YourCompany", "AppTimeLimiter");
    settings.beginGroup("AppUsage_" + date);
    QStringList keys = settings.childKeys();
    for (const QString& key : keys) {
        data[key] = settings.value(key).toInt();
    }
    settings.endGroup();
    return data;
}

void AppMonitor::clearHistoryExceptToday()
{
    QString today = QDate::currentDate().toString("yyyy-MM-dd");
    QSettings settings("YourCompany", "AppTimeLimiter");
    QStringList groups = settings.childGroups();

    int removed = 0;
    for (const QString& g : groups) {
        if (g.startsWith("AppUsage_")) {
            QString dateStr = g.mid(9);
            if (dateStr != today) {
                settings.remove(g);
                removed++;
            }
        }
    }
    qDebug() << "[AppMonitor] Cleared" << removed << "history date(s) except today";
}

void AppMonitor::clearHistoryBeforeDate(const QDate& cutoff)
{
    if (!cutoff.isValid()) return;

    QSettings settings("YourCompany", "AppTimeLimiter");
    QStringList groups = settings.childGroups();

    int removed = 0;
    for (const QString& g : groups) {
        if (g.startsWith("AppUsage_")) {
            QString dateStr = g.mid(9);
            QDate d = QDate::fromString(dateStr, "yyyy-MM-dd");
            if (d.isValid() && d < cutoff) {
                settings.remove(g);
                removed++;
            }
        }
    }
    qDebug() << "[AppMonitor] Cleared" << removed << "history date(s) before" << cutoff.toString("yyyy-MM-dd");
}

void AppMonitor::autoCleanHistory()
{
    QSettings settings("YourCompany", "AppTimeLimiter");
    QString cutoffStr = settings.value("HistoryCleanupDate").toString();
    if (cutoffStr.isEmpty()) return;   // 未设置自动清理日期，跳过

    QDate cutoff = QDate::fromString(cutoffStr, "yyyy-MM-dd");
    if (!cutoff.isValid()) return;

    clearHistoryBeforeDate(cutoff);
}
