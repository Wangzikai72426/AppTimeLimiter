#define INITGUID
#include "MainWindow.h"
#include "SettingsDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSettings>
#include <QMessageBox>
#include <QProcess>
#include <QFile>
#include <QApplication>
#include <QCheckBox>
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <comdef.h>
#include <shlguid.h>
#include <shlobj.h>
#include <QDebug>
#include <qfileinfo.h>
#include <QStandardPaths>
#include <QFileInfo>
#include <QCoreApplication>
#include <xcharconv.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    // Total daily limit spin box
    m_totalLimitSpin = new QSpinBox(this);
    m_totalLimitSpin->setRange(0, 1440); // 0 = unlimited, max 24 hours
    m_totalLimitSpin->setSuffix(" 分钟 (0=无限制)");
    m_totalLimitSpin->setSingleStep(10);

    // Password input
    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText("设置解锁密码");

    //Uninstall Button
    m_uninstall = new QPushButton("卸载 健康使用电脑",this);

    QPushButton* confirmBtn = new QPushButton("确定", this);
    QPushButton* cancelBtn = new QPushButton("取消", this);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // --- Total limit section ---
    QGroupBox* limitGroup = new QGroupBox("每日电脑总使用时长");
    QVBoxLayout* limitLayout = new QVBoxLayout(limitGroup);
    QLabel* limitHint = new QLabel("设置每日允许使用电脑的总时长。\n设为 0 表示不限制。");
    limitHint->setStyleSheet("color: gray; font-size: 12px;");
    limitLayout->addWidget(limitHint);
    limitLayout->addWidget(m_totalLimitSpin);
    mainLayout->addWidget(limitGroup);

    // --- Password section ---
    QGroupBox* pwdGroup = new QGroupBox("管理员密码");
    QVBoxLayout* pwdLayout = new QVBoxLayout(pwdGroup);
    QLabel* pwdHint = new QLabel("设置解锁密码，用于解锁屏幕和退出程序。\n默认密码: 2026888");
    pwdHint->setStyleSheet("color: gray; font-size: 12px;");
    pwdLayout->addWidget(pwdHint);
    pwdLayout->addWidget(m_passwordEdit);
    mainLayout->addWidget(pwdGroup);

    // --- 自启动 ---
    QGroupBox* autoGroup = new QGroupBox("开机自启动", this);
    QVBoxLayout* autoLayout = new QVBoxLayout(autoGroup);
    isAutoStart = new QCheckBox("开机自启动", this);
    // 所有用户统一使用 Windows 服务方式自启动
    isAutoStart->setText("使用 Windows 服务在登录时自动启动");
    isAutoStart->setChecked(isServiceInstalled());
    connect(isAutoStart, &QCheckBox::toggled, this, &SettingsDialog::onAutoStartToggled);
    autoLayout->addWidget(isAutoStart);
    mainLayout->addWidget(autoGroup);

    QPushButton* manageHiddenBtn = new QPushButton("管理隐藏应用", this);
    connect(manageHiddenBtn, &QPushButton::clicked, this, &SettingsDialog::showHiddenAppsRequested);

    // --- 历史记录管理 ---
    QGroupBox* historyGroup = new QGroupBox("历史记录管理", this);
    QVBoxLayout* historyLayout = new QVBoxLayout(historyGroup);

    m_clearHistoryBtn = new QPushButton("清除除今天外所有记录", this);
    connect(m_clearHistoryBtn, &QPushButton::clicked, this, &SettingsDialog::onClearHistory);
    historyLayout->addWidget(m_clearHistoryBtn);

    QHBoxLayout* autoCleanLayout = new QHBoxLayout();
    QLabel* autoCleanHint = new QLabel("自动清理该日期之前的记录:", this);
    m_autoCleanDateEdit = new QDateEdit(this);
    m_autoCleanDateEdit->setCalendarPopup(true);
    m_autoCleanDateEdit->setDisplayFormat("yyyy-MM-dd");
    m_applyCleanBtn = new QPushButton("保存自动清理设置", this);
    connect(m_applyCleanBtn, &QPushButton::clicked, this, &SettingsDialog::onSaveAutoClean);
    autoCleanLayout->addWidget(autoCleanHint);
    autoCleanLayout->addWidget(m_autoCleanDateEdit);
    autoCleanLayout->addWidget(m_applyCleanBtn);
    historyLayout->addLayout(autoCleanLayout);

    QLabel* autoCleanTip = new QLabel("留空并保存 = 关闭自动清理；程序启动及每日切换时自动执行清理。", this);
    autoCleanTip->setStyleSheet("color: gray; font-size: 11px;");
    historyLayout->addWidget(autoCleanTip);

    mainLayout->addWidget(historyGroup);

    // --- Buttons ---
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    mainLayout->addWidget(manageHiddenBtn);
    mainLayout->addWidget(m_uninstall);
    btnLayout->addWidget(confirmBtn);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);

    // Connect signals
    connect(confirmBtn, &QPushButton::clicked, this, &SettingsDialog::onConfirm);
    connect(cancelBtn, &QPushButton::clicked, this, &SettingsDialog::onCancel);
    connect(m_uninstall, &QPushButton::clicked, this, &SettingsDialog::uninstall);

    // Load existing settings
    loadSettings();

    //setFixedSize(400, 300);
    setWindowTitle("设置");
}



