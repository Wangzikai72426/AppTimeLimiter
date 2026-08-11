#include "MainWindow.h"
#include "ProcessProtector.h"
#include <QApplication>
#include <QIcon>
#include <QSharedMemory>
#include <QTimer>
#include <QDebug>
#include <QMessageBox>
#include <cstring>
#include <windows.h>
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

const char* SHARED_CMD_NAME = "AppTimeLimiterCommand";

// 由 Windows 服务在登录时拉起时附加的参数，表示"仅后台运行并显示托盘图标"
const char* ARG_BACKGROUND = "--background";

// ---------------- 辅助函数：检测当前用户身份与提权状态 ----------------

// 检查当前进程是否已提权（以管理员身份运行）
static bool IsProcessElevated()
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

// 检查当前用户是否属于 Administrators 组（无论当前进程是否已提权）
// 对于 UAC 过滤令牌，Administrators SID 以 deny-only 形式存在，但用户仍是管理员
static bool IsCurrentUserAdmin()
{
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return false;

    DWORD dwSize = 0;
    GetTokenInformation(hToken, TokenGroups, NULL, 0, &dwSize);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        CloseHandle(hToken);
        return false;
    }

    PTOKEN_GROUPS pGroups = (PTOKEN_GROUPS)LocalAlloc(LPTR, dwSize);
    if (!pGroups || !GetTokenInformation(hToken, TokenGroups, pGroups, dwSize, &dwSize)) {
        LocalFree(pGroups);
        CloseHandle(hToken);
        return false;
    }
    CloseHandle(hToken);

    SID_IDENTIFIER_AUTHORITY auth = SECURITY_NT_AUTHORITY;
    PSID pAdminSid = NULL;
    if (!AllocateAndInitializeSid(&auth, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &pAdminSid)) {
        LocalFree(pGroups);
        return false;
    }

    bool isAdmin = false;
    for (DWORD i = 0; i < pGroups->GroupCount; i++) {
        if (EqualSid(pGroups->Groups[i].Sid, pAdminSid)) {
            // Administrators SID 出现在令牌中即表示该用户是管理员
            // （即使当前是过滤令牌，deny-only 属性不影响判断）
            isAdmin = true;
            break;
        }
    }

    FreeSid(pAdminSid);
    LocalFree(pGroups);
    return isAdmin;
}

int main(int argc, char* argv[])
{
    // 判断是否为服务拉起的"后台启动"模式（此时不应弹出主窗口）
    
    bool startHidden = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], ARG_BACKGROUND) == 0) {
            startHidden = true;
            qDebug() << "检测到--background参数，不显示主窗口";
            break;
        }
    }

    // ---------- 管理员用户提权逻辑 ----------
    // manifest 设为 AsInvoker 后，应用默认以当前进程权限运行。
    // 若当前用户是管理员但本进程未提权，且非后台启动模式，
    // 则以管理员身份重新启动（触发 UAC 提示），与原有行为一致。
    // 普通用户（非管理员）不触发提权，直接以普通权限运行。
    if (!startHidden && IsCurrentUserAdmin() && !IsProcessElevated()) {
        wchar_t exePath[MAX_PATH] = { 0 };
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        SHELLEXECUTEINFOW sei = {};
        sei.cbSize = sizeof(sei);
        sei.lpVerb = L"runas";
        sei.lpFile = exePath;
        sei.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&sei)) {
            qDebug() << "管理员用户：已启动提权实例，当前非提权实例退出";
            return 0;  // 提权实例已启动，当前非提权实例退出
        }
        // 若提权失败（用户拒绝 UAC），继续以非提权方式运行
        qDebug() << "提权请求被拒绝，将以非管理员身份运行";
    }
    
    QApplication app(argc, argv);
    
    if (!ProcessProtector::DenyTerminateAccess()) {
        // 记录失败日志，可以继续运行但不具防杀能力
        qWarning("Failed to apply ACL protection.");
        if (QMessageBox::question(NULL, "健康使用电脑", "无法启用ACL保护，是否退出?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) QApplication::quit();
    }

    // ---------- 单例检测 & 命令传递 ----------
    QSharedMemory sharedCmd(SHARED_CMD_NAME);

    // 尝试创建共享内存（大小为 128 字节）
    if (sharedCmd.create(128)) {
        // 第一个实例：清空命令
        memset(sharedCmd.data(), 0, 128);
        qDebug() << "主程序启动，共享内存已创建";
    }
    else {
        // 已有实例在运行
        if (!startHidden) {
            // 前台（手动）启动：通知已有实例显示主窗口
            if (sharedCmd.attach()) {
                char* data = (char*)sharedCmd.data();
                if (data) {
                    strncpy_s(data, 128, "SHOW", 128);
                    qDebug() << "新实例发送 SHOW 命令，退出";
                }
                sharedCmd.detach();
            }
        }
        else {
            // 后台（服务）启动且已有实例：静默退出，避免弹出主窗口
            qDebug() << "后台实例：已有实例运行，静默退出";
        }
        return 0;   // 新进程退出
    }

    // ---------- 设置组织名和应用名（用于 QSettings） ----------
    QCoreApplication::setOrganizationName("YourCompany");
    QCoreApplication::setApplicationName("AppTimeLimiter");

    // 设置窗口图标
    QGuiApplication::setWindowIcon(QIcon(":/icon.png"));
    app.setWindowIcon(QIcon(":/icon.png"));

    // ---------- 创建主窗口 ----------
    MainWindow w;
    if (startHidden) {
        // 由服务拉起：仅在后台运行，显示系统托盘图标，不弹出主窗口
        qDebug() << "后台启动模式：主窗口隐藏，仅显示系统托盘";
    }
    else {
        w.show();
    }

    // ---------- 定时器：检查共享内存中的命令 ----------
    QTimer* cmdTimer = new QTimer(&app);
    QObject::connect(cmdTimer, &QTimer::timeout, [&]() {
        // 确保共享内存已附加（第一个实例始终保持附加）
        if (!sharedCmd.isAttached()) {
            if (!sharedCmd.attach()) {
                qDebug() << "定时器：无法附加到共享内存";
                return;
            }
        }

        char* data = (char*)sharedCmd.data();
        if (data && strcmp(data, "SHOW") == 0) {
            // 收到显示命令 → 激活主窗口
            w.show();
            w.raise();
            w.activateWindow();

            // 清除命令，防止重复触发
            memset(data, 0, 128);
            qDebug() << "执行 SHOW 命令，已清除标记";
        }
        });
    cmdTimer->start(300);   // 300ms 检查一次，响应更快

    return app.exec();
}