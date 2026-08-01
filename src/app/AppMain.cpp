// AppMain.cpp : 应用入口
/******************************************************************************/
#include "pch.h"
#include "AppMain.h"
#include "MainWnd.h"

#include <windows.h>

#include <string>
#include <set>

#include "../host/SingleHost.h"
#include "../host/HostNaming.h"

CVsthostApp theApp;

CVsthostApp::CVsthostApp()
    : m_pHost(NULL)
{
}

CVsthostApp::~CVsthostApp()
{
}

/*===========================================================================*/
/* Shell 独立插件 / 快捷方式 exe 批量生成（类似 shell2vst）                   */
/*===========================================================================*/
enum GenMode
{
    GEN_EXE = 0,        /* (Shell名)内部名.exe 快捷方式（复制 shell 到同级） */
    GEN_DLL,            /* 独立 VST2 包装器 .dll */
    GEN_VST3            /* 独立 VST3 包装器 .vst3 */
};

/* 按名称关键词推断声道子文件夹（Waves 命名规范） */
static std::wstring ChannelFolder(const std::wstring &name)
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
    return L"其他";
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

/* 查找包装器模板：exe 同目录 wrapper\ 或向上两级 bin\wrapper\ */
static std::wstring FindWrapperTemplate(const wchar_t *name)
{
    std::wstring dir = ExeDir();
    std::wstring c = dir + L"wrapper\\" + name;
    if (GetFileAttributesW(c.c_str()) != INVALID_FILE_ATTRIBUTES)
        return c;
    c = dir + L"..\\..\\wrapper\\" + name;   /* bin\x64\Debug -> bin\wrapper */
    if (GetFileAttributesW(c.c_str()) != INVALID_FILE_ATTRIBUTES)
        return c;
    return L"";
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

/*****************************************************************************/
/* GenShellOutput : 批量生成 Shell 内部效果器的独立插件 / 快捷方式            */
/*   mode=GEN_EXE  : (Shell名)内部名.exe，并在各声道子文件夹复制 shell 同级   */
/*   mode=GEN_DLL  : 独立 VST2 包装器 .dll（wrapper 模板 + 同名 .ini）        */
/*   mode=GEN_VST3 : 独立 VST3 包装器 .vst3（wrapper 模板 + 同名 .ini）       */
/*****************************************************************************/
static bool GenShellOutput(const std::wstring &shellPath,
                           const std::wstring &outDir, GenMode mode)
{
    /* 当前 exe（快捷方式 / 模板来源） */
    wchar_t szExe[1024];
    GetModuleFileNameW(NULL, szExe, 1024);

    CSingleHost host;
    if (!host.LoadForEnum(shellPath))
    {
        AfxMessageBox(_T("无法加载该 Shell 模块。"), MB_OK | MB_ICONERROR);
        return false;
    }
    int n = host.GetInternalCount();
    if (n <= 0)
    {
        AfxMessageBox(_T("该模块没有可枚举的内部效果器（或不是 Shell）。"),
                      MB_OK | MB_ICONERROR);
        return false;
    }

    /* Shell 文件名/主干/扩展名 + 所在目录 */
    std::wstring shellFile = shellPath;
    size_t slash = shellFile.find_last_of(L"\\/");
    std::wstring shellDir = (slash == std::wstring::npos) ? L"" : shellFile.substr(0, slash + 1);
    if (slash != std::wstring::npos)
        shellFile = shellFile.substr(slash + 1);
    std::wstring shellBase = shellFile;
    size_t dot = shellBase.find_last_of(L'.');
    if (dot != std::wstring::npos)
        shellBase = shellBase.substr(0, dot);
    bool isVst3 = (GetExtLower(shellPath) == L".vst3");

    /* 输出目录（默认 = shell 同目录下 <shell主干> 文件夹） */
    std::wstring out = outDir;
    if (out.empty())
        out = shellDir + L"standalone_" + shellBase;
    CreateDirectoryW(out.c_str(), NULL);

    /* 独立包装器模板（exe 用自身；dll/vst3 用 bin\wrapper 下的模板） */
    std::wstring tmpl;
    if (mode == GEN_EXE)
        tmpl = szExe;
    else if (mode == GEN_DLL)
        tmpl = FindWrapperTemplate(L"shell2vst2.dll");
    else
        tmpl = FindWrapperTemplate(L"shell2vst3.dll");
    if (mode != GEN_EXE &&
        GetFileAttributesW(tmpl.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        AfxMessageBox(_T("未找到包装器模板（wrapper\\shell2vst2.dll / shell2vst3.dll）。\n"
                         "请先运行 tools\\build_wrappers.ps1。"),
                      MB_OK | MB_ICONERROR);
        return false;
    }

    /* 独立包装器/exe：ini 写 shell 绝对路径，shell 可放任意位置（不复制副本） */
    int count = 0, fail = 0;
    for (int i = 0; i < n; i++)
    {
        PluginInternalInfo info;
        if (!host.GetInternalInfo(i, &info) || !info.name[0])
            continue;
        std::wstring name = AnsiToWide(info.name);
        std::wstring folder = ChannelFolder(name);
        std::wstring sub = out + L"\\" + folder;
        CreateDirectoryW(sub.c_str(), NULL);

        if (mode == GEN_EXE)
        {
            /* 快捷方式 exe：ini 指定 shell 绝对路径（不复制 shell 副本） */
            std::wstring target = sub + L"\\" + MakeShellExeName(shellBase, name);
            if (!CopyFileW(tmpl.c_str(), target.c_str(), FALSE))
            {
                fail++;
                continue;
            }
            std::wstring ini = target + L".ini";
            WritePrivateProfileStringW(L"shell2vst", L"shell",
                                       shellPath.c_str(), ini.c_str());
            WritePrivateProfileStringW(L"shell2vst", L"name", name.c_str(), ini.c_str());
            count++;
        }
        else
        {
            std::wstring target = sub + L"\\" + SanitizeFileName(name) +
                                  (isVst3 ? L".vst3" : L".dll");
            if (!CopyFileW(tmpl.c_str(), target.c_str(), FALSE))
            {
                fail++;
                continue;
            }
            /* 写包装器配置（shell = 绝对路径，可放任意位置） */
            std::wstring ini = target + L".ini";
            WritePrivateProfileStringW(L"shell2vst", L"shell",
                                       shellPath.c_str(), ini.c_str());
            if (mode == GEN_DLL)
            {
                wchar_t uid[32];
                swprintf(uid, 32, L"0x%08lX", (unsigned long)info.uid);
                WritePrivateProfileStringW(L"shell2vst", L"uid", uid, ini.c_str());
            }
            else
            {
                WritePrivateProfileStringW(L"shell2vst", L"name", name.c_str(), ini.c_str());
            }
            count++;
        }
    }

    const wchar_t *what =
        (mode == GEN_EXE) ? L"快捷方式 exe" : (isVst3 ? L"独立 VST3 插件" : L"独立 VST2 插件");
    CString msg;
    msg.Format(_T("已为 [%s] 生成 %d 个%s（失败 %d）\n输出目录：%s"),
               shellBase.c_str(), count, what, fail, out.c_str());
    AfxMessageBox(msg, MB_OK | MB_ICONINFORMATION);
    return true;
}

/*****************************************************************************/
/* InitInstance : 程序初始化                                                 */
/*****************************************************************************/
BOOL CVsthostApp::InitInstance()
{
    /* 初始化 Common Controls（配合 app.manifest 启用 6.0） */
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    CWinApp::InitInstance();

    /* 命令行批量生成（处理后退出，不进入 UI）：
       --shell2vst <shell路径> [--out <输出目录>]
       --gen-shell-exes <shell路径> [--out <输出目录>] */
    {
        int argc = 0;
        LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv)
        {
            for (int i = 1; i < argc; i++)
            {
                if ((wcscmp(argv[i], L"--shell2vst") == 0 ||
                     wcscmp(argv[i], L"--gen-shell-exes") == 0) && i + 1 < argc)
                {
                    std::wstring shell = argv[i + 1];
                    std::wstring out;
                    /* 可选 --out <目录> */
                    for (int j = i + 2; j + 1 < argc; j++)
                        if (wcscmp(argv[j], L"--out") == 0)
                        {
                            out = argv[j + 1];
                            break;
                        }
                    GenMode mode = (argv[i][2] == L's') ? GEN_DLL : GEN_EXE;
                    if (GetExtLower(shell) == L".vst3")
                        mode = (argv[i][2] == L's') ? GEN_VST3 : GEN_EXE;
                    GenShellOutput(shell, out, mode);
                    LocalFree(argv);
                    return FALSE;   /* 批量生成后退出 */
                }
            }
            LocalFree(argv);
        }
    }

    m_pHost = new CSingleHost;

    // 单插件宿主主窗口（SDI）
    CMainFrame *pFrame = new CMainFrame;
    if (!pFrame->Create())
    {
        delete pFrame;
        delete m_pHost;
        m_pHost = NULL;
        return FALSE;
    }
    m_pMainWnd = pFrame;
    pFrame->SetHost(m_pHost);

    pFrame->ShowWindow(m_nCmdShow);
    pFrame->UpdateWindow();

    // 命令行显式插件路径（计划书 §5.1 规则 1）
    CCommandLineInfo cmdInfo;
    ParseCommandLine(cmdInfo);
    if (cmdInfo.m_nShellCommand == CCommandLineInfo::FileOpen &&
        !cmdInfo.m_strFileName.IsEmpty())
        pFrame->DoLoad(cmdInfo.m_strFileName.GetString());
    else
        pFrame->AutoLoad();         // 同名自动加载（计划书 §5.1 规则 2）
    return TRUE;
}

/*****************************************************************************/
/* ExitInstance : 退出前保存状态并清理                                        */
/*****************************************************************************/
int CVsthostApp::ExitInstance()
{
    if (m_pHost)
    {
        if (m_pHost->IsLoaded())
            m_pHost->SaveStateFile();
        m_pHost->Close();
        delete m_pHost;
        m_pHost = NULL;
    }
    return CWinApp::ExitInstance();
}