void SettingsDialog::uninstall() {

    if (QMessageBox::question(this,"健康使用电脑","你确定要卸载吗?",QMessageBox::Yes|QMessageBox::Cancel)==QMessageBox::Cancel) {
        return;
    }

    QString uninstallerPath = "C:/ProgramData/AppTimeLimiter/Uninstall/unins000.exe";

    // 3. 检查卸载程序是否存在
    if (!QFile::exists(uninstallerPath)) {
        QMessageBox::warning(this, "错误", "找不到卸载程序，请手动删除安装文件夹。");
        return;
    }

    // 4. 启动卸载程序
    if (QProcess::startDetached(uninstallerPath, QStringList())) {
        // 5. 主程序立即退出，避免文件被占用导致卸载失败
        QApplication::quit();
    }
    else {
        QMessageBox::warning(this, "错误", "无法启动卸载程序，请稍后重试。");
    }
}

void SettingsDialog::onConfirm()
{
    saveSettings();

    QMessageBox::information(this, "成功", "设置已保存！");
    accept();
}

void SettingsDialog::onCancel()
{
    reject();
}

void SettingsDialog::onClearHistory()
{
    int ret = QMessageBox::question(this, "确认清除",
        "将清除除今天以外的所有历史使用记录，且不可恢复。\n确定继续吗？",
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    emit clearHistoryRequested();
    QMessageBox::information(this, "完成", "已清除除今天外的所有历史记录。");
}

void SettingsDialog::onSaveAutoClean()
{
    QSettings settings("YourCompany", "AppTimeLimiter");
    QDate d = m_autoCleanDateEdit->date();
    // 若日期编辑框为空（用户清空），则关闭自动清理
    if (m_autoCleanDateEdit->date().isNull() || !m_autoCleanDateEdit->dateTime().isValid()) {
        settings.remove("HistoryCleanupDate");
        QMessageBox::information(this, "完成", "已关闭自动清理历史记录功能。");
    }
    else {
        settings.setValue("HistoryCleanupDate", d.toString("yyyy-MM-dd"));
        emit autoCleanDateApplied(d);   // 保存即生效：立即清理超过截止日期的记录
        QMessageBox::information(this, "完成",
            QString("已保存：已自动清理 %1 之前的历史记录，并将在程序启动及每日切换时持续生效。")
            .arg(d.toString("yyyy-MM-dd")));
    }
}

void SettingsDialog::loadSettings()
{
    QSettings settings("YourCompany", "AppTimeLimiter");
    int totalLimit = settings.value("TotalLimit", 0).toInt();
    QString password = settings.value("Password", "2026888").toString();
    QString cleanupDate = settings.value("HistoryCleanupDate").toString();

    m_totalLimitSpin->setValue(totalLimit);
    m_passwordEdit->setText(password);
    if (!cleanupDate.isEmpty()) {
        QDate d = QDate::fromString(cleanupDate, "yyyy-MM-dd");
        if (d.isValid()) m_autoCleanDateEdit->setDate(d);
    }
}

void SettingsDialog::saveSettings()
{
    QSettings settings("YourCompany", "AppTimeLimiter");
    settings.setValue("TotalLimit", m_totalLimitSpin->value());
    settings.setValue("Password", m_passwordEdit->text());
    // 注意：HistoryCleanupDate 由 onSaveAutoClean 专门管理，此处不覆盖

    QMessageBox::information(this, "成功", "设置已保存!");
}

// ---------------- 开机自启动 ----------------
void SettingsDialog::onAutoStartToggled(bool checked)
{
    // 所有用户统一使用 Windows 服务方式
    if (checked) {
        if (!installAutoStartService()) {
            QMessageBox::warning(this, "错误", "无法安装自启动服务，请确认已授权管理员权限。");
            isAutoStart->setChecked(false);
        }
        else {
            QMessageBox::information(this, "成功", "开机自启动已启用（Windows 服务）。重启或下次登录后将自动启动本程序。");
        }
    }
    else {
        if (!uninstallAutoStartService()) {
            QMessageBox::warning(this, "错误", "无法卸载自启动服务。");
            isAutoStart->setChecked(true);
        }
        else {
            QMessageBox::information(this, "成功", "开机自启动已关闭。");
        }
    }
}

bool SettingsDialog::isServiceInstalled()
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, L"AppTimeLimiterSvc", SERVICE_QUERY_STATUS);
    bool installed = (svc != NULL);
    if (svc) CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return installed;
}

bool SettingsDialog::installAutoStartService()
{
    QString svcExe = QCoreApplication::applicationDirPath() + "/AppTimeLimiterService.exe";
    if (!QFile::exists(svcExe)) {
        QMessageBox::warning(this, "错误", "找不到 AppTimeLimiterService.exe，请先构建服务工程。");
        return false;
    }
    QString appExe = QCoreApplication::applicationFilePath();

    if (isProcessElevated()) {
        // 已提权：子进程继承令牌，直接注册服务
        QProcess p;
        p.start(svcExe, QStringList() << "install" << appExe);
        if (!p.waitForStarted()) return false;
        p.waitForFinished(20000);
        return p.exitCode() == 0;
    } else {
        // 未提权：通过 ShellExecuteExW 以管理员身份启动服务程序（触发 UAC）
        QString params = QString("install \"%1\"").arg(appExe);
        SHELLEXECUTEINFOW sei = {};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = L"runas";
        sei.lpFile = (LPCWSTR)svcExe.utf16();
        sei.lpParameters = (LPCWSTR)params.utf16();
        sei.nShow = SW_HIDE;
        if (!ShellExecuteExW(&sei)) {
            DWORD err = GetLastError();
            if (err == ERROR_CANCELLED) {
                QMessageBox::warning(this, "提示", "已取消管理员授权，无法安装服务。");
            }
            return false;
        }
        // 等待服务安装完成
        if (sei.hProcess) {
            WaitForSingleObject(sei.hProcess, 30000);
            DWORD exitCode = 1;
            GetExitCodeProcess(sei.hProcess, &exitCode);
            CloseHandle(sei.hProcess);
            return exitCode == 0;
        }
        return true;
    }
}

bool SettingsDialog::uninstallAutoStartService()
{
    QString svcExe = QCoreApplication::applicationDirPath() + "/AppTimeLimiterService.exe";
    if (!QFile::exists(svcExe)) return true;   // 无服务可卸载，视为成功

    if (isProcessElevated()) {
        // 已提权：直接卸载
        QProcess p;
        p.start(svcExe, QStringList() << "remove");
        if (!p.waitForStarted()) return false;
        p.waitForFinished(20000);
        return p.exitCode() == 0;
    } else {
        // 未提权：通过 ShellExecuteExW 以管理员身份启动服务程序（触发 UAC）
        SHELLEXECUTEINFOW sei = {};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = L"runas";
        sei.lpFile = (LPCWSTR)svcExe.utf16();
        sei.lpParameters = L"remove";
        sei.nShow = SW_HIDE;
        if (!ShellExecuteExW(&sei)) {
            DWORD err = GetLastError();
            if (err == ERROR_CANCELLED) {
                QMessageBox::warning(this, "提示", "无管理员授权，无法卸载服务。");
            }
            return false;
        }
        if (sei.hProcess) {
            WaitForSingleObject(sei.hProcess, 30000);
            DWORD exitCode = 1;
            GetExitCodeProcess(sei.hProcess, &exitCode);
            CloseHandle(sei.hProcess);
            return exitCode == 0;
        }
        return true;
    }
}

// ---------------- 辅助函数：检查当前进程是否已提权 ----------------

bool SettingsDialog::isProcessElevated()
{
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return false;
    TOKEN_ELEVATION elev = {};
    DWORD cbSize = sizeof(elev);
    bool elevated = GetTokenInformation(hToken, TokenElevation, &elev, sizeof(elev), &cbSize)
                    && elev.TokenIsElevated;
    CloseHandle(hToken);
    return elevated;
}
