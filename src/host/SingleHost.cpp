// SingleHost.cpp : 单插件宿主核心实现
/******************************************************************************/
#include "SingleHost.h"

#include <windows.h>

#include <string>
#include <vector>
#include <cstring>
#include <cwchar>
#include <cstdio>

#include "Vst2Plugin.h"
#include "Vst3Plugin.h"
#include "HostNaming.h"

/*===========================================================================*/
/* 工具函数                                                                    */
/*===========================================================================*/

/* PE 位数探测：返回 IMAGE_FILE_MACHINE_*；失败返回 0 */
static WORD DetectMachine(const wchar_t *path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    DWORD rd = 0;
    IMAGE_DOS_HEADER dos;
    if (!ReadFile(h, &dos, sizeof(dos), &rd, NULL) || dos.e_magic != IMAGE_DOS_SIGNATURE)
    {
        CloseHandle(h);
        return 0;
    }
    SetFilePointer(h, dos.e_lfanew, NULL, FILE_BEGIN);
    IMAGE_NT_HEADERS nt;
    if (!ReadFile(h, &nt, sizeof(nt), &rd, NULL) || nt.Signature != IMAGE_NT_SIGNATURE)
    {
        CloseHandle(h);
        return 0;
    }
    CloseHandle(h);
    return nt.FileHeader.Machine;
}

/* 文件名（不含路径与扩展名）是否含 "WaveShell"（忽略大小写） */
static bool IsWaveShellPath(const std::wstring &path)
{
    size_t slash = path.find_last_of(L"\\/");
    std::wstring base = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    size_t dot = base.find_last_of(L'.');
    if (dot != std::wstring::npos)
        base = base.substr(0, dot);
    for (size_t i = 0; i < base.size(); i++)
        if (base[i] >= L'A' && base[i] <= L'Z')
            base[i] = (wchar_t)(base[i] - L'A' + L'a');
    return base.find(L"waveshell") != std::wstring::npos;
}

/* 取扩展名（小写） */
static std::wstring GetExtLower(const std::wstring &path)
{
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos)
        return L"";
    std::wstring ext = path.substr(dot);
    for (size_t i = 0; i < ext.size(); i++)
        if (ext[i] >= L'A' && ext[i] <= L'Z')
            ext[i] = (wchar_t)(ext[i] - L'A' + L'a');
    return ext;
}

/* 宽字符 -> ANSI（VST2 路径/名称用） */
static std::string WideToAnsi(const std::wstring &w)
{
    if (w.empty())
        return std::string();
    int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    if (n <= 1)
        return std::string();
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, &s[0], n, NULL, NULL);
    return s;
}

/* 当前 exe 所在目录（带结尾反斜杠） */
static std::wstring ExeDir()
{
    wchar_t sz[1024];
    GetModuleFileNameW(NULL, sz, 1024);
    std::wstring p = sz;
    size_t s = p.find_last_of(L"\\/");
    return (s == std::wstring::npos) ? L"" : p.substr(0, s + 1);
}

