// shell2vst3.cpp : VST3 Shell 独立包装器模板（预编译为单文件 .vst3 模块）
//
// 运行时从「同目录同名 .ini」读取配置并只暴露 shell 中的指定内部效果器：
//   [shell2vst]
//   shell=WaveShell1-VST3 16.7_x64.vst3   （shell 模块文件名，相对本目录或绝对路径）
//   name=API-560 Stereo                    （目标内部效果器名称，shell 内唯一）
//
// 原理：实现 IPluginFactory 代理，把 shell 的 factory 包起来，classInfos 只暴露
// 名称匹配的那一个 class；createInstance 仅放行该 class。
/******************************************************************************/
#include <windows.h>
#include <cstring>
#include <cstdio>

#pragma comment(lib, "ole32.lib")   /* CoCreateGuid（FUID::generate） */

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/funknownimpl.h"

static char              g_shellPath[1024] = "";
static char              g_name[256] = "";
static wchar_t           g_shellPathW[1024] = L"";
static wchar_t           g_nameW[256] = L"";
static HMODULE           g_self = NULL;   /* 本 DLL 自身模块句柄 */

static void DbLog(const char *fmt, ...) { (void)fmt; }   /* 调试日志（已禁用） */

static HINSTANCE         g_shell = NULL;
static Steinberg::IPluginFactory *g_shellFactory = NULL;

/* 目标 class 的 16 字节 UID（从 shell 按名称匹配后缓存） */
static Steinberg::TUID   g_cid = {};
static bool              g_haveCid = false;

/*****************************************************************************/
/* IPluginFactory 代理：只暴露目标内部效果器                                  */
/*****************************************************************************/
class CProxyFactory : public Steinberg::IPluginFactory
{
public:
    CProxyFactory() : m_found(false), m_targetIndex(0)
    {
        FUNKNOWN_CTOR
    }

    Steinberg::tresult PLUGIN_API getFactoryInfo(Steinberg::PFactoryInfo *info) override
    {
        if (!g_shellFactory || !info)
            return Steinberg::kResultFalse;
        return g_shellFactory->getFactoryInfo(info);
    }

    Steinberg::int32 PLUGIN_API countClasses() override
    {
        if (!g_shellFactory)
            return 0;
        return FindTarget() ? 1 : 0;
    }

    Steinberg::tresult PLUGIN_API getClassInfo(Steinberg::int32 index,
                                               Steinberg::PClassInfo *info) override
    {
        if (!g_shellFactory || !info || index != 0)
            return Steinberg::kResultFalse;
        if (!FindTarget())
            return Steinberg::kResultFalse;
        return g_shellFactory->getClassInfo(m_targetIndex, info);
    }

    Steinberg::tresult PLUGIN_API createInstance(Steinberg::FIDString cid,
                                                 Steinberg::FIDString iid,
                                                 void **obj) override
    {
        if (!g_shellFactory)
            return Steinberg::kNoInterface;
        if (!FindTarget())
            return Steinberg::kNoInterface;
        if (cid && memcmp(cid, g_cid, 16) != 0)
            return Steinberg::kNoInterface;
        return g_shellFactory->createInstance(cid, iid, obj);
    }

    DECLARE_FUNKNOWN_METHODS

private:
    /* 在 shell factory 里按名称找目标 class，缓存其 index 与 UID */
    bool FindTarget()
    {
        if (m_found)
            return true;
        DbLog("FindTarget: begin factory=%p name='%s'", (void*)g_shellFactory, g_name);
        if (!g_shellFactory || !g_name[0])
        {
            DbLog("FindTarget: no factory/name");
            return false;
        }

        int n = 0;
        __try
        {
            n = g_shellFactory->countClasses();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            DbLog("FindTarget: countClasses EXCEPTION code=0x%08X",
                  (unsigned)GetExceptionCode());
            return false;
        }
        DbLog("FindTarget: shell classes=%d, want='%s'", n, g_name);
        for (int i = 0; i < n; i++)
        {
            Steinberg::PClassInfo ci;
            Steinberg::tresult rc = Steinberg::kResultFalse;
            __try
            {
                rc = g_shellFactory->getClassInfo(i, &ci);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                DbLog("FindTarget: getClassInfo[%d] EXCEPTION", i);
                continue;
            }
            if (rc == Steinberg::kResultOk)
            {
                if (strcmp(ci.name, g_name) == 0)
                {
                    memcpy(g_cid, ci.cid, 16);
                    g_haveCid = true;
                    m_targetIndex = i;
                    m_found = true;
                    DbLog("FindTarget: matched[%d] cid=%02X%02X%02X%02X...", i,
                          ci.cid[0], ci.cid[1], ci.cid[2], ci.cid[3]);
                    return true;
                }
            }
        }
        DbLog("FindTarget: not found");
        return false;
    }

    bool m_found;
    Steinberg::int32 m_targetIndex;
};
IMPLEMENT_FUNKNOWN_METHODS(CProxyFactory, Steinberg::IPluginFactory,
                           Steinberg::IPluginFactory::iid)

static CProxyFactory g_proxy;

/*****************************************************************************/
/* 读配置（同目录同名 .ini，UTF-16，用宽字符 API） */
static void LoadConfig()
{
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(g_self, self, MAX_PATH);

    wchar_t ini[MAX_PATH];
    wcscpy_s(ini, self);
    wcscat_s(ini, L".ini");   /* 同目录同名 + .ini（如 X.vst3.ini） */

    GetPrivateProfileStringW(L"shell2vst", L"shell", L"", g_shellPathW, 1024, ini);
    GetPrivateProfileStringW(L"shell2vst", L"name", L"", g_nameW, 256, ini);

    WideCharToMultiByte(CP_ACP, 0, g_shellPathW, -1, g_shellPath, 1024, NULL, NULL);
    WideCharToMultiByte(CP_ACP, 0, g_nameW, -1, g_name, 256, NULL, NULL);
}

/* 相对 shell 路径解析为相对本模块目录的绝对路径 */
static void ResolveShellPath()
{
    if (!g_shellPath[0])
        return;
    if (g_shellPath[0] == '\\' || g_shellPath[0] == '/' || strchr(g_shellPath, ':'))
        return;

    wchar_t self[MAX_PATH];
    GetModuleFileNameW(g_self, self, MAX_PATH);
    wchar_t *slash = wcsrchr(self, L'\\');
    if (!slash)
        return;
    *slash = L'\0';
    wchar_t tmp[1200];
    _snwprintf(tmp, 1200, L"%s\\%S", self, g_shellPath);
    *slash = L'\\';
    WideCharToMultiByte(CP_ACP, 0, tmp, -1, g_shellPath, 1024, NULL, NULL);

    /* 规范化（消除 ..\ 段） */
    char full[MAX_PATH];
    if (GetFullPathNameA(g_shellPath, MAX_PATH, full, NULL))
        strncpy(g_shellPath, full, 1024);
    DbLog("ResolveShellPath: '%s'", g_shellPath);
}

/*****************************************************************************/
/* VST3 模块入口                                                              */
/*****************************************************************************/
extern "C" __declspec(dllexport) bool InitDll()
{
    LoadConfig();
    ResolveShellPath();
    DbLog("InitDll: shell='%s' name='%s'", g_shellPath, g_name);

    if (g_shell)
    {
        FreeLibrary(g_shell);
        g_shell = NULL;
        g_shellFactory = NULL;
    }
    g_shell = LoadLibraryA(g_shellPath);
    if (!g_shell)
    {
        DbLog("InitDll: LoadLibrary FAILED err=%lu", (unsigned long)GetLastError());
        return false;
    }
    DbLog("InitDll: LoadLibrary OK h=%p", (void*)g_shell);
    /* 与 vst3sdk 宿主一致：先 InitDll（可选）再 GetPluginFactory */
    if (auto shellInit = (bool (*)())GetProcAddress(g_shell, "InitDll"))
    {
        if (!shellInit())
        {
            DbLog("InitDll: shell InitDll failed");
            return false;
        }
    }
    auto getFactory = (Steinberg::IPluginFactory * (*)())
        GetProcAddress(g_shell, "GetPluginFactory");
    if (!getFactory)
    {
        DbLog("InitDll: no GetPluginFactory in shell");
        return false;
    }
    DbLog("InitDll: getFactory ptr=%p", (void*)getFactory);
    __try
    {
        g_shellFactory = getFactory();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        DbLog("InitDll: getFactory EXCEPTION code=0x%08X", (unsigned)GetExceptionCode());
        return false;
    }
    DbLog("InitDll: shellFactory=%p", (void*)g_shellFactory);
    return g_shellFactory != NULL;
}

extern "C" __declspec(dllexport) bool ExitDll()
{
    if (g_shell)
    {
        FreeLibrary(g_shell);
        g_shell = NULL;
        g_shellFactory = NULL;
    }
    return true;
}

extern "C" __declspec(dllexport) Steinberg::IPluginFactory *PLUGIN_API GetPluginFactory()
{
    return &g_proxy;
}

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        g_self = hModule;
    if (reason == DLL_PROCESS_DETACH && g_shell)
    {
        FreeLibrary(g_shell);
        g_shell = NULL;
        g_shellFactory = NULL;
    }
    return TRUE;
}
