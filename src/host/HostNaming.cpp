// HostNaming.cpp : 宿主命名实现
/******************************************************************************/
#include "HostNaming.h"

#include <windows.h>
#include <tlhelp32.h>
#include <cwchar>

/* 当前 exe 文件名主干（不含扩展名） */
std::wstring GetExeBaseName()
{
    wchar_t szExe[1024];
    GetModuleFileNameW(NULL, szExe, 1024);
    std::wstring path = szExe;
    size_t slash = path.find_last_of(L"\\/");
    std::wstring base = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    size_t dot = base.find_last_of(L'.');
    if (dot != std::wstring::npos)
        base = base.substr(0, dot);
    return base;
}

static std::wstring WLower(std::wstring s)
{
    for (size_t i = 0; i < s.size(); i++)
        if (s[i] >= L'A' && s[i] <= L'Z')
            s[i] = (wchar_t)(s[i] - L'A' + L'a');
    return s;
}

/* 取指定进程 exe 文件名主干 */
static std::wstring ExeBaseOf(DWORD pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return L"";
    wchar_t buf[1024];
    DWORD sz = 1024;
    BOOL ok = QueryFullProcessImageNameW(h, 0, buf, &sz);
    CloseHandle(h);
    if (!ok)
        return L"";
    std::wstring p = buf;
    size_t s = p.find_last_of(L"\\/");
    std::wstring b = (s == std::wstring::npos) ? p : p.substr(s + 1);
    size_t d = b.find_last_of(L'.');
    if (d != std::wstring::npos)
        b = b.substr(0, d);
    return b;
}

/* 实例序号：同名 exe 宿主进程中，创建时间早于自己的数量 + 1（按启动先后排序） */
int GetInstanceOrdinal()
{
    /* 重启继承：RestartHost 用环境变量 VSTHOST_ORDINAL 传给子进程，直接采用
       （不能走命令行参数：MFC ParseCommandLine 会把裸数字当作文件路径，
       导致重启后 DoLoad("2") 加载失败而非同名自动加载） */
    {
        wchar_t env[32] = L"";
        DWORD sz = GetEnvironmentVariableW(L"VSTHOST_ORDINAL", env, 32);
        if (sz > 0 && sz < 32)
        {
            int n = _wtoi(env);
            if (n > 0)
                return n;
        }
    }

    std::wstring base = GetExeBaseName();
    DWORD myPid = GetCurrentProcessId();
    std::wstring lbase = WLower(base);

    FILETIME myCreate = {0, 0};
    {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, myPid);
        if (h)
        {
            FILETIME e, k, u;
            GetProcessTimes(h, &myCreate, &e, &k, &u);
            CloseHandle(h);
        }
    }

    int ordinal = 1;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe))
        {
            do
            {
                if (pe.th32ProcessID == myPid)
                    continue;
                std::wstring b = ExeBaseOf(pe.th32ProcessID);
                if (b.empty() || WLower(b) != lbase)
                    continue;
                HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                if (!h)
                    continue;
                FILETIME create, e, k, u;
                BOOL ok = GetProcessTimes(h, &create, &e, &k, &u);
                CloseHandle(h);
                if (!ok)
                    continue;
                /* 启动更早（创建时间更早）则序号 +1 */
                if (CompareFileTime(&create, &myCreate) < 0)
                    ordinal++;
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }
    return ordinal;
}

/* 按基础名 + 递增序号生成实例名 */
std::wstring ComputeInstanceName(const std::wstring &base)
{
    int ordinal = GetInstanceOrdinal();
    wchar_t buf[32];
    swprintf(buf, 32, L"_%d", ordinal);
    return base + buf;
}

/* 宿主展示名 / JACK 客户端名 */
std::wstring ComputeHostName()
{
    return ComputeInstanceName(GetExeBaseName());
}

/*****************************************************************************/
/* Shell exe 命名辅助                                                        */
/*****************************************************************************/

/* ANSI -> 宽字符（CP_ACP） */
std::wstring AnsiToWide(const std::string &a)
{
    if (a.empty())
        return std::wstring();
    int n = MultiByteToWideChar(CP_ACP, 0, a.c_str(), -1, NULL, 0);
    if (n <= 1)
        return std::wstring();
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_ACP, 0, a.c_str(), -1, &w[0], n);
    return w;
}

/* 过滤文件名非法字符 */
std::wstring SanitizeFileName(const std::wstring &name)
{
    std::wstring r = name;
    for (size_t i = 0; i < r.size(); i++)
        if (wcschr(L"\\/:*?\"<>|", r[i]))
            r[i] = L'_';
    return r;
}

/* 生成 Shell 直选 exe 名 */
std::wstring MakeShellExeName(const std::wstring &shellBase, const std::wstring &internalName)
{
    return L"(" + shellBase + L")" + SanitizeFileName(internalName) + L".exe";
}