/* 原子写文件：先写 tmp 再改名 */
static bool WriteFileAtomic(const std::wstring &path, const void *data, int size)
{
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD wr = 0;
    BOOL ok = WriteFile(h, data, (DWORD)size, &wr, NULL);
    CloseHandle(h);
    if (!ok || (int)wr != size)
    {
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

static bool ReadFileAll(const std::wstring &path, std::vector<unsigned char> &out)
{
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD sz = GetFileSize(h, NULL);
    if (sz > 0 && sz < (64u << 20))
    {
        out.resize(sz);
        DWORD rd = 0;
        ReadFile(h, out.data(), sz, &rd, NULL);
    }
    CloseHandle(h);
    return !out.empty();
}

/*===========================================================================*/
/* CSingleHost                                                                */
/*===========================================================================*/
CSingleHost::CSingleHost()
    : m_pPlugin(NULL)
{
}

CSingleHost::~CSingleHost()
{
    Free();
}

/*****************************************************************************/
/* Close : 关闭当前插件                                                       */
/*****************************************************************************/
void CSingleHost::Close()
{
    Free();
}

/*****************************************************************************/
/* Free : 释放当前插件                                                         */
/*****************************************************************************/
void CSingleHost::Free()
{
    if (m_pPlugin)
    {
        m_pPlugin->Shutdown();
        delete m_pPlugin;
        m_pPlugin = NULL;
    }
    m_szModulePath.clear();
    m_szStateBase.clear();
}

/*****************************************************************************/
/* UpdateStateBase : 按当前插件 + 实例序号生成状态预设名/窗口标题              */
/*   普通插件 -> 插件文件名（模块主干）；Shell（多组件）-> 当前内部效果器名    */
/*****************************************************************************/
void CSingleHost::UpdateStateBase()
{
    m_szStateBase.clear();
    if (!m_pPlugin)
        return;

    std::wstring base;
    /* Shell（内部效果器 > 1）用当前内部效果器名作为基础名 */
    if (m_pPlugin->GetInternalCount() > 1)
    {
        const char *name = m_pPlugin->GetName();
        if (name && name[0])
            base = AnsiToWide(name);
    }
    if (base.empty())
    {
        /* 普通插件：直接用插件文件名（模块主干）作为基础名 */
        base = m_szModulePath;
        size_t s = base.find_last_of(L"\\/");
        if (s != std::wstring::npos)
            base = base.substr(s + 1);
        size_t d = base.find_last_of(L'.');
        if (d != std::wstring::npos)
            base = base.substr(0, d);
    }

    m_szStateBase = ComputeInstanceName(base);   /* <名字>_<实例序号> */
}

/*****************************************************************************/
/* LoadPath : 按路径加载（内部）                                               */
/*****************************************************************************/
bool CSingleHost::LoadPath(const std::wstring &path)
{
    Free();

    std::wstring ext = GetExtLower(path);
    if (ext != L".dll" && ext != L".vst3")
        return false;

    /* 位数校验：x86/x64 宿主各自对应加载，位数不符直接拒绝（不再走桥）
       注：.vst3 目录形式无法直接读 PE 头，DetectMachine 返回 0 时放行，
       由 Module::create 加载时自行失败。 */
    WORD machine = DetectMachine(path.c_str());
    if (machine != 0)
    {
        WORD hostMachine = (sizeof(void *) == 8) ? IMAGE_FILE_MACHINE_AMD64
                                                 : IMAGE_FILE_MACHINE_I386;
        if (machine != hostMachine)
        {
            SetLastError(ERROR_BAD_EXE_FORMAT);  /* 位数不匹配 */
            return false;
        }
    }

    if (ext == L".vst3")
    {
        /* VST3：Module::create + 多组件（WaveShell）支持（M3） */
        Vst3Plugin *p = new Vst3Plugin;
        if (!p->Load(path, 0))
        {
            delete p;
            SetLastError(ERROR_PROC_NOT_FOUND);
            return false;
        }
        m_pPlugin = p;
        m_szModulePath = path;
        p->Init(44100.0, 512);   /* 先实例化（GetName 生效）再算状态基名 */
        UpdateStateBase();
        return true;
    }

    Vst2Plugin *p = new Vst2Plugin;
    std::string pathA = WideToAnsi(path);   /* 按 ANSI 传给 VST2 */
    if (!p->Load(pathA.c_str(), 0))
    {
        delete p;
        return false;
    }
    m_pPlugin = p;
    m_szModulePath = path;
    p->Init(44100.0, 512);              /* 默认采样率/块大小，M4/M5 由后端决定 */
    UpdateStateBase();
    return true;
}

/*****************************************************************************/
/* LoadFromFile : 手动加载                                                     */
/*****************************************************************************/
bool CSingleHost::LoadFromFile(const std::wstring &path)
{
    if (!LoadPath(path))
        return false;

    /* 恢复状态（插件名_实例序号.fxp） */
    LoadStateFile();
    return true;
}

/*****************************************************************************/
/* LoadFromExeName : 同名自动加载（计划书 §5.1）                               */
/*****************************************************************************/
bool CSingleHost::LoadFromExeName(const std::wstring &exePath)
{
    /* 取 exe 所在目录 + 文件名主干 */
    size_t slash = exePath.find_last_of(L"\\/");
    std::wstring dir = (slash == std::wstring::npos) ? L"" : exePath.substr(0, slash + 1);
    std::wstring exeBase = (slash == std::wstring::npos) ? exePath : exePath.substr(slash + 1);
    size_t dot = exeBase.find_last_of(L'.');
    if (dot != std::wstring::npos)
        exeBase = exeBase.substr(0, dot);

    /* 0) exe 同目录同名 .ini 指定 shell（[shell2vst] shell=<绝对路径> name=内部名）
       优先级最高：shell 可放任意位置 */
    {
        std::wstring iniFile = exePath + L".ini";
        if (GetFileAttributesW(iniFile.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            wchar_t shell[1024] = L"";
            wchar_t name[256] = L"";
            GetPrivateProfileStringW(L"shell2vst", L"shell", L"", shell, 1024,
                                     iniFile.c_str());
            GetPrivateProfileStringW(L"shell2vst", L"name", L"", name, 256,
                                     iniFile.c_str());
            if (shell[0])
                return LoadShellInternal(shell, name);
        }
    }

    /* 1) 精确匹配：name.vst3 / name.dll（VST3 优先，M3 支持） */
    std::wstring candV3 = dir + exeBase + L".vst3";
    std::wstring candV2 = dir + exeBase + L".dll";
    DWORD aV3 = GetFileAttributesW(candV3.c_str());
    DWORD aV2 = GetFileAttributesW(candV2.c_str());
    if (aV3 != INVALID_FILE_ATTRIBUTES)
    {
        if (!LoadPath(candV3))
            return false;
        LoadStateFile();        /* 自动加载：恢复 插件名_实例序号.fxp */
        return true;
    }
    if (aV2 != INVALID_FILE_ATTRIBUTES)
    {
        if (!LoadPath(candV2))
            return false;
        LoadStateFile();        /* 自动加载：恢复 插件名_实例序号.fxp */
        return true;
    }

    /* 2) (Shell文件名)内部插件名 形式 */
    if (!exeBase.empty() && exeBase[0] == L'(')
    {
        size_t close = exeBase.find(L')');
        if (close != std::wstring::npos)
        {
            std::wstring shellName = exeBase.substr(1, close - 1);
            std::wstring internal = exeBase.substr(close + 1);
            std::wstring shV3 = dir + shellName + L".vst3";
            std::wstring shV2 = dir + shellName + L".dll";
            std::wstring shellPath;
            if (GetFileAttributesW(shV3.c_str()) != INVALID_FILE_ATTRIBUTES)
                shellPath = shV3;
            else if (GetFileAttributesW(shV2.c_str()) != INVALID_FILE_ATTRIBUTES)
                shellPath = shV2;
            if (!shellPath.empty())
                return LoadShellInternal(shellPath, internal);
        }
    }
    return false;
}

/*****************************************************************************/
/* LoadShellInternal : 加载 shell 并切换到指定内部效果器                      */
/*   internalName 为空 -> 用默认（第一个）；启动直选不保存未使用的旧状态       */
/*****************************************************************************/
bool CSingleHost::LoadShellInternal(const std::wstring &shellPath,
                                    const std::wstring &internalName)
{
    if (!LoadPath(shellPath))
        return false;

    if (!internalName.empty())
    {
        /* 用内部效果器名匹配（宽转窄后：忽略大小写精确 -> 唯一子串） */
        std::string internalA = WideToAnsi(internalName);
        int n = GetInternalCount();
        int match = -1;
        for (int i = 0; i < n; i++)
        {
            PluginInternalInfo info;
            if (GetInternalInfo(i, &info) &&
                _stricmp(info.name, internalA.c_str()) == 0)
            {
                match = i;
                break;
            }
        }
        if (match < 0)
        {
            int found = 0;
            for (int i = 0; i < n; i++)
            {
                PluginInternalInfo info;
                if (GetInternalInfo(i, &info) &&
                    strstr(info.name, internalA.c_str()))
                {
                    match = i;
                    found++;
                }
            }
            if (found != 1)
                match = -1;
        }
        if (match >= 0)
            SwitchInternal(match, false);   /* 启动直选：不保存未使用的旧内部效果器 */
    }
    else
    {
        /* 无文件名直选：按上次选择（last_uid）恢复，未命中则用默认（第一个） */
        int last = LoadLastUid();
        if (last >= 0)
            SwitchInternal(last, false);
    }

    /* 启动即恢复该插件的预设状态 */
    LoadStateFile();
    return true;
}

/*****************************************************************************/
/* 内部效果器（shell）                                                          */
/*****************************************************************************/
int CSingleHost::GetInternalCount() const
{
    return m_pPlugin ? m_pPlugin->GetInternalCount() : 0;
}

bool CSingleHost::GetInternalInfo(int idx, PluginInternalInfo *info) const
{
    return m_pPlugin && m_pPlugin->GetInternalInfo(idx, info);
}

int CSingleHost::GetCurrentInternal() const
{
    return m_pPlugin ? m_pPlugin->GetCurrentInternal() : -1;
}

bool CSingleHost::SwitchInternal(int idx, bool bSaveCurrent)
{
    if (!m_pPlugin)
        return false;

    /* 保存当前内部效果器状态（启动直选时旧内部效果器未被使用，不保存） */
    if (bSaveCurrent)
        SaveStateFile();

    if (!m_pPlugin->SwitchInternal(idx))
        return false;

    /* 内部效果器可能改名 -> 重算预设名，再恢复其状态 */
    UpdateStateBase();
    LoadStateFile();
    SaveLastUid();          /* 记住本次选择（跨会话恢复） */
    return true;
}

/*****************************************************************************/
/* last_uid：内部效果器上次选择（<exe>.ini [shell] lastuid，跨会话恢复）       */
/*****************************************************************************/
std::wstring CSingleHost::ConfigIniPath()
{
    return ExeDir() + GetExeBaseName() + L".ini";
}

void CSingleHost::SaveLastUid() const
{
    if (!m_pPlugin || m_pPlugin->GetInternalCount() <= 1)
        return;
    int cur = m_pPlugin->GetCurrentInternal();
    if (cur < 0)
        return;
    PluginInternalInfo info;
    if (!m_pPlugin->GetInternalInfo(cur, &info))
        return;
    wchar_t b[32];
    swprintf(b, 32, L"%lu", (unsigned long)info.uid);
    WritePrivateProfileStringW(L"shell", L"lastuid", b, ConfigIniPath().c_str());
}

int CSingleHost::LoadLastUid() const
{
    if (!m_pPlugin || m_pPlugin->GetInternalCount() <= 1)
        return -1;
    unsigned long uid = GetPrivateProfileIntW(L"shell", L"lastuid", 0,
                                              ConfigIniPath().c_str());
    if (uid == 0)
        return -1;
    int n = GetInternalCount();
    for (int i = 0; i < n; i++)
    {
        PluginInternalInfo info;
        if (GetInternalInfo(i, &info) && (unsigned long)info.uid == uid)
            return i;
    }
    return -1;
}

/*****************************************************************************/
/* .fxp 状态存取（按 exe 名 + uid 分档）                                       */
/*****************************************************************************/
void CSingleHost::SaveStateFile() const
{
    if (!m_pPlugin || m_szStateBase.empty())
        return;
    void *buf = NULL;
    int size = 0;
    if (!m_pPlugin->SaveState(buf, size) || !buf || size <= 0)
        return;

    std::wstring file = ExeDir() + m_szStateBase + L".fxp";
    WriteFileAtomic(file, buf, size);
    free(buf);
}

void CSingleHost::LoadStateFile()
{
    if (!m_pPlugin || m_szStateBase.empty())
        return;
    std::wstring file = ExeDir() + m_szStateBase + L".fxp";
    std::vector<unsigned char> data;
    if (ReadFileAll(file, data) && !data.empty())
        m_pPlugin->LoadState(data.data(), (int)data.size());
}
