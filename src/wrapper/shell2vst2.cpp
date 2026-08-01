// shell2vst2.cpp : VST2 Shell 独立包装器模板（预编译为 DLL，约 10KB）
//
// 运行时从「同目录同名 .ini」读取配置并加载 shell 中的指定内部效果器：
//   [shell2vst]
//   shell=WaveShell1-VST 16.6_x64.dll     （shell 文件名，相对本目录或绝对路径）
//   uid=0x56535430                        （目标内部效果器 UID）
//
// 原理：包装 audioMaster 回调，在插件查询 audioMasterCurrentId 时返回目标 UID，
// 使 shell 的 VSTPluginMain 实例化对应内部效果器；其余 opcode 转发给宿主。
/******************************************************************************/
#include <windows.h>
#include <cstring>
#include <cstdio>

#include "pluginterfaces/vst2.x/aeffectx.h"

static HINSTANCE             g_shell = NULL;
static AEffect *(*g_shellMain)(audioMasterCallback) = NULL;
static audioMasterCallback   g_hostAudioMaster = NULL;
static VstIntPtr             g_uid = 0;
static char                  g_shellPath[1024] = "";
static wchar_t               g_shellPathW[1024] = L"";
static HMODULE               g_self = NULL;   /* 本 DLL 自身模块句柄 */

/* 包装的 audioMaster：当前 UID 查询返回目标内部效果器 UID，其余转发宿主 */
static VstIntPtr VSTCALLBACK WrapperAudioMaster(AEffect *effect, VstInt32 opcode,
                                                VstInt32 index, VstIntPtr value,
                                                void *ptr, float opt)
{
    if (opcode == audioMasterCurrentId)
        return g_uid;
    if (g_hostAudioMaster)
        return g_hostAudioMaster(effect, opcode, index, value, ptr, opt);
    return 0;
}

/* 读配置（同目录同名 .ini，UTF-16，用宽字符 API） */
static void LoadConfig()
{
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);

    wchar_t ini[MAX_PATH];
    wcscpy_s(ini, self);
    wcscat_s(ini, L".ini");   /* 同目录同名 + .ini（如 X.dll.ini） */

    GetPrivateProfileStringW(L"shell2vst", L"shell", L"", g_shellPathW, 1024, ini);
    WideCharToMultiByte(CP_ACP, 0, g_shellPathW, -1, g_shellPath, 1024, NULL, NULL);
    g_uid = (VstIntPtr)(VstInt32)GetPrivateProfileIntW(L"shell2vst", L"uid", 0, ini);
}

/* 把相对 shell 路径解析为相对本 DLL 目录的绝对路径 */
static void ResolveShellPath()
{
    if (!g_shellPath[0])
        return;
    /* 已是绝对路径 */
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
}

/*****************************************************************************/
/* VST2 入口                                                                  */
/*****************************************************************************/
extern "C" __declspec(dllexport) AEffect *VSTPluginMain(audioMasterCallback host)
{
    LoadConfig();
    ResolveShellPath();
    g_hostAudioMaster = host;

    if (g_shell)
    {
        FreeLibrary(g_shell);
        g_shell = NULL;
    }
    g_shell = LoadLibraryA(g_shellPath);
    if (!g_shell)
        return NULL;
    g_shellMain = (AEffect *(*)(audioMasterCallback))GetProcAddress(g_shell, "VSTPluginMain");
    if (!g_shellMain)
        g_shellMain = (AEffect *(*)(audioMasterCallback))GetProcAddress(g_shell, "main");
    if (!g_shellMain)
        return NULL;

    AEffect *effect = g_shellMain(WrapperAudioMaster);
    if (!effect || effect->magic != kEffectMagic)
        return NULL;
    return effect;
}

/* VST2 旧式入口别名：main -> VSTPluginMain（避免 CRT main 冲突，用链接器导出） */
#pragma comment(linker, "/EXPORT:main=VSTPluginMain")

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        g_self = hModule;
    if (reason == DLL_PROCESS_DETACH && g_shell)
    {
        FreeLibrary(g_shell);
        g_shell = NULL;
    }
    return TRUE;
}
