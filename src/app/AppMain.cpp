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
