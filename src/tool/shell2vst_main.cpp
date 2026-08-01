// shell2vst_main.cpp : 独立 Shell 拆包工具（与 vsthost 主程序完全解耦）
//   由 <shell路径> 枚举内部效果器，批量生成独立插件 / 快捷方式：
//
//   用法：
//     shell2vst.exe <shell路径> [--out <输出目录>] [--exe|--dll|--vst3|--all]
//                   [--host <宿主exe模板>]
//
//   生成物（按声道子文件夹分类：Mono / Stereo / 5.x / 7.x / Quad / 9.x / Other）：
//     --exe   (Shell名)内部名.exe 快捷方式（模板 = --host 指定，否则同目录 vsthost*.exe）
//     --dll   独立 VST2 包装器 .dll（模板 = wrapper\shell2vst2.dll + 同名 .ini[shell,uid]）
//     --vst3  独立 VST3 包装器 .vst3（模板 = wrapper\shell2vst3.dll + 同名 .ini[shell,name]）
//
//   默认（未指定任何模式）为 --all。退出码：0 成功 / 1 失败 / 2 参数错误。
/******************************************************************************/
#include <windows.h>
#include <clocale>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

#include "../host/SingleHost.h"
#include "../host/HostNaming.h"

namespace {

/* 当前 exe 所在目录（带结尾反斜杠） */
std::wstring ExeDir()
{
    wchar_t sz[1024];
    GetModuleFileNameW(NULL, sz, 1024);
    std::wstring p = sz;
    size_t s = p.find_last_of(L"\\/");
    return (s == std::wstring::npos) ? L"" : p.substr(0, s + 1);
}

/* 按名称关键词推断声道子文件夹（Waves 命名规范） */
std::wstring ChannelFolder(const std::wstring &name)
{
    if (name.find(L"9.") != std::wstring::npos) return L"9.x";
    if (name.find(L"7.") != std::wstring::npos) return L"7.x";
    if (name.find(L"5.") != std::wstring::npos) return L"5.x";
    if (name.find(L"Quad") != std::wstring::npos) return L"Quad";
    if (name.find(L"Stereo") != std::wstring::npos ||
        name.find(L"2.0") != std::wstring::npos ||
        name.find(L"2.1") != std::wstring::npos) return L"Stereo";
    if (name.find(L"Mono") != std::wstring::npos ||
        name.find(L"1.0") != std::wstring::npos) return L"Mono";
    return L"Other";
}

/* 取扩展名（小写） */
std::wstring GetExtLower(const std::wstring &path)
{
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return L"";
    std::wstring ext = path.substr(dot);
    for (size_t i = 0; i < ext.size(); i++)
        if (ext[i] >= L'A' && ext[i] <= L'Z')
            ext[i] = (wchar_t)(ext[i] - L'A' + L'a');
    return ext;
}

/* 查找模板：依次尝试 dir\name、dir\wrapper\name、dir\..\..\wrapper\name */
std::wstring FindTemplate(const std::wstring &dir, const wchar_t *name)
{
    std::wstring c = dir + name;
    if (GetFileAttributesW(c.c_str()) != INVALID_FILE_ATTRIBUTES) return c;
    c = dir + L"wrapper\\" + name;
    if (GetFileAttributesW(c.c_str()) != INVALID_FILE_ATTRIBUTES) return c;
    c = dir + L"..\\..\\wrapper\\" + name;   /* bin\x64\Debug -> bin\wrapper */
    if (GetFileAttributesW(c.c_str()) != INVALID_FILE_ATTRIBUTES) return c;
    return L"";
}

/* 查找宿主 exe 模板（快捷方式 exe 用）：同目录 vsthost*.exe，取第一个 */
std::wstring FindHostExe(const std::wstring &dir)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"vsthost*.exe").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return L"";
    FindClose(h);
    return dir + fd.cFileName;
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
    /* wprintf 输出中文必须启用系统 locale（默认 "C" locale 无法转换宽字符） */
    setlocale(LC_ALL, "");

    std::wstring shell, out, hostExe;
    bool bExe = false, bDll = false, bVst3 = false;

    for (int i = 1; i < argc; i++)
    {
        if (wcscmp(argv[i], L"--out") == 0 && i + 1 < argc) out = argv[++i];
        else if (wcscmp(argv[i], L"--host") == 0 && i + 1 < argc) hostExe = argv[++i];
        else if (wcscmp(argv[i], L"--exe") == 0) bExe = true;
        else if (wcscmp(argv[i], L"--dll") == 0) bDll = true;
        else if (wcscmp(argv[i], L"--vst3") == 0) bVst3 = true;
        else if (wcscmp(argv[i], L"--all") == 0) bExe = bDll = bVst3 = true;
        else if (shell.empty() && argv[i][0] != L'-') shell = argv[i];
        else { wprintf(L"未知参数: %s\n", argv[i]); return 2; }
    }

    if (shell.empty())
    {
        fwprintf(stderr, L"用法: shell2vst.exe <shell路径> [--out <目录>] "
                        L"[--exe|--dll|--vst3|--all] [--host <宿主exe>]\n");
        return 2;
    }
    if (!bExe && !bDll && !bVst3) bExe = bDll = bVst3 = true;   /* 默认 --all */

    /* shell 路径规范化为绝对路径：写入 .ini 的始终为绝对路径，
       wrapper 与主程序快捷方式 exe 均可直接使用（主程序不解析相对路径） */
    wchar_t absPath[2048];
    if (GetFullPathNameW(shell.c_str(), 2048, absPath, NULL))
        shell = absPath;

    CSingleHost host;
    if (!host.LoadForEnum(shell))
    {
        fwprintf(stderr, L"错误: 无法加载 Shell 模块: %s (lastError=%lu)\n",
                 shell.c_str(), (unsigned long)GetLastError());
        return 1;
    }
    int n = host.GetInternalCount();
    fwprintf(stderr, L"已加载 %s，枚举到 %d 个内部效果器\n", shell.c_str(), n);
    if (n <= 0)
    {
        wprintf(L"错误: 该模块没有可枚举的内部效果器（或不是 Shell）\n");
        return 1;
    }

    /* Shell 文件名 / 主干 / 扩展名 + 所在目录 */
    std::wstring shellFile = shell;
    size_t slash = shellFile.find_last_of(L"\\/");
    std::wstring shellDir = (slash == std::wstring::npos) ? L"" : shellFile.substr(0, slash + 1);
    if (slash != std::wstring::npos) shellFile = shellFile.substr(slash + 1);
    std::wstring shellBase = shellFile;
    size_t dot = shellBase.find_last_of(L'.');
    if (dot != std::wstring::npos) shellBase = shellBase.substr(0, dot);
    bool isVst3 = (GetExtLower(shell) == L".vst3");
    (void)isVst3;

    /* 输出目录（默认 = shell 同目录 standalone_<shell主干>） */
    std::wstring outDir = out;
    if (outDir.empty()) outDir = shellDir + L"standalone_" + shellBase;
    CreateDirectoryW(outDir.c_str(), NULL);

    /* 模板 */
    std::wstring dir = ExeDir();
    std::wstring tmplExe = hostExe.empty() ? FindHostExe(dir) : hostExe;
    std::wstring tmplDll = FindTemplate(dir, L"shell2vst2.dll");
    std::wstring tmplVst3 = FindTemplate(dir, L"shell2vst3.dll");
    if (bExe && tmplExe.empty())
    {
        wprintf(L"错误: 未找到宿主 exe 模板（用 --host 指定或放到工具同目录）\n");
        return 1;
    }
    if (bDll && tmplDll.empty())
    {
        wprintf(L"错误: 未找到 wrapper\\shell2vst2.dll 模板（先运行 tools\\build_wrappers.ps1）\n");
        return 1;
    }
    if (bVst3 && tmplVst3.empty())
    {
        wprintf(L"错误: 未找到 wrapper\\shell2vst3.dll 模板（先运行 tools\\build_wrappers.ps1）\n");
        return 1;
    }

    int count = 0, fail = 0;
    for (int i = 0; i < n; i++)
    {
        PluginInternalInfo info;
        if (!host.GetInternalInfo(i, &info) || !info.name[0]) continue;
        std::wstring name = AnsiToWide(info.name);
        std::wstring folder = ChannelFolder(name);
        std::wstring sub = outDir + L"\\" + folder;
        CreateDirectoryW(sub.c_str(), NULL);

        if (bExe)
        {
            std::wstring target = sub + L"\\" + MakeShellExeName(shellBase, name);
            if (!CopyFileW(tmplExe.c_str(), target.c_str(), FALSE)) { fail++; continue; }
            std::wstring ini = target + L".ini";
            WritePrivateProfileStringW(L"shell2vst", L"shell", shell.c_str(), ini.c_str());
            WritePrivateProfileStringW(L"shell2vst", L"name", name.c_str(), ini.c_str());
            wprintf(L"  exe : %s\n", target.c_str());
            count++;
        }
        if (bDll)
        {
            std::wstring target = sub + L"\\" + SanitizeFileName(name) + L".dll";
            if (!CopyFileW(tmplDll.c_str(), target.c_str(), FALSE)) { fail++; continue; }
            std::wstring ini = target + L".ini";
            WritePrivateProfileStringW(L"shell2vst", L"shell", shell.c_str(), ini.c_str());
            wchar_t uid[32];
            swprintf(uid, 32, L"0x%08lX", (unsigned long)info.uid);
            WritePrivateProfileStringW(L"shell2vst", L"uid", uid, ini.c_str());
            wprintf(L"  dll : %s\n", target.c_str());
            count++;
        }
        if (bVst3)
        {
            std::wstring target = sub + L"\\" + SanitizeFileName(name) + L".vst3";
            if (!CopyFileW(tmplVst3.c_str(), target.c_str(), FALSE)) { fail++; continue; }
            std::wstring ini = target + L".ini";
            WritePrivateProfileStringW(L"shell2vst", L"shell", shell.c_str(), ini.c_str());
            WritePrivateProfileStringW(L"shell2vst", L"name", name.c_str(), ini.c_str());
            wprintf(L"  vst3: %s\n", target.c_str());
            count++;
        }
    }

    wprintf(L"完成: 为 [%s] 生成 %d 个（失败 %d）\n输出目录: %s\n",
            shellBase.c_str(), count, fail, outDir.c_str());
    return 0;
}
