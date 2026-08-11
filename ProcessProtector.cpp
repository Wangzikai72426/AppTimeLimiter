#include "ProcessProtector.h"
#include <QDebug>

bool ProcessProtector::DenyTerminateAccess()
{
    HANDLE hProcess = GetCurrentProcess();
    DWORD dwErr = ERROR_SUCCESS;

    // 1. 获取当前进程所有者的 SID（当前用户）
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(hProcess, TOKEN_QUERY, &hToken)) {
        qCritical() << "OpenProcessToken failed:" << GetLastError();
        return false;
    }

    // 获取 Token 用户 SID 所需大小
    DWORD dwSize = 0;
    GetTokenInformation(hToken, TokenUser, nullptr, 0, &dwSize);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        CloseHandle(hToken);
        return false;
    }

    PTOKEN_USER pTokenUser = (PTOKEN_USER)LocalAlloc(LPTR, dwSize);
    if (!pTokenUser || !GetTokenInformation(hToken, TokenUser, pTokenUser, dwSize, &dwSize)) {
        CloseHandle(hToken);
        LocalFree(pTokenUser);
        return false;
    }
    CloseHandle(hToken);

    PSID pOwnerSid = pTokenUser->User.Sid;  // 当前用户 SID

    // 2. 创建 Everyone SID
    PSID pEveryoneSid = nullptr;
    SID_IDENTIFIER_AUTHORITY SIDAuthWorld = SECURITY_WORLD_SID_AUTHORITY;
    if (!AllocateAndInitializeSid(&SIDAuthWorld, 1, SECURITY_WORLD_RID,
        0, 0, 0, 0, 0, 0, 0, &pEveryoneSid)) {
        LocalFree(pTokenUser);
        return false;
    }

    // 3. 创建 SYSTEM SID (S-1-5-18)
    PSID pSystemSid = nullptr;
    SID_IDENTIFIER_AUTHORITY SIDAuthNT = SECURITY_NT_AUTHORITY;
    if (!AllocateAndInitializeSid(&SIDAuthNT, 1, SECURITY_LOCAL_SYSTEM_RID,
        0, 0, 0, 0, 0, 0, 0, &pSystemSid)) {
        FreeSid(pEveryoneSid);
        LocalFree(pTokenUser);
        return false;
    }

    // 4. 构造 EXPLICIT_ACCESS 数组
    EXPLICIT_ACCESS ea[3] = {};
    // 允许当前用户完全控制（所有者需要 PROCESS_ALL_ACCESS 或至少 PROCESS_TERMINATE 以便自己可退出）
    ea[0].grfAccessPermissions = PROCESS_ALL_ACCESS;
    ea[0].grfAccessMode = SET_ACCESS;
    ea[0].grfInheritance = NO_INHERITANCE;
    ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[0].Trustee.ptstrName = (LPTSTR)pOwnerSid;

    // 允许 SYSTEM 完全控制（驱动、服务等需要）
    ea[1].grfAccessPermissions = PROCESS_ALL_ACCESS;
    ea[1].grfAccessMode = SET_ACCESS;
    ea[1].grfInheritance = NO_INHERITANCE;
    ea[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[1].Trustee.ptstrName = (LPTSTR)pSystemSid;

    // 拒绝 Everyone 的 PROCESS_TERMINATE (和 PROCESS_TERMINATE 权限)
    ea[2].grfAccessPermissions = PROCESS_TERMINATE;
    ea[2].grfAccessMode = DENY_ACCESS;
    ea[2].grfInheritance = NO_INHERITANCE;
    ea[2].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[2].Trustee.ptstrName = (LPTSTR)pEveryoneSid;

    // 5. 合成新的 DACL
    PACL pNewDacl = nullptr;
    dwErr = SetEntriesInAcl(3, ea, nullptr, &pNewDacl);
    if (dwErr != ERROR_SUCCESS) {
        qCritical() << "SetEntriesInAcl failed:" << dwErr;
        FreeSid(pSystemSid);
        FreeSid(pEveryoneSid);
        LocalFree(pTokenUser);
        return false;
    }

    // 6. 将新 DACL 设置到进程对象上
    dwErr = SetSecurityInfo(hProcess, SE_KERNEL_OBJECT,
        DACL_SECURITY_INFORMATION,
        nullptr, nullptr, pNewDacl, nullptr);
    if (dwErr != ERROR_SUCCESS) {
        qCritical() << "SetSecurityInfo failed:" << dwErr;
        LocalFree(pNewDacl);
        FreeSid(pSystemSid);
        FreeSid(pEveryoneSid);
        LocalFree(pTokenUser);
        return false;
    }

    // 清理
    LocalFree(pNewDacl);
    FreeSid(pSystemSid);
    FreeSid(pEveryoneSid);
    LocalFree(pTokenUser);

    qDebug() << "Process ACL protection applied successfully.";
    return true;
}