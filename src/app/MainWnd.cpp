// MainWnd.cpp : 主窗口实现（M1/M2）
/******************************************************************************/
#include "pch.h"
#include "MainWnd.h"

#include <windows.h>

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

#include "../host/SingleHost.h"
#include "../host/IPlugin.h"
#include "../host/HostNaming.h"
#include "../host/AsioBackend.h"
#include "../host/JackBackend.h"

#include <math.h>
#include <cstdio>
#include "../ui/AsioMapDialog.h"
#include "../ui/ClosePromptDlg.h"
#include "../ui/GlobalSettingsDlg.h"
#include "../ui/LevelMeterDlg.h"
#include "../ui/MeterSettingsDlg.h"
#include "../ui/MidiMapDialog.h"
#include "../ui/loudness_std.h"
#include "../host/MidiInput.h"

#include <set>
#include <vector>

/* rand_s：Windows 系统安全随机（基于 RtlGenRandom），在此显式声明
   （避免 _CRT_RAND_S / <stdlib.h> 包含顺序问题） */
extern "C" int __cdecl rand_s(unsigned *randomValue);

/*****************************************************************************/
/* JACK 客户端名：超过 27 字节时中间省略（...），保证末尾数字序号后缀        */
/* （_1、_2…）始终可见；截断按 UTF-8 边界进行，避免切半多字节字符。          */
/*****************************************************************************/
static std::string TruncateJackClientName(const std::string &name)
{
    const size_t kMaxLen = 27;      /* JACK 客户端名上限（含结尾 NUL） */
    const char *kEllipsis = "...";
    const size_t kEllLen = 3;

    if (name.size() <= kMaxLen)
        return name;

    /* 数字序号后缀：末尾最后一个 "_" 之后全是数字（如 "_1"、"_12"） */
    size_t numStart = name.size();
    while (numStart > 0 && name[numStart - 1] >= '0' && name[numStart - 1] <= '9')
        numStart--;
    bool hasSuffix = (numStart < name.size() && numStart > 0 && name[numStart - 1] == '_');
    size_t suffixStart = hasSuffix ? numStart - 1 : name.size();   /* 指向 "_" */
    std::string suffix = name.substr(suffixStart);                 /* 完整保留 */

    /* 剩余空间 = 上限 - 后缀 - 省略号，头部/尾部各半 */
    size_t budget = (kMaxLen > suffix.size() + kEllLen)
                        ? kMaxLen - suffix.size() - kEllLen : 1;
    size_t tailMax = budget / 2;
    size_t tailLen = (tailMax < suffixStart) ? tailMax : suffixStart;

    /* 尾部起点须落在字符边界（向前回退到非续字节） */
    size_t tailStart = suffixStart - tailLen;
    while (tailStart < suffixStart &&
           ((unsigned char)name[tailStart] & 0xC0) == 0x80)
        tailStart++;

    /* 头部截断：不切半多字节字符，并去掉尾部空白 */
    size_t headLen = budget - tailLen;
    size_t n = (headLen < name.size()) ? headLen : name.size();
    while (n > 0 && ((unsigned char)name[n] & 0xC0) == 0x80)
        n--;
    while (n > 0 && (name[n - 1] == ' ' || name[n - 1] == '\t'))
        n--;

    return name.substr(0, n) + kEllipsis + name.substr(tailStart);
}

/*****************************************************************************/
/* JACK 客户端名缩写：按空白拆词，每词取首字母（大写），词内 "_数字/点" 段  */
/* 原样保留（如 Stereo_7.1 -> S_7.1），词间用 "_" 连接，末尾序号后缀保留：  */
/*   "DTS Neural UpMix Stereo_7.1_1" -> "D_N_U_S_7.1_1"                      */
/*****************************************************************************/
static std::string AbbrevWord(const char *s, size_t len)
{
    std::string r;
    if (len == 0)
        return r;

    /* 取首个字符作为缩写核心（字母转大写，数字/符号原样，中文取整字符） */
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80 && ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')))
        r += (char)(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
    else if (c < 0x80)
        r += (char)c;
    else
    {
        int clen = 1;
        if ((c & 0xE0) == 0xC0) clen = 2;
        else if ((c & 0xF0) == 0xE0) clen = 3;
        else if ((c & 0xF8) == 0xF0) clen = 4;
        r.append(s, (size_t)clen);
    }

    /* 词内 "_数字/点" 段原样保留（如 "_7.1"） */
    size_t i = 1;
    while (i < len)
    {
        if (s[i] == '_')
        {
            size_t j = i + 1;
            while (j < len && ((s[j] >= '0' && s[j] <= '9') || s[j] == '.'))
                j++;
            if (j > i + 1)
            {
                r.append(s + i, j - i);
                i = j;
                continue;
            }
        }
        i++;
    }
    return r;
}

static std::string AbbreviateClientName(const std::string &name)
{
    /* 末尾数字序号后缀（"_1" 等）完整保留 */
    size_t numStart = name.size();
    while (numStart > 0 && name[numStart - 1] >= '0' && name[numStart - 1] <= '9')
        numStart--;
    bool hasSuffix = (numStart < name.size() && numStart > 0 && name[numStart - 1] == '_');
    size_t suffixStart = hasSuffix ? numStart - 1 : name.size();
    std::string suffix = name.substr(suffixStart);

    std::string out;
    size_t i = 0;
    size_t end = suffixStart;
    while (i < end)
    {
        while (i < end && (name[i] == ' ' || name[i] == '\t' ||
                           name[i] == '\r' || name[i] == '\n'))
            i++;
        if (i >= end)
            break;
        size_t j = i;
        while (j < end && name[j] != ' ' && name[j] != '\t' &&
               name[j] != '\r' && name[j] != '\n')
            j++;
        std::string ab = AbbrevWord(name.c_str() + i, j - i);
        if (!ab.empty())
        {
            if (!out.empty())
                out += '_';
            out += ab;
        }
        i = j;
    }
    return out + suffix;
}

/* Client_ 真随机兜底名：完全真随机生成 20 位小写字母（7 + 20 = 27 字节，
   恰好 ≤ 上限），不依赖插件名；同一插件实例名字的固定性由 ini 持久化
   映射（首次生成后读 ini 复用）保证 */
static std::string RandomClientName()
{
    const char *alphabet = "abcdefghijklmnopqrstuvwxyz";
    std::string r = "Client_";
    for (int i = 0; i < 20; i++)
    {
        unsigned v = 0;
        if (rand_s(&v) != 0)
            v = (unsigned)::GetTickCount() + (unsigned)i;   /* 理论不会失败 */
        r += alphabet[v % 26];
    }
    return r;
}

/* JACK 客户端名 fallback：全名 → 省略号 → 缩写 → 真随机兜底。
   超 27 字节或撞名（used 已包含）即降级下一级；随机兜底完全真随机
   （撞名时重新生成，至多 8 次），名字固定性由 ini 持久化保证。 */
static std::string MakeJackClientName(const std::string &fullName,
                                      const std::set<std::string> &used)
{
    const size_t kMaxLen = 27;      /* JACK 客户端名上限（含结尾 NUL） */
    auto inUse = [&used](const std::string &n)
    { return used.find(n) != used.end(); };

    /* 1. 全名 */
    if (fullName.size() <= kMaxLen && !inUse(fullName))
        return fullName;

    /* 2. 省略号 */
    {
        std::string el = TruncateJackClientName(fullName);
        if (!el.empty() && el.size() <= kMaxLen && !inUse(el))
            return el;
    }

    /* 3. 缩写 */
    {
        std::string ab = AbbreviateClientName(fullName);
        if (!ab.empty() && ab.size() <= kMaxLen && !inUse(ab))
            return ab;
    }

    /* 4. 真随机兜底（撞名时重新生成，至多 8 次） */
    for (int k = 0; k < 8; k++)
    {
        std::string rn = RandomClientName();
        if (!inUse(rn))
            return rn;
    }
    return RandomClientName();
}

/*****************************************************************************/
/* DWM 系统风格（计划书 §5.9.1）                                              */
/*****************************************************************************/
/* 读取系统“应用使用深色模式”设置（0=深色） */
static bool IsSystemDark()
{
    DWORD v = 1, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &v, &sz) == ERROR_SUCCESS)
        return v == 0;
    return false;
}

/* 应用 DWM 属性：深色标题栏 + Win11 圆角（失败一律静默降级） */
static void ApplySystemStyle(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
        return;

    BOOL dark = IsSystemDark() ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    int corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
}

void CPluginHostView::OnPaint()
{
    CPaintDC dc(this);
    CRect rc;
    GetClientRect(&rc);

    /* 占位文字用当前系统 UI 字体（与主界面一致） */
    CFont *pf = GetFont();
    CFont *pOld = pf ? dc.SelectObject(pf) : NULL;

    /* 占位面板深色适配 */
    if (IsSystemDark())
    {
        dc.FillSolidRect(rc, RGB(32, 32, 32));
        dc.SetTextColor(RGB(200, 200, 200));
    }
    else
    {
        dc.FillSolidRect(rc, ::GetSysColor(COLOR_BTNFACE));
        dc.SetTextColor(::GetSysColor(COLOR_BTNTEXT));
    }
    dc.SetBkMode(TRANSPARENT);
    CString s = _T("vsthost —— 插件编辑器将嵌入此区域");
    dc.DrawText(s, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (pOld)
        dc.SelectObject(pOld);
}

/*****************************************************************************/
/* CPluginHostView : 插件编辑器宿主窗格                                        */
/*****************************************************************************/
BEGIN_MESSAGE_MAP(CPluginHostView, CWnd)
    ON_WM_PAINT()
END_MESSAGE_MAP()

BOOL CPluginHostView::Create(CWnd *pParent, UINT nID)
{
    CRect rc(0, 0, 0, 0);
    return CWnd::Create(NULL, _T(""),
                        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_BORDER,
                        rc, pParent, nID);
}

/*****************************************************************************/
/* CMainFrame : 主框架窗口                                                    */
/*****************************************************************************/
BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_WM_TIMER()
    ON_WM_DROPFILES()
    ON_WM_SETTINGCHANGE()
    ON_WM_PAINT()
    ON_WM_INITMENUPOPUP()
    ON_WM_CLOSE()
    ON_WM_SYSCOMMAND()
    ON_MESSAGE(WM_TRAYICON, &CMainFrame::OnTrayIcon)
    ON_COMMAND(IDM_FILE_OPEN, &CMainFrame::OnFileOpen)
    ON_COMMAND(IDM_FILE_CLOSE, &CMainFrame::OnFileClose)
    ON_COMMAND(IDM_FILE_SAVE_EXE, &CMainFrame::OnFileSaveExe)
    ON_COMMAND(IDM_FILE_EXIT, &CMainFrame::OnFileExit)
    ON_COMMAND(IDM_FILE_SETTINGS, &CMainFrame::OnFileSettings)
    ON_COMMAND(IDM_TRAY_SHOW, &CMainFrame::OnTrayShow)
    ON_COMMAND(IDM_TRAY_EXIT, &CMainFrame::OnTrayExit)
    ON_COMMAND(IDM_APP_ABOUT, &CMainFrame::OnAppAbout)
    ON_COMMAND(IDM_APP_ABOUT_PLUGIN, &CMainFrame::OnAppAboutPlugin)
    ON_COMMAND(IDM_PLUGIN_TESTPROC, &CMainFrame::OnPluginTestProc)
    ON_COMMAND(IDM_PLUGIN_SINETEST, &CMainFrame::OnPluginSineTest)
    ON_COMMAND(IDM_PLUGIN_MIDIMAP, &CMainFrame::OnPluginMidiMap)
    ON_COMMAND_RANGE(IDM_INTERNAL_BASE, IDM_INTERNAL_MAX, &CMainFrame::OnInternalSelect)
    ON_COMMAND_RANGE(IDM_AUDIO_BASE, IDM_AUDIO_MAX, &CMainFrame::OnAsioSelect)
    ON_COMMAND(IDM_AUDIO_CPANEL, &CMainFrame::OnAsioControlPanel)
    ON_COMMAND(IDM_AUDIO_REFRESH, &CMainFrame::OnAsioRefresh)
    ON_COMMAND(IDM_AUDIO_MAP, &CMainFrame::OnAsioMap)
    ON_COMMAND_RANGE(IDM_AUDIO_BACKEND_ASIO, IDM_AUDIO_BACKEND_JACK,
                     &CMainFrame::OnBackendSelect)
    ON_MESSAGE(CJackBackend::WM_JACK_SHUTDOWN, &CMainFrame::OnJackShutdown)
    ON_MESSAGE(CJackBackend::WM_JACK_BUFSIZE, &CMainFrame::OnJackBufsize)
    ON_COMMAND(IDM_VIEW_METERS, &CMainFrame::OnViewMeters)
    ON_COMMAND_RANGE(IDM_VIEW_PEAK_BASE, IDM_VIEW_PEAK_MAX, &CMainFrame::OnPeakSelect)
    ON_COMMAND_RANGE(IDM_VIEW_REFRESH_BASE, IDM_VIEW_REFRESH_MAX, &CMainFrame::OnRefreshSelect)
    ON_COMMAND(IDM_VIEW_METER_SETTINGS, &CMainFrame::OnMeterSettings)
    ON_COMMAND(IDM_VIEW_METER_WINDOW, &CMainFrame::OnMeterWindow)
END_MESSAGE_MAP()

static UINT indicators[] =
{
    ID_SEPARATOR,   // DSP 使用率（左下角，固定宽度，不可截断）
    ID_SEPARATOR,   // 中间占位（stretch，空，把右侧 pane 推到右下角）
    ID_SEPARATOR    // 通道数 X 进 / Y 出（右下角，固定宽度，不可截断）
};

CMainFrame::CMainFrame()
    : m_pPluginView(NULL), m_pHost(NULL), m_hInternalMenu(NULL),
      m_pAsio(NULL), m_hAsioMenu(NULL), m_bShowMeters(false),
      m_pJack(NULL), m_pBackend(NULL), m_backendMode(0),
      m_lastJackBufsizeTick(0),
      m_bSineTest(false), m_sinePhase(0.0), m_peakHoldSeconds(1.0),
      m_meterRefreshMs(50), m_bPeakLine(true), m_bValueBox(true),
      m_loudnessStd(0), m_silenceReset(10.0), m_silenceThresh(-70.0),
      m_pMeterDlg(NULL),
      m_bCsvLog(false), m_csvIntervalMs(1000), m_csvFile(NULL),
      m_lastAutoResets(0), m_bTrayVisible(false)
{
    InitializeCriticalSection(&m_midiMapCs);
    memset((void *)m_inLevel, 0, sizeof(m_inLevel));
    memset((void *)m_outLevel, 0, sizeof(m_outLevel));
    memset((void *)m_inHold, 0, sizeof(m_inHold));
    memset((void *)m_outHold, 0, sizeof(m_outHold));
    memset((void *)m_inHoldFrames, 0, sizeof(m_inHoldFrames));
    memset((void *)m_outHoldFrames, 0, sizeof(m_outHoldFrames));
}

CMainFrame::~CMainFrame()
{
    RemoveTrayIcon();
    CloseCsvFile();
    DeleteCriticalSection(&m_midiMapCs);
    if (m_pAsio)
    {
        m_pAsio->Close();   /* 停止并释放 ASIO（不阻塞，已停止） */
        delete m_pAsio;
        m_pAsio = NULL;
    }
    if (m_pJack)
    {
        m_pJack->Close();   /* 停止并关闭 JACK client */
        delete m_pJack;
        m_pJack = NULL;
    }
    if (m_pPluginView)
        delete m_pPluginView;
}

/*****************************************************************************/
/* Create : 创建主窗口（含运行时构建的菜单）                                    */
/*****************************************************************************/
BOOL CMainFrame::Create()
{
    CString wndClass = AfxRegisterWndClass(CS_DBLCLKS,
        ::LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_BTNFACE + 1), 0);

    /* 窗口标题 = 插件/exe 文件名 + 实例序号（JACK 客户端名同源，见 HostNaming） */
    std::wstring hostName = ComputeHostName();

    if (!CFrameWnd::Create(wndClass, hostName.c_str(),
                           WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           CRect(0, 0, 900, 620), NULL, NULL))
        return FALSE;

    CMenu menu;
    menu.CreateMenu();

    CMenu mFile;
    mFile.CreatePopupMenu();
    mFile.AppendMenu(MF_STRING, IDM_FILE_OPEN, _T("打开插件(&O)..."));
    mFile.AppendMenu(MF_STRING, IDM_FILE_CLOSE, _T("关闭插件(&C)"));
    mFile.AppendMenu(MF_SEPARATOR);
    mFile.AppendMenu(MF_STRING, IDM_FILE_SAVE_EXE, _T("另存为插件快捷方式(&S)..."));
    mFile.AppendMenu(MF_STRING, IDM_PLUGIN_TESTPROC, _T("处理测试（1 块）"));
    mFile.AppendMenu(MF_SEPARATOR);
    mFile.AppendMenu(MF_STRING, IDM_FILE_SETTINGS, _T("全局设置(&G)..."));
    mFile.AppendMenu(MF_SEPARATOR);
    mFile.AppendMenu(MF_STRING, IDM_FILE_EXIT, _T("退出(&X)"));
    menu.AppendMenu(MF_POPUP, (UINT_PTR)mFile.Detach(), _T("文件(&F)"));

    /* 插件 -> 内部效果器（Shell 子菜单，运行时填充）+ 测试信号 */
    CMenu mPlug;
    mPlug.CreatePopupMenu();
    m_hInternalMenu = ::CreatePopupMenu();
    mPlug.AppendMenu(MF_POPUP, (UINT_PTR)m_hInternalMenu, _T("内部效果器(&I)"));
    mPlug.AppendMenu(MF_SEPARATOR);
    mPlug.AppendMenu(MF_STRING, IDM_PLUGIN_SINETEST,
                     _T("测试信号：1kHz 正弦(&T)"));
    mPlug.AppendMenu(MF_SEPARATOR);
    mPlug.AppendMenu(MF_STRING, IDM_PLUGIN_MIDIMAP, _T("MIDI 参数映射(&M)..."));
    menu.AppendMenu(MF_POPUP, (UINT_PTR)mPlug.Detach(), _T("插件(&P)"));

    /* 音频 -> ASIO 设备（运行时填充）+ 控制面板/刷新/通道分配 */
    CMenu mAudio;
    mAudio.CreatePopupMenu();
    m_hAsioMenu = ::CreatePopupMenu();
    mAudio.AppendMenu(MF_POPUP, (UINT_PTR)m_hAsioMenu, _T("ASIO 设备(&D)"));
    mAudio.AppendMenu(MF_SEPARATOR);
    mAudio.AppendMenu(MF_STRING, IDM_AUDIO_CPANEL, _T("驱动控制面板(&P)..."));
    mAudio.AppendMenu(MF_STRING, IDM_AUDIO_REFRESH, _T("刷新设备列表(&R)"));
    mAudio.AppendMenu(MF_SEPARATOR);
    mAudio.AppendMenu(MF_STRING, IDM_AUDIO_MAP, _T("ASIO 通道分配(&M)..."));
    mAudio.AppendMenu(MF_SEPARATOR);
    mAudio.AppendMenu(MF_STRING, IDM_AUDIO_BACKEND_ASIO, _T("使用 ASIO(&A)"));
    mAudio.AppendMenu(MF_STRING, IDM_AUDIO_BACKEND_JACK, _T("使用 JACK(&J)"));
    menu.AppendMenu(MF_POPUP, (UINT_PTR)mAudio.Detach(), _T("音频(&A)"));

    /* 视图 -> 显示电平表（勾选）+ 峰值保持时长 */
    CMenu mView;
    mView.CreatePopupMenu();
    mView.AppendMenu(MF_STRING, IDM_VIEW_METERS, _T("显示电平表(&M)"));
    mView.AppendMenu(MF_SEPARATOR);
    CMenu mPeak;
    mPeak.CreatePopupMenu();
    mPeak.AppendMenu(MF_STRING, IDM_VIEW_PEAK_BASE + 0, _T("关闭(&0)"));
    mPeak.AppendMenu(MF_STRING, IDM_VIEW_PEAK_BASE + 1, _T("0.5 秒"));
    mPeak.AppendMenu(MF_STRING, IDM_VIEW_PEAK_BASE + 2, _T("1 秒(&1)"));
    mPeak.AppendMenu(MF_STRING, IDM_VIEW_PEAK_BASE + 3, _T("2 秒"));
    mPeak.AppendMenu(MF_STRING, IDM_VIEW_PEAK_BASE + 4, _T("5 秒"));
    m_hPeakMenu = mPeak.Detach();
    mView.AppendMenu(MF_POPUP, (UINT_PTR)m_hPeakMenu, _T("峰值保持(&K)"));
    /* 视图 -> 电平表刷新频率 */
    CMenu mRefresh;
    mRefresh.CreatePopupMenu();
    mRefresh.AppendMenu(MF_STRING, IDM_VIEW_REFRESH_BASE + 0, _T("30 ms（最快）"));
    mRefresh.AppendMenu(MF_STRING, IDM_VIEW_REFRESH_BASE + 1, _T("50 ms(&5)"));
    mRefresh.AppendMenu(MF_STRING, IDM_VIEW_REFRESH_BASE + 2, _T("80 ms"));
    mRefresh.AppendMenu(MF_STRING, IDM_VIEW_REFRESH_BASE + 3, _T("100 ms（省电）"));
    m_hRefreshMenu = mRefresh.Detach();
    mView.AppendMenu(MF_POPUP, (UINT_PTR)m_hRefreshMenu, _T("电平表刷新频率(&R)"));
    mView.AppendMenu(MF_SEPARATOR);
    mView.AppendMenu(MF_STRING, IDM_VIEW_METER_SETTINGS, _T("电平表设置(&S)..."));
    mView.AppendMenu(MF_STRING, IDM_VIEW_METER_WINDOW, _T("独立电平表窗口(&W)"));
    menu.AppendMenu(MF_POPUP, (UINT_PTR)mView.Detach(), _T("视图(&V)"));

    CMenu mHelp;
    mHelp.CreatePopupMenu();
    mHelp.AppendMenu(MF_STRING, IDM_APP_ABOUT_PLUGIN, _T("关于插件(&P)..."));
    mHelp.AppendMenu(MF_STRING, IDM_APP_ABOUT, _T("关于 vsthost(&A)..."));
    menu.AppendMenu(MF_POPUP, (UINT_PTR)mHelp.Detach(), _T("帮助(&H)"));

    SetMenu(&menu);
    menu.Detach();      // 菜单句柄转交窗口

    ModifyStyleEx(0, WS_EX_ACCEPTFILES);    // 支持拖放插件

    /* 恢复电平表显示配置（[View] meters，默认显示） */
    m_bShowMeters = GetPrivateProfileIntW(L"View", L"meters", 1,
                                          AsioConfigPath().c_str()) != 0;
    if (m_bShowMeters)
        GetMenu()->CheckMenuItem(IDM_VIEW_METERS, MF_BYCOMMAND | MF_CHECKED);

    /* 音频后端（[View] backend，0=ASIO 1=JACK）：
       未配置过时自动检测——若 JACK 服务器在运行则默认用 JACK，否则 ASIO；
       已配置过则尊重用户上次选择 */
    wchar_t bk[8] = L"";
    GetPrivateProfileStringW(L"View", L"backend", L"", bk, 8,
                             AsioConfigPath().c_str());
    if (bk[0] == 0)
    {
        m_backendMode = CJackBackend::ServerAvailable() ? 1 : 0;
        WritePrivateProfileStringW(L"View", L"backend",
                                   m_backendMode ? L"1" : L"0",
                                   AsioConfigPath().c_str());
    }
    else
    {
        m_backendMode = (_wtoi(bk) != 0) ? 1 : 0;
    }
    if (GetMenu())
        GetMenu()->CheckMenuRadioItem(IDM_AUDIO_BACKEND_ASIO,
                                      IDM_AUDIO_BACKEND_JACK,
                                      m_backendMode ? IDM_AUDIO_BACKEND_JACK
                                                    : IDM_AUDIO_BACKEND_ASIO,
                                      MF_BYCOMMAND);

    /* 峰值保持时长（[View] peakhold，×10 存，0=关，默认 1 秒） */
    m_peakHoldSeconds = GetPrivateProfileIntW(L"View", L"peakhold", 10,
                                              AsioConfigPath().c_str()) / 10.0;
    ApplyPeakMenu();

    /* 电平表刷新频率（[View] meterrefresh，ms，默认 50） */
    m_meterRefreshMs = GetPrivateProfileIntW(L"View", L"meterrefresh", 50,
                                             AsioConfigPath().c_str());
    if (m_meterRefreshMs < 20) m_meterRefreshMs = 20;
    if (m_meterRefreshMs > 200) m_meterRefreshMs = 200;
    ApplyRefreshMenu();
    RefreshMeterTimer();    /* OnCreate 时用的默认值，这里按配置重设 */

    /* 电平表精细设置（[View]） */
    m_bPeakLine = GetPrivateProfileIntW(L"View", L"peakline", 1,
                                        AsioConfigPath().c_str()) != 0;
    m_bValueBox = GetPrivateProfileIntW(L"View", L"metervalue", 1,
                                        AsioConfigPath().c_str()) != 0;
    m_loudnessStd = GetPrivateProfileIntW(L"View", L"loudnessstd", 0,
                                          AsioConfigPath().c_str());
    if (m_loudnessStd < 0) m_loudnessStd = 0;
    if (m_loudnessStd >= g_loudnessStdCount) m_loudnessStd = g_loudnessStdCount - 1;
    m_silenceReset = (double)GetPrivateProfileIntW(L"View", L"lra_silreset", 10,
                                                   AsioConfigPath().c_str());
    m_silenceThresh = (double)GetPrivateProfileIntW(L"View", L"silence_thresh", -70,
                                                    AsioConfigPath().c_str());
    m_loudIn.SetSilenceReset(m_silenceReset);
    m_loudOut.SetSilenceReset(m_silenceReset);
    m_loudIn.SetSilenceThreshold(m_silenceThresh);
    m_loudOut.SetSilenceThreshold(m_silenceThresh);

    /* CSV 响度日志（[View] csvlog / csvinterval / csvfolder） */
    m_bCsvLog = GetPrivateProfileIntW(L"View", L"csvlog", 0,
                                      AsioConfigPath().c_str()) != 0;
    m_csvIntervalMs = GetPrivateProfileIntW(L"View", L"csvinterval", 1000,
                                            AsioConfigPath().c_str());
    if (m_csvIntervalMs < 200) m_csvIntervalMs = 200;
    if (m_csvIntervalMs > 60000) m_csvIntervalMs = 60000;
    wchar_t csvDir[1024] = L"";
    GetPrivateProfileStringW(L"View", L"csvfolder", L"", csvDir, 1024,
                             AsioConfigPath().c_str());
    m_csvFolder = csvDir;
    m_lastAutoResets = 0;
    if (m_bCsvLog)
        OpenCsvFile();
    LoadMidiMap();

    /* 定时自动保存插件状态（30s，防崩溃丢失，计划书 §5.9 第 5 点） */
    SetTimer(4, 30000, NULL);

    ApplySystemStyle(m_hWnd);               // DWM 系统风格（深色标题栏/圆角）
    return TRUE;
}

/*****************************************************************************/
/* OnCreate : 创建状态栏 / 插件宿主窗格 / 空闲定时器                            */
/*****************************************************************************/
int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (CFrameWnd::OnCreate(lpCreateStruct) == -1)
        return -1;

    /* 窗口图标：大图标（标题栏）+ 小图标（任务栏/Alt-Tab），取自 vsthost.rc 的
       IDR_MAINFRAME=128（即 IDR_APP_ICON，与托盘 LoadIcon 同源） */
    SetIcon(AfxGetApp()->LoadIcon(IDR_APP_ICON), TRUE);
    SetIcon(AfxGetApp()->LoadIcon(IDR_APP_ICON), FALSE);

    if (!m_wndStatusBar.Create(this) ||
        !m_wndStatusBar.SetIndicators(indicators, _countof(indicators)))
        return -1;
    /* DSP 使用率 pane 固定宽度（最左=左下角，不可截断） */
    m_wndStatusBar.SetPaneInfo(0, ID_SEPARATOR, SBPS_NORMAL, 96);
    /* 中间占位 pane 拉伸占满剩余空间（保证右侧 pane 贴到右下角） */
    m_wndStatusBar.SetPaneInfo(1, ID_SEPARATOR, SBPS_STRETCH, 0);
    /* 通道数 pane 固定宽度（最右=右下角，不可截断） */
    m_wndStatusBar.SetPaneInfo(2, ID_SEPARATOR, SBPS_NORMAL, 118);

    m_pPluginView = new CPluginHostView;
    if (!m_pPluginView->Create(this, 1001))
        return -1;

    /* 统一使用当前系统 UI 字体（Segoe UI 等，含占位框） */
    NONCLIENTMETRICSW ncm;
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        m_fontUI.CreateFontIndirectW(&ncm.lfMessageFont);
    /* 电平表数值框小字体（基于系统 UI 字体缩小） */
    if ((HFONT)m_fontUI)
    {
        LOGFONTW lf;
        m_fontUI.GetLogFont(&lf);
        lf.lfHeight = -11;          /* 小号（约 8pt） */
        m_fontMeter.CreateFontIndirectW(&lf);
    }
    if ((HFONT)m_fontUI)
    {
        m_wndStatusBar.SetFont(&m_fontUI);
        m_pPluginView->SetFont(&m_fontUI);
        SetFont(&m_fontUI);
    }

    SetTimer(1, 50, NULL);      // 插件 idle（编辑器刷新，固定 50ms）
    SetTimer(2, (UINT)m_meterRefreshMs, NULL);  // 电平表刷新（频率可配）
    SetTimer(3, (UINT)m_csvIntervalMs, NULL);   // CSV 响度日志采样（可配间隔）
    return 0;
}

/*****************************************************************************/
/* OnSize : 调整插件宿主窗格尺寸（扣除状态栏）                                  */
/*****************************************************************************/
void CMainFrame::OnSize(UINT nType, int cx, int cy)
{
    CFrameWnd::OnSize(nType, cx, cy);
    if (m_pPluginView && m_wndStatusBar.m_hWnd)
    {
        CRect rcStatus, rcClient;
        m_wndStatusBar.GetWindowRect(&rcStatus);
        GetClientRect(&rcClient);
        rcClient.bottom -= rcStatus.Height();
        if (m_bShowMeters)
        {
            /* 左右边缘留垂直电平表区域（高度自适应窗口） */
            rcClient.left  += MeterInWidth();
            rcClient.right -= MeterOutWidth();
        }
        m_pPluginView->MoveWindow(&rcClient);
    }
}

/*****************************************************************************/
/* OnTimer : 插件 idle 轮询                                                   */
/*****************************************************************************/
void CMainFrame::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 1)      /* 插件 idle（固定 50ms） */
    {
        if (m_pHost && m_pHost->IsLoaded())
        {
            IPlugin *p = m_pHost->Get();
            p->Idle();
            /* 插件请求了新编辑器尺寸 -> 自适应窗口 */
            if (p->EditorSizeChanged())
            {
                int w = 0, h = 0;
                if (p->GetEditorSize(w, h) && w > 0 && h > 0)
                    FitWindowToEditor(w, h);
            }
        }
        /* 每 500ms 刷新状态栏（DSP 使用率） */
        static int nTick = 0;
        if ((++nTick % 10) == 0)
            UpdateAsioStatus();
    }
    else if (nIDEvent == 2) /* 电平表刷新（频率可配） */
    {
        InvalidateMeters();
    }
    else if (nIDEvent == 3) /* CSV 响度日志采样 */
    {
        if (m_bCsvLog)
        {
            /* 自动静音重置后开新文件 */
            long ar = m_loudIn.AutoResetCount();
            if (ar != m_lastAutoResets)
            {
                m_lastAutoResets = ar;
                OpenCsvFile();
            }
            WriteCsvRow();
        }
    }
    else if (nIDEvent == 4) /* 定时自动保存插件状态（30s，防崩溃丢失） */
    {
        if (m_pHost && m_pHost->IsLoaded())
            m_pHost->SaveStateFile();
    }
    CFrameWnd::OnTimer(nIDEvent);
}

/*****************************************************************************/
/* InvalidateMeters : 失效左右边缘电平表条带区域（定时重绘，不打扰编辑器）      */
/*****************************************************************************/
void CMainFrame::InvalidateMeters()
{
    if (!m_bShowMeters || !m_wndStatusBar.m_hWnd)
        return;
    /* 当前后端（ASIO 或 JACK）运行才刷新 */
    bool running = (m_pJack && m_pJack->IsRunning()) ||
                   (m_pAsio && m_pAsio->IsRunning());
    if (!running)
        return;
    CRect rcStatus, rcClient;
    m_wndStatusBar.GetWindowRect(&rcStatus);
    GetClientRect(&rcClient);
    rcClient.bottom -= rcStatus.Height();
    int wIn = MeterInWidth();
    int wOut = MeterOutWidth();
    if (wIn > 0)
    {
        CRect rc(rcClient.left, rcClient.top,
                 rcClient.left + wIn, rcClient.bottom);
        InvalidateRect(&rc, FALSE);
    }
    if (wOut > 0)
    {
        CRect rc(rcClient.right - wOut, rcClient.top,
                 rcClient.right, rcClient.bottom);
        InvalidateRect(&rc, FALSE);
    }
}

/*****************************************************************************/
/* AutoLoad : 启动时按 exe 名同名自动加载                                      */
/*****************************************************************************/
void CMainFrame::AutoLoad()
{
    if (!m_pHost)
        return;
    wchar_t szExe[1024];
    GetModuleFileNameW(NULL, szExe, 1024);
    if (!m_pHost->LoadFromExeName(szExe))
        return;                     // 未找到同名插件：保持空宿主，可手动打开
    OpenPluginEditor();
    StartAudio();                   // 加载后启动 ASIO（默认 FL Studio ASIO）
}

/*****************************************************************************/
/* DoLoad : 手动加载插件                                                       */
/*****************************************************************************/
void CMainFrame::DoLoad(const std::wstring &path)
{
    if (!m_pHost)
        return;

    ClosePluginEditor();
    if (!m_pHost->LoadFromFile(path))
    {
        DWORD err = GetLastError();
        CString msg;
        if (err == ERROR_PROC_NOT_FOUND)
            msg = _T("插件加载失败（模块无法加载，可能是位数不匹配或模块损坏）。");
        else if (err == ERROR_BAD_EXE_FORMAT)
            msg = _T("插件位数与宿主不匹配：32 位插件请使用 x86 版 vsthost，64 位插件请使用 x64 版。");
        else
            msg = _T("插件加载失败。");
        AfxMessageBox(msg, MB_OK | MB_ICONERROR);
        UpdateStatus();
        return;
    }
    OpenPluginEditor();
    StartAudio();
}

/*****************************************************************************/
/* OpenPluginEditor / ClosePluginEditor : 编辑器嵌入                           */
/*****************************************************************************/
void CMainFrame::OpenPluginEditor()
{
    ClosePluginEditor();
    if (!m_pHost || !m_pHost->IsLoaded())
    {
        RebuildInternalMenu();
        UpdateStatus();
        return;
    }
    IPlugin *p = m_pHost->Get();
    if (p && p->HasEditor())
    {
        p->OpenEditor(m_pPluginView->GetSafeHwnd());
        /* 随插件界面大小自适应主窗口 */
        int w = 0, h = 0;
        if (p->GetEditorSize(w, h) && w > 0 && h > 0)
            FitWindowToEditor(w, h);
    }
    /* 加载插件后窗口标题 = 插件文件名_实例序号（JACK 客户端名同源） */
    SetWindowTextW(m_pHost->GetStateBase().c_str());
    RebuildInternalMenu();
    UpdateStatus();
}

/*****************************************************************************/
/* FitWindowToEditor : 调整主窗口尺寸，使插件宿主区刚好容纳编辑器             */
/*****************************************************************************/
void CMainFrame::FitWindowToEditor(int w, int h)
{
    if (!m_pPluginView || !m_wndStatusBar.m_hWnd)
        return;
    CRect rcView;
    m_pPluginView->GetWindowRect(&rcView);
    int dw = w - rcView.Width();
    int dh = h - rcView.Height();
    if (dw == 0 && dh == 0)
        return;

    CRect rcFrame;
    GetWindowRect(&rcFrame);
    SetWindowPos(NULL, 0, 0,
                 rcFrame.Width() + dw,
                 rcFrame.Height() + dh,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void CMainFrame::ClosePluginEditor()
{
    if (m_pHost && m_pHost->IsLoaded())
        m_pHost->Get()->CloseEditor();
}

/*****************************************************************************/
/* RebuildInternalMenu : 填充“内部效果器”子菜单（仅 WaveShell）                 */
/*****************************************************************************/
void CMainFrame::RebuildInternalMenu()
{
    if (!m_hInternalMenu)
        return;
    while (::GetMenuItemCount(m_hInternalMenu) > 0)
        ::RemoveMenu(m_hInternalMenu, 0, MF_BYPOSITION);

    int n = m_pHost ? m_pHost->GetInternalCount() : 0;
    if (n <= 0)
        return;

    int cur = m_pHost ? m_pHost->GetCurrentInternal() : -1;
    for (int i = 0; i < n; i++)
    {
        PluginInternalInfo info;
        if (!m_pHost->GetInternalInfo(i, &info))
            continue;
        CString name(info.name);    // ANSI -> Unicode
        UINT flags = MF_STRING | ((i == cur) ? MF_CHECKED : 0);
        ::AppendMenu(m_hInternalMenu, flags, IDM_INTERNAL_BASE + i, name);
    }
}

/*****************************************************************************/
/* UpdateStatus : 状态栏                                                       */
/*****************************************************************************/
void CMainFrame::UpdateStatus()
{
    UpdateAsioStatus();     /* 状态栏 = 左下 DSP + 右下通道 */
}

/*****************************************************************************/
/* ---- 音频（M4 ASIO）----                                                   */
/*****************************************************************************/
/*****************************************************************************/
/* AsioConfigPath : 配置 ini = exe 同目录 <exe名>.ini                         */
/*****************************************************************************/
std::wstring CMainFrame::AsioConfigPath() const
{
    wchar_t sz[1024];
    GetModuleFileNameW(NULL, sz, 1024);
    return std::wstring(sz) + L".ini";
}

/*****************************************************************************/
/* SaveAsioConfig : 记忆上次驱动（[asio] driver=）                            */
/*****************************************************************************/
void CMainFrame::SaveAsioConfig(const char *driverName)
{
    if (!driverName || !driverName[0])
        return;
    wchar_t wbuf[256];
    MultiByteToWideChar(CP_ACP, 0, driverName, -1, wbuf, 256);
    WritePrivateProfileStringW(L"asio", L"driver", wbuf,
                               AsioConfigPath().c_str());
}

/*****************************************************************************/
/* PickDefaultDriver : 记忆驱动 / FL Studio ASIO / 第一个                     */
/*   默认 FL Studio ASIO：软件驱动，避免抢占其他 DAW 正在用的 ASIO 硬件驱动   */
/*****************************************************************************/
std::string CMainFrame::PickDefaultDriver()
{
    /* 1) 配置记忆的驱动（若仍存在） */
    wchar_t wbuf[256] = L"";
    GetPrivateProfileStringW(L"asio", L"driver", L"", wbuf, 256,
                             AsioConfigPath().c_str());
    if (wbuf[0])
    {
        char abuf[256] = "";
        WideCharToMultiByte(CP_ACP, 0, wbuf, -1, abuf, 256, NULL, NULL);
        for (size_t i = 0; i < m_asioDrivers.size(); i++)
            if (_stricmp(m_asioDrivers[i].c_str(), abuf) == 0)
                return m_asioDrivers[i];
    }
    /* 2) 默认 FL Studio ASIO */
    for (size_t i = 0; i < m_asioDrivers.size(); i++)
        if (_stricmp(m_asioDrivers[i].c_str(), "FL Studio ASIO") == 0)
            return m_asioDrivers[i];
    /* 3) 第一个可用 */
    return m_asioDrivers[0];
}

/*****************************************************************************/
/* StartAudio : 插件加载后启动音频（按后端模式分发 ASIO / JACK）              */
/*   启动目标后端前先彻底关闭另一后端：两个后端严格互斥，JACK 模式下          */
/*   ASIO 驱动完全释放（不占用、不传输）                                      */
/*   若启用 JACK 但服务器未运行/不可用 → 自动回退到 ASIO 保证出声             */
/*****************************************************************************/
void CMainFrame::StartAudio()
{
    if (!m_pHost || !m_pHost->IsLoaded())
        return;
    if (m_backendMode == 1)
    {
        if (m_pAsio && m_pAsio->IsOpen())
            m_pAsio->Close();       /* 彻底关闭 ASIO（释放驱动 DLL） */
        if (StartJackAudio())
            return;                 /* JACK 启动成功 */
        /* JACK 不可用/失败 → 回退 ASIO */
        if (m_pJack && m_pJack->IsOpen())
            m_pJack->Close();
        StartAsioAudio();
        AfxMessageBox(_T("JACK 未运行（或未找到 libjack64.dll），已回退到 ASIO。\n"
                         "可在“音频→使用 ASIO / 使用 JACK”中随时切换。"),
                      MB_OK | MB_ICONINFORMATION);
    }
    else
    {
        if (m_pJack && m_pJack->IsOpen())
            m_pJack->Close();       /* 彻底关闭 JACK */
        StartAsioAudio();
    }
}

/*****************************************************************************/
/* StartJackAudio : 启动 JACK 音频流（端口随插件能力，计划书 §5.6）            */
/*   返回 false：lib 缺失 / 服务器未运行 / 打开或启动失败（调用方回退 ASIO）   */
/*****************************************************************************/
bool CMainFrame::StartJackAudio()
{
    if (!m_pHost || !m_pHost->IsLoaded())
        return false;
    if (!m_pJack)
        m_pJack = new CJackBackend;
    if (!m_pJack)
        return false;

    if (!CJackBackend::IsAvailable())
    {
        m_wndStatusBar.SetPaneText(1, _T("JACK: 未找到 libjack64.dll"));
        return false;
    }
    if (!CJackBackend::ServerAvailable())
    {
        m_wndStatusBar.SetPaneText(1, _T("JACK: 服务器未运行"));
        return false;
    }

    IPlugin *p = m_pHost->Get();
    int in = p->GetInputChannels();
    int out = p->GetOutputChannels();

    /* MIDI 端口随插件能力（VST2 receiveVstMidiEvent / VST3 事件总线） */
    m_pJack->SetMidiPorts(p->WantMidiInput(), p->WantMidiOutput());
    m_pJack->SetNotifyWindow(GetSafeHwnd());

    /* 真实端口名：优先用插件总线/pin 名（VST3 bus 名 / VST2 pin label），
       拿不到则留空 → JACK 回退默认命名 in_N / out_N */
    {
        std::vector<std::string> inNames(in), outNames(out);
        char pn[128];
        for (int i = 0; i < in && p->GetChannelName(i, true, pn, sizeof(pn)); i++)
            inNames[i] = pn;
        for (int i = 0; i < out && p->GetChannelName(i, false, pn, sizeof(pn)); i++)
            outNames[i] = pn;
        m_pJack->SetPortNames(inNames, outNames);
    }

    /* 客户端名：优先用当前插件名（GetStateBase = 窗口标题，含实例序号
       "插件名_1"）；无插件时用 exe 主干名。JACK 客户端名上限 27 字节。
       先查 ini 持久化映射 [JackNames] <插件原全名_序号>=<最终客户端名>
       （首次生成后固定）；无记录或撞名/超限时按 fallback 链重新生成：
       全名 → 省略号 → 缩写 → 真随机兜底（首次生成后靠 ini 固定），
       无论落在哪一级都写回 ini 持久保存。 */
    std::wstring hostName = (m_pHost && m_pHost->IsLoaded())
                                ? m_pHost->GetStateBase()
                                : ComputeHostName();
    std::string utf8;
    {
        int n = WideCharToMultiByte(CP_UTF8, 0, hostName.c_str(), -1,
                                    NULL, 0, NULL, NULL);
        if (n > 1)
        {
            utf8.resize(n - 1);
            WideCharToMultiByte(CP_UTF8, 0, hostName.c_str(), -1,
                                &utf8[0], n, NULL, NULL);
        }
    }

    /* 撞名检测：服务器上当前已占用的客户端名集合 */
    std::set<std::string> used;
    m_pJack->GetUsedClientNames(used);

    std::string cnameStr;
    {
        /* 优先读 ini 持久化名字 */
        wchar_t wini[64] = { 0 };
        GetPrivateProfileStringW(L"JackNames", hostName.c_str(), L"",
                                 wini, 64, AsioConfigPath().c_str());
        if (wini[0])
        {
            int m = WideCharToMultiByte(CP_UTF8, 0, wini, -1, NULL, 0, NULL, NULL);
            if (m > 1)
            {
                cnameStr.resize(m - 1);
                WideCharToMultiByte(CP_UTF8, 0, wini, -1,
                                    &cnameStr[0], m, NULL, NULL);
            }
        }
        /* 无效（空/超限/撞名）则重新 fallback 并写回 */
        if (cnameStr.empty() || cnameStr.size() > 27 ||
            used.find(cnameStr) != used.end())
        {
            cnameStr = MakeJackClientName(utf8, used);
            std::wstring wout;
            int m = MultiByteToWideChar(CP_UTF8, 0, cnameStr.c_str(), -1,
                                        NULL, 0);
            if (m > 1)
            {
                wout.resize(m - 1);
                MultiByteToWideChar(CP_UTF8, 0, cnameStr.c_str(), -1,
                                    &wout[0], m);
            }
            WritePrivateProfileStringW(L"JackNames", hostName.c_str(),
                                       wout.c_str(), AsioConfigPath().c_str());
        }
    }
    char cname[64] = { 0 };
    cnameStr.copy(cname, sizeof(cname) - 1);

    if (!m_pJack->Open(cname, 0.0, 0, in, out, GetSafeHwnd()))
    {
        UpdateAsioStatus();
        return false;               /* 打开失败 → 上层回退 ASIO */
    }
    m_pBackend = m_pJack;

    /* JACK：端口数 = 插件通道数，一一对应直通，不使用 ASIO 通道映射
       （清空残留的 [AsioMap]，否则输入/输出被错误重定向到内部静音缓冲） */
    m_mapIn.clear();
    m_mapOut.clear();

    /* MIDI 通道过滤（[asio] midichannel；JACK 下 MIDI 输入来自 midi_in 端口，
       不使用 MME MIDI 设备） */
    m_midiChannel = GetPrivateProfileIntW(L"asio", L"midichannel", 0,
                                          AsioConfigPath().c_str());
    if (m_midiChannel < 0 || m_midiChannel > 16) m_midiChannel = 0;
    m_midiEnabled = false;
    m_midiInput.Close();
    /* JACK 自带 midi_out，不使用 MME MIDI 输出 */
    m_midiOutEnabled = false;
    m_midiOut.Close();

    /* 电平清零 */
    memset((void *)m_inLevel, 0, sizeof(m_inLevel));
    memset((void *)m_outLevel, 0, sizeof(m_outLevel));

    /* 响度核心按 JACK 采样率/通道数初始化 */
    m_loudIn.Setup(m_pJack->GetSampleRate(), in);
    m_loudOut.Setup(m_pJack->GetSampleRate(), out);

    /* 用 JACK 实际采样率/块大小重配置插件 */
    p->ReconfigureAudio(m_pJack->GetSampleRate(), m_pJack->GetBufferSize());

    /* 分配回调缓冲（插件通道数） */
    int maxCh = (in > out ? in : out);
    if (maxCh < 1)
        maxCh = 1;
    int frames = m_pJack->GetBufferSize();
    m_plugIn.resize(maxCh);
    m_plugOut.resize(maxCh);
    for (int i = 0; i < maxCh; i++)
    {
        m_plugIn[i].assign(frames, 0.f);
        m_plugOut[i].assign(frames, 0.f);
    }
    m_plugInPtrs.resize(maxCh);
    m_plugOutPtrs.resize(maxCh);
    for (int i = 0; i < maxCh; i++)
    {
        m_plugInPtrs[i] = m_plugIn[i].data();
        m_plugOutPtrs[i] = m_plugOut[i].data();
    }

    m_pJack->SetProcessCallback(&CMainFrame::AudioProcessCB, this);
    if (!m_pJack->Start())
    {
        m_pJack->Close();
        UpdateAsioStatus();
        return false;               /* 启动失败 → 上层回退 ASIO */
    }
    RebuildAsioMenu();
    UpdateAsioStatus();

    /* 重排布局（左右电平表区域）并重绘 */
    CRect rc;
    GetClientRect(&rc);
    OnSize(SIZE_RESTORED, rc.Width(), rc.Height());
    Invalidate();
    return true;
}

/*****************************************************************************/
/* StartAsioAudio : 启动 ASIO 音频流（默认 FL Studio ASIO）                   */
/*****************************************************************************/
void CMainFrame::StartAsioAudio()
{
    if (!m_pHost || !m_pHost->IsLoaded())
        return;
    if (!m_pAsio)
        m_pAsio = new CAsioBackend;
    if (!m_pAsio)
        return;

    CAsioBackend::EnumerateDrivers(m_asioDrivers);
    if (m_asioDrivers.empty())
    {
        m_wndStatusBar.SetPaneText(1, _T("ASIO: 无驱动（请安装 ASIO 驱动）"));
        return;
    }

    std::string drv = PickDefaultDriver();
    if (drv.empty())
        return;

    IPlugin *p = m_pHost->Get();
    int in = p->GetInputChannels();
    int out = p->GetOutputChannels();

    /* 采样率 / 缓冲：ini 记忆（[asio] samplerate / buffersize），首次用默认并回写 */
    double wantRate = (double)GetPrivateProfileIntW(L"asio", L"samplerate", 0,
                                                    AsioConfigPath().c_str());
    int    wantBuf  = GetPrivateProfileIntW(L"asio", L"buffersize", 0,
                                            AsioConfigPath().c_str());
    if (wantRate <= 0) wantRate = 44100.0;
    if (wantBuf  <= 0) wantBuf  = 512;

    /* 打开（默认设备 = FL Studio ASIO；sysRef = 主窗口句柄） */
    if (!m_pAsio->Open(drv.c_str(), wantRate, wantBuf, in, out, GetSafeHwnd()))
    {
        CString msg;
        msg.Format(_T("ASIO 设备打开失败：%S\n\n可能是采样率/缓冲被占用，或被其他程序独占\n（如 FL Studio、Cubase 等 DAW）。\n请先关闭占用它的程序，或在“音频->ASIO 设备”中选择其他驱动。"),
                   drv.c_str());
        AfxMessageBox(msg, MB_OK | MB_ICONWARNING);
        m_asioCurrent.clear();
        UpdateAsioStatus();
        return;
    }
    m_asioCurrent = drv;
    SaveAsioConfig(drv.c_str());
    m_pBackend = m_pAsio;

    /* 首次（ini 未记录）把驱动实际采样率/缓冲回写为默认值 */
    if (GetPrivateProfileIntW(L"asio", L"samplerate", 0, AsioConfigPath().c_str()) <= 0)
    {
        wchar_t b[32];
        swprintf(b, 32, L"%.0f", m_pAsio->GetSampleRate());
        WritePrivateProfileStringW(L"asio", L"samplerate", b, AsioConfigPath().c_str());
        swprintf(b, 32, L"%d", m_pAsio->GetBufferSize());
        WritePrivateProfileStringW(L"asio", L"buffersize", b, AsioConfigPath().c_str());
    }

    /* MIDI 输入（[asio] midiinput / mididevice / midichannel，默认开启首个设备） */
    m_midiEnabled = GetPrivateProfileIntW(L"asio", L"midiinput", 0,
                                          AsioConfigPath().c_str()) != 0;
    m_midiDevice  = GetPrivateProfileIntW(L"asio", L"mididevice", 0,
                                          AsioConfigPath().c_str());
    m_midiChannel = GetPrivateProfileIntW(L"asio", L"midichannel", 0,
                                          AsioConfigPath().c_str());
    if (m_midiChannel < 0 || m_midiChannel > 16) m_midiChannel = 0;
    if (m_midiEnabled)
        m_midiInput.Open(m_midiDevice);

    /* MIDI 输出（MME，[asio] midiout / midioutdevice；ASIO 无自带 MIDI） */
    m_midiOutEnabled = GetPrivateProfileIntW(L"asio", L"midiout", 0,
                                             AsioConfigPath().c_str()) != 0;
    m_midiOutDevice  = GetPrivateProfileIntW(L"asio", L"midioutdevice", 0,
                                             AsioConfigPath().c_str());
    if (m_midiOutEnabled)
        m_midiOut.Open(m_midiOutDevice);
    else
        m_midiOut.Close();

    /* 通道映射（[AsioMap]；默认前 N） */
    LoadAsioMap(in, out, m_pAsio->GetInputChannels(), m_pAsio->GetOutputChannels());
    /* 电平清零 */
    memset((void *)m_inLevel, 0, sizeof(m_inLevel));
    memset((void *)m_outLevel, 0, sizeof(m_outLevel));

    /* 响度核心按实际采样率/通道数初始化 */
    m_loudIn.Setup(m_pAsio->GetSampleRate(), in);
    m_loudOut.Setup(m_pAsio->GetSampleRate(), out);

    /* 用驱动实际采样率/块大小重配置插件 */
    p->ReconfigureAudio(m_pAsio->GetSampleRate(), m_pAsio->GetBufferSize());

    /* 分配回调缓冲（插件通道数，含 ASIO 不足时的零填充/丢弃） */
    int maxCh = (in > out ? in : out);
    if (maxCh < 1)
        maxCh = 1;
    int frames = m_pAsio->GetBufferSize();
    m_plugIn.resize(maxCh);
    m_plugOut.resize(maxCh);
    for (int i = 0; i < maxCh; i++)
    {
        m_plugIn[i].assign(frames, 0.f);
        m_plugOut[i].assign(frames, 0.f);
    }
    m_plugInPtrs.resize(maxCh);
    m_plugOutPtrs.resize(maxCh);
    for (int i = 0; i < maxCh; i++)
    {
        m_plugInPtrs[i] = m_plugIn[i].data();
        m_plugOutPtrs[i] = m_plugOut[i].data();
    }

    m_pAsio->SetProcessCallback(&CMainFrame::AudioProcessCB, this);
    if (!m_pAsio->Start())
    {
        AfxMessageBox(_T("ASIO 启动失败。"), MB_OK | MB_ICONERROR);
        m_pAsio->Close();
        UpdateAsioStatus();
        return;
    }
    RebuildAsioMenu();
    UpdateAsioStatus();

    /* 重排布局（左右电平表区域）并重绘 */
    CRect rc;
    GetClientRect(&rc);
    OnSize(SIZE_RESTORED, rc.Width(), rc.Height());
    Invalidate();
}

/*****************************************************************************/
/* StopAudio : 停止音频（关闭插件/切换/退出前调用）                            */
/*****************************************************************************/
void CMainFrame::StopAudio()
{
    m_midiInput.Close();
    m_midiOut.Close();
    if (m_pJack)
        m_pJack->Close();           /* 停止 + 关闭 JACK client */
    if (m_pAsio)
        m_pAsio->Close();           /* 内部 Stop + DisposeBuffers + ASIOExit */
    m_asioCurrent.clear();
    m_pBackend = NULL;
    m_plugInPtrs.clear();
    m_plugOutPtrs.clear();
    UpdateAsioStatus();
}

/*****************************************************************************/
/* AudioProcessCB : ASIO / JACK 实时回调（共用）                            */
/*   通道映射（[AsioMap]，-1=静音/丢弃）+ 插件处理 + 输入/输出电平采集          */
/*****************************************************************************/
void CMainFrame::AudioProcessCB(void *ctx, float **in, float **out,
                                int frames, int inCh, int outCh)
{
    CMainFrame *f = (CMainFrame *)ctx;
    if (!f || !f->m_pHost)
        return;
    IPlugin *p = f->m_pHost->Get();
    if (!p)
        return;

    int pin = p->GetInputChannels();
    int pout = p->GetOutputChannels();
    if (pin < 1 && pout < 1)
        return;

    float *pi[32], *po[32];
    int zi = 0;
    for (int c = 0; c < pin; c++)
    {
        int src = (c < (int)f->m_mapIn.size()) ? f->m_mapIn[c]
                                               : (c < inCh ? c : -1);
        if (src >= 0 && src < inCh)
            pi[c] = in[src];
        else if (zi < (int)f->m_plugInPtrs.size())
        {
            pi[c] = f->m_plugInPtrs[zi];
            memset(pi[c], 0, frames * sizeof(float));
            zi++;
        }
        else
            pi[c] = f->m_plugInPtrs[0];
    }
    zi = 0;
    for (int c = 0; c < pout; c++)
    {
        int dst = (c < (int)f->m_mapOut.size()) ? f->m_mapOut[c]
                                                : (c < outCh ? c : -1);
        if (dst >= 0 && dst < outCh)
            po[c] = out[dst];
        else if (zi < (int)f->m_plugOutPtrs.size())
        {
            po[c] = f->m_plugOutPtrs[zi];
            zi++;
        }
        else
            po[c] = f->m_plugOutPtrs[0];
    }

    /* MIDI 输入：JACK 自带 MIDI 端口事件（本回调内由 JackBackend 收集） */
    if (f->m_pJack && f->m_pJack->IsRunning())
    {
        int n = f->m_pJack->GetMidiInCount();
        for (int i = 0; i < n; i++)
        {
            unsigned char mev[3];
            int mlen = 0;
            f->m_pJack->GetMidiIn(i, mev, mlen);
            f->ProcessMidiEvent(p, mev, mlen);
        }
    }
    /* MIDI 输入：MME 队列（[asio] midiinput，同一实时线程消费，安全） */
    if (f->m_midiEnabled)
    {
        unsigned char mev[3];
        int mlen = 0;
        while (f->m_midiInput.PopEvent(mev, mlen))
            f->ProcessMidiEvent(p, mev, mlen);
    }

    /* 测试信号：1kHz 正弦注入所有插件输入通道（覆盖真实输入，验证通路） */
    if (f->m_bSineTest)
    {
        double sr = (f->m_pBackend && f->m_pBackend->GetSampleRate() > 0)
                        ? f->m_pBackend->GetSampleRate() : 44100.0;
        double st = 2.0 * 3.14159265358979 * 1000.0 / sr;
        double ph = f->m_sinePhase;
        for (int c = 0; c < pin; c++)
        {
            float *b = pi[c];
            for (int i = 0; i < frames; i++)
                b[i] = (float)(0.5 * sin(ph + st * i));
        }
        f->m_sinePhase = ph + st * frames;
    }

    /* 输入电平 + 平滑 + 峰值线：
       条（m_inLevel）attack τ≈30ms / release τ≈80ms，跟随信号起伏；
       峰值线（m_inHold）同样平滑，上升快、下降 τ=峰值保持时长（默认 1s），不瞬跳；
       低于 -60dB 视为静音直接归零 */
    double srM = (f->m_pBackend && f->m_pBackend->GetSampleRate() > 0)
                     ? f->m_pBackend->GetSampleRate() : 44100.0;
    float att = (srM > 0) ? (float)exp(-(double)frames / (srM * 0.03))
                          : 0.60f;
    float rel = (srM > 0) ? (float)exp(-(double)frames / (srM * 0.08))
                          : 0.88f;
    double tauHold = (f->m_peakHoldSeconds > 0.0) ? f->m_peakHoldSeconds : 0.05;
    float relH = (srM > 0) ? (float)exp(-(double)frames / (srM * tauHold))
                           : 0.999f;
    for (int c = 0; c < pin && c < 32; c++)
    {
        const float *buf = pi[c];
        float pk = 0.f;
        for (int i = 0; i < frames; i++)
        {
            float a = buf[i];
            if (a < 0.f) a = -a;
            if (a > pk) pk = a;
        }
        float cur = f->m_inLevel[c];
        if (pk >= cur)
            cur = cur + (pk - cur) * att; /* 平滑上升（attack） */
        else
            cur = pk + (cur - pk) * rel;  /* 平滑下降（release） */
        if (cur < 0.001f) cur = 0.f;      /* 静音归零（<-60dB） */
        f->m_inLevel[c] = cur;
        /* 峰值线：平滑上升（快）/ 平滑下降（τ=保持时长），与条同源不瞬跳 */
        float hd = f->m_inHold[c];
        if (pk >= hd)
            hd = hd + (pk - hd) * att;
        else
            hd = pk + (hd - pk) * relH;
        if (hd < 0.001f) hd = 0.f;
        f->m_inHold[c] = hd;
    }

    /* 输入响度（K 权重 + gating，实时累积） */
    f->m_loudIn.Process((const float *const *)pi, pin, frames);

    p->Process(pi, po, frames, pin, pout, NULL);

    /* MIDI 输出：插件产生的输出事件（取决于插件能力） */
    IPlugin::PluginMidiEvent mout[64];
    int mn = p->CollectMidiOut(mout, 64);
    if (mn > 0)
    {
        if (f->m_pJack && f->m_pJack->IsRunning())
            f->m_pJack->WriteMidiOut(mout, mn);       /* JACK midi_out */
        else if (f->m_midiOutEnabled)                 /* ASIO：MME MIDI Out */
            for (int i = 0; i < mn; i++)
                f->m_midiOut.SendEvent(mout[i].d, mout[i].len);
    }

    /* 输出响度 */
    f->m_loudOut.Process((const float *const *)po, pout, frames);

    /* 输出电平 + 平滑 + 峰值线（与输入同参数：条 release τ≈80ms / 峰值线 τ=保持时长） */
    for (int c = 0; c < pout && c < 32; c++)
    {
        const float *buf = po[c];
        float pk = 0.f;
        for (int i = 0; i < frames; i++)
        {
            float a = buf[i];
            if (a < 0.f) a = -a;
            if (a > pk) pk = a;
        }
        float cur = f->m_outLevel[c];
        if (pk >= cur)
            cur = cur + (pk - cur) * att; /* 平滑上升（attack） */
        else
            cur = pk + (cur - pk) * rel;  /* 平滑下降（release） */
        if (cur < 0.001f) cur = 0.f;      /* 静音归零（<-60dB） */
        f->m_outLevel[c] = cur;
        /* 峰值线：平滑上升（快）/ 平滑下降（τ=保持时长），与条同源不瞬跳 */
        float hd = f->m_outHold[c];
        if (pk >= hd)
            hd = hd + (pk - hd) * att;
        else
            hd = pk + (hd - pk) * relH;
        if (hd < 0.001f) hd = 0.f;
        f->m_outHold[c] = hd;
    }
}

/*****************************************************************************/
/* ProcessMidiEvent : 实时回调内把一条 MIDI 事件喂给插件                       */
/*   通道过滤（m_midiChannel，0=Omni）+ CC->参数映射 + SendMidiIn             */
/*****************************************************************************/
void CMainFrame::ProcessMidiEvent(IPlugin *p, const unsigned char *mev, int mlen)
{
    if (!p || mlen < 1)
        return;
    /* MIDI 通道过滤：0=Omni（全部），否则只接受指定通道 */
    int evch = (mev[0] & 0x0F) + 1;
    if (m_midiChannel != 0 && evch != m_midiChannel)
        return;
    /* CC -> 插件参数映射（外部控制器） */
    if ((mev[0] & 0xF0) == 0xB0 && mlen >= 3 && !m_midiMap.empty())
    {
        int mch = (mev[0] & 0x0F) + 1;
        int mcc = mev[1];
        float mval = mev[2] / 127.0f;
        EnterCriticalSection(&m_midiMapCs);
        for (size_t i = 0; i < m_midiMap.size(); i++)
        {
            if (m_midiMap[i].ch == mch && m_midiMap[i].cc == mcc &&
                m_midiMap[i].param >= 0)
                p->SetParam(m_midiMap[i].param, mval);
        }
        LeaveCriticalSection(&m_midiMapCs);
    }
    p->SendMidiIn(mev, mlen);
}

/*****************************************************************************/
/* OnBackendSelect : 音频后端 Radio（ASIO / JACK）选择                        */
/*****************************************************************************/
void CMainFrame::OnBackendSelect(UINT nID)
{
    ApplyBackendMode((nID == IDM_AUDIO_BACKEND_JACK) ? 1 : 0);
}

/*****************************************************************************/
/* ApplyBackendMode : 写 ini [View] backend + 弹窗询问是否重启生效            */
/*   切换后端需重启 vsthost 才彻底生效（避免原地切换的残留问题），逻辑更简单    */
/*****************************************************************************/
void CMainFrame::ApplyBackendMode(int mode)
{
    if (mode != 0)
        mode = 1;
    m_backendMode = mode;
    WritePrivateProfileStringW(L"View", L"backend", mode ? L"1" : L"0",
                               AsioConfigPath().c_str());
    if (GetMenu())
        GetMenu()->CheckMenuRadioItem(IDM_AUDIO_BACKEND_ASIO,
                                      IDM_AUDIO_BACKEND_JACK,
                                      mode ? IDM_AUDIO_BACKEND_JACK
                                           : IDM_AUDIO_BACKEND_ASIO,
                                      MF_BYCOMMAND);
    /* 直接弹窗：是否重启 vsthost 使切换生效 */
    if (AfxMessageBox(_T("切换音频后端需要重启 vsthost 才能生效。\n是否立即重启？"),
                      MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES)
        RestartHost();
}

/*****************************************************************************/
/* RestartHost : 保存状态 + 重启自身（新实例同名自动加载、按 ini 用新后端；   */
/*   通过 --ordinal N 继承当前实例序号，避免状态文件变成 _2 丢失预设）         */
/*****************************************************************************/
void CMainFrame::RestartHost()
{
    if (m_pHost && m_pHost->IsLoaded())
        m_pHost->SaveStateFile();
    StopAudio();
    RemoveTrayIcon();

    int ord = GetInstanceOrdinal();
    /* 用环境变量传序号给子进程（CreateProcess 默认继承父进程环境），
       不能加命令行参数——MFC ParseCommandLine 会把裸数字当文件路径，
       导致重启后不走同名自动加载而加载失败 */
    wchar_t ordStr[16];
    swprintf(ordStr, 16, L"%d", ord);
    SetEnvironmentVariableW(L"VSTHOST_ORDINAL", ordStr);

    wchar_t szExe[1024];
    GetModuleFileNameW(NULL, szExe, 1024);
    wchar_t cmd[2048];
    swprintf(cmd, 2048, L"\"%s\"", szExe);

    STARTUPINFOW si = { 0 };
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = { 0 };
    if (CreateProcessW(szExe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        /* 新实例已启动，退出当前进程（新实例同名自动加载 + 按 ini 用新后端） */
        CFrameWnd::OnClose();
    }
    else
    {
        AfxMessageBox(_T("重启失败。请手动关闭后重新运行 vsthost。"),
                      MB_OK | MB_ICONERROR);
    }
}

/*****************************************************************************/
/* OnJackShutdown : JACK 服务器退出（shutdown 回调通知，UI 线程）              */
/*****************************************************************************/
LRESULT CMainFrame::OnJackShutdown(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    if (m_pJack && m_pJack->IsRunning())
    {
        StopAudio();
        AfxMessageBox(_T("JACK 服务器已退出，音频已停止。\n"
                         "请重新启动 JACK2 服务器后再试。"),
                      MB_OK | MB_ICONWARNING);
    }
    return 0;
}

/*****************************************************************************/
/* OnJackBufsize : JACK 块大小变化（通知线程，UI 线程重启音频以重新 setup）    */
/*   防抖：仅在实际变化且距上次重启超过 500ms 时处理，避免服务器重复通知       */
/*   （如激活时回调当前值）导致 StopAudio/StartAudio 无限循环                 */
/*****************************************************************************/
LRESULT CMainFrame::OnJackBufsize(WPARAM wParam, LPARAM /*lParam*/)
{
    if (m_pJack && m_pJack->IsRunning() && m_pHost && m_pHost->IsLoaded())
    {
        int newSize = (int)wParam;
        if (newSize <= 0 || newSize == m_pJack->GetBufferSize())
            return 0;                       /* 无变化，忽略 */
        DWORD now = ::GetTickCount();
        if (now - m_lastJackBufsizeTick < 500)
            return 0;                       /* 500ms 防抖 */
        m_lastJackBufsizeTick = now;
        StopAudio();
        StartAudio();
    }
    return 0;
}

/*****************************************************************************/
/* RebuildAsioMenu : 填充“音频->ASIO 设备”子菜单                              */
/*****************************************************************************/
void CMainFrame::RebuildAsioMenu()
{
    if (!m_hAsioMenu)
        return;
    while (::GetMenuItemCount(m_hAsioMenu) > 0)
        ::RemoveMenu(m_hAsioMenu, 0, MF_BYPOSITION);

    CAsioBackend::EnumerateDrivers(m_asioDrivers);
    size_t maxItems = IDM_AUDIO_MAX - IDM_AUDIO_BASE;
    if (m_asioDrivers.empty())
    {
        ::AppendMenu(m_hAsioMenu, MF_STRING | MF_GRAYED, IDM_AUDIO_BASE,
                     _T("（无 ASIO 驱动）"));
        return;
    }
    /* 当前选择：运行时 m_asioCurrent；未运行时按记忆驱动（[asio] driver）勾选，
       保证切回 ASIO 后设备选择恢复 */
    std::string cur = m_asioCurrent;
    if (cur.empty())
    {
        wchar_t wbuf[256] = L"";
        GetPrivateProfileStringW(L"asio", L"driver", L"", wbuf, 256,
                                 AsioConfigPath().c_str());
        char abuf[256] = "";
        WideCharToMultiByte(CP_ACP, 0, wbuf, -1, abuf, 256, NULL, NULL);
        cur = abuf;
    }
    for (size_t i = 0; i < m_asioDrivers.size() && i < maxItems; i++)
    {
        CString name(m_asioDrivers[i].c_str());     /* ANSI -> Unicode */
        UINT flags = MF_STRING;
        if (!cur.empty() &&
            _stricmp(m_asioDrivers[i].c_str(), cur.c_str()) == 0)
            flags |= MF_CHECKED;
        ::AppendMenu(m_hAsioMenu, flags, IDM_AUDIO_BASE + (UINT)i, name);
    }
}

/*****************************************************************************/
/* UpdateAsioStatus : 状态栏 pane0 = DSP 固定（左下角）；pane2 = 通道+ASIO+测试 */
/*****************************************************************************/
void CMainFrame::UpdateAsioStatus()
{
    if (!m_wndStatusBar.m_hWnd)
        return;
    /* pane0：DSP 使用率（左下角固定宽 96px，不可截断） */
    CString d;
    if (m_pJack && m_pJack->IsRunning())
        d.Format(_T("DSP %.0f%%"), m_pJack->GetDspUsage());
    else if (m_pAsio && m_pAsio->IsRunning())
        d.Format(_T("DSP %.0f%%"), m_pAsio->GetDspUsage());
    else
        d = _T("DSP --");
    m_wndStatusBar.SetPaneText(0, d);

    /* pane1：中间占位（stretch）—— JACK 后端时显示客户端名 */
    if (m_pBackend == m_pJack && m_pJack && m_pJack->IsRunning())
    {
        CString bk;
        bk.Format(_T("JACK: %S"), m_pJack->GetDriverName());
        m_wndStatusBar.SetPaneText(1, bk);
    }
    else
        m_wndStatusBar.SetPaneText(1, _T(""));

    /* pane2：通道数（右下角固定宽 118px，不可截断） */
    CString ch;
    if (m_pHost && m_pHost->IsLoaded())
    {
        IPlugin *p = m_pHost->Get();
        ch.Format(_T("%d 进 / %d 出"),
                  p->GetInputChannels(), p->GetOutputChannels());
    }
    else
        ch = _T("0 进 / 0 出");
    m_wndStatusBar.SetPaneText(2, ch);
}

/*****************************************************************************/
/* 音频菜单命令                                                               */
/*****************************************************************************/
void CMainFrame::OnAsioSelect(UINT nID)
{
    int idx = (int)(nID - IDM_AUDIO_BASE);
    if (idx < 0 || idx >= (int)m_asioDrivers.size())
        return;
    std::string drv = m_asioDrivers[idx];

    if (!m_pHost || !m_pHost->IsLoaded())
    {
        /* 无插件：仅记忆偏好（下次加载插件时生效） */
        SaveAsioConfig(drv.c_str());
        m_asioCurrent = drv;
        RebuildAsioMenu();
        return;
    }
    /* 有插件：切换驱动（StopAudio 会 Close；StartAudio 按记忆选择新驱动） */
    if (m_pAsio)
        m_pAsio->Close();
    SaveAsioConfig(drv.c_str());
    StartAudio();
}

void CMainFrame::OnAsioControlPanel()
{
    if (m_pAsio && m_pAsio->IsOpen())
        m_pAsio->ControlPanel();
    else
        AfxMessageBox(_T("请先加载插件并启动音频，再打开驱动控制面板。"),
                      MB_OK | MB_ICONINFORMATION);
}

void CMainFrame::OnAsioRefresh()
{
    CAsioBackend::EnumerateDrivers(m_asioDrivers);
    RebuildAsioMenu();
}

/*****************************************************************************/
/* LoadAsioMap / SaveAsioMap : 通道映射（[AsioMap] 存 exe ini，-1=静音/丢弃） */
/*****************************************************************************/
void CMainFrame::LoadAsioMap(int plugIn, int plugOut, int asioIn, int asioOut)
{
    m_mapIn.assign(plugIn, -1);
    m_mapOut.assign(plugOut, -1);
    std::wstring ini = AsioConfigPath();
    for (int c = 0; c < plugIn; c++)
    {
        wchar_t key[16];
        swprintf(key, 16, L"in%d", c);
        int v = GetPrivateProfileIntW(L"AsioMap", key, -1, ini.c_str());
        m_mapIn[c] = (v >= 0 && v < asioIn) ? v : (c < asioIn ? c : -1);
    }
    for (int c = 0; c < plugOut; c++)
    {
        wchar_t key[16];
        swprintf(key, 16, L"out%d", c);
        int v = GetPrivateProfileIntW(L"AsioMap", key, -1, ini.c_str());
        m_mapOut[c] = (v >= 0 && v < asioOut) ? v : (c < asioOut ? c : -1);
    }
}

void CMainFrame::SaveAsioMap() const
{
    std::wstring ini = AsioConfigPath();
    wchar_t key[16], val[16];
    for (int c = 0; c < (int)m_mapIn.size(); c++)
    {
        swprintf(key, 16, L"in%d", c);
        swprintf(val, 16, L"%d", m_mapIn[c]);
        WritePrivateProfileStringW(L"AsioMap", key, val, ini.c_str());
    }
    for (int c = 0; c < (int)m_mapOut.size(); c++)
    {
        swprintf(key, 16, L"out%d", c);
        swprintf(val, 16, L"%d", m_mapOut[c]);
        WritePrivateProfileStringW(L"AsioMap", key, val, ini.c_str());
    }
}

/*****************************************************************************/
/* OnAsioMap : 打开 ASIO 通道分配对话框                                       */
/*****************************************************************************/
void CMainFrame::OnAsioMap()
{
    if (!m_pAsio || !m_pAsio->IsOpen())
    {
        AfxMessageBox(_T("请先加载插件并启动音频（ASIO 已打开）后，再进行通道分配。"),
                      MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!m_pHost || !m_pHost->IsLoaded())
        return;
    IPlugin *p = m_pHost->Get();
    int pin = p->GetInputChannels();
    int pout = p->GetOutputChannels();

    /* ASIO 通道名（含驱动报告的通道名） */
    std::vector<std::string> inNames, outNames;
    int asi = m_pAsio->GetInputChannels();
    int aso = m_pAsio->GetOutputChannels();
    for (int i = 0; i < asi; i++)
    {
        char buf[96];
        sprintf_s(buf, "in_%d (%s)", i + 1, m_pAsio->GetChannelName(i, true));
        inNames.push_back(buf);
    }
    for (int i = 0; i < aso; i++)
    {
        char buf[96];
        sprintf_s(buf, "out_%d (%s)", i + 1, m_pAsio->GetChannelName(i, false));
        outNames.push_back(buf);
    }

    CAsioMapDialog dlg(inNames, outNames, pin, pout, m_mapIn, m_mapOut);
    if (dlg.DoModal() != IDOK)
        return;
    SaveAsioMap();
    UpdateAsioStatus();
}

/*****************************************************************************/
/* OnFileSettings : 打开全局设置窗口（关闭行为 + 音频/MIDI 设置）             */
/*****************************************************************************/
void CMainFrame::OnFileSettings()
{
    CGlobalSettingsDlg dlg(this);
    dlg.DoModal();
}

/*****************************************************************************/
/* ASIO 采样率/缓冲/MIDI 输入访问器（默认 = ini 记忆或当前驱动实际值）        */
/*****************************************************************************/
double CMainFrame::AsioSampleRate() const
{
    int v = GetPrivateProfileIntW(L"asio", L"samplerate", 0,
                                  AsioConfigPath().c_str());
    if (v > 0)
        return (double)v;
    return (m_pAsio && m_pAsio->GetSampleRate() > 0)
               ? m_pAsio->GetSampleRate() : 44100.0;
}

int CMainFrame::AsioBufferSize() const
{
    int v = GetPrivateProfileIntW(L"asio", L"buffersize", 0,
                                  AsioConfigPath().c_str());
    if (v > 0)
        return v;
    return m_pAsio ? m_pAsio->GetBufferSize() : 512;
}

bool CMainFrame::MidiInputEnabled() const
{
    return GetPrivateProfileIntW(L"asio", L"midiinput", 0,
                                 AsioConfigPath().c_str()) != 0;
}

int CMainFrame::MidiDeviceIndex() const
{
    return GetPrivateProfileIntW(L"asio", L"mididevice", 0,
                                 AsioConfigPath().c_str());
}

int CMainFrame::MidiChannel() const
{
    int ch = GetPrivateProfileIntW(L"asio", L"midichannel", 0,
                                   AsioConfigPath().c_str());
    if (ch < 0 || ch > 16)
        ch = 0;
    return ch;
}

bool CMainFrame::MidiOutputEnabled() const
{
    return GetPrivateProfileIntW(L"asio", L"midiout", 0,
                                 AsioConfigPath().c_str()) != 0;
}

int CMainFrame::MidiOutputDeviceIndex() const
{
    return GetPrivateProfileIntW(L"asio", L"midioutdevice", 0,
                                 AsioConfigPath().c_str());
}

/*****************************************************************************/
/* ApplyAsioSettings : 写 ini + 重启音频应用新采样率/缓冲/MIDI               */
/*****************************************************************************/
void CMainFrame::ApplyAsioSettings(double rate, int buf, bool midiOn, int midiDev,
                                   int midiCh, bool midiOutOn, int midiOutDev)
{
    wchar_t b[32];
    swprintf(b, 32, L"%.0f", rate);
    WritePrivateProfileStringW(L"asio", L"samplerate", b, AsioConfigPath().c_str());
    swprintf(b, 32, L"%d", buf);
    WritePrivateProfileStringW(L"asio", L"buffersize", b, AsioConfigPath().c_str());
    WritePrivateProfileStringW(L"asio", L"midiinput", midiOn ? L"1" : L"0",
                               AsioConfigPath().c_str());
    swprintf(b, 32, L"%d", midiDev);
    WritePrivateProfileStringW(L"asio", L"mididevice", b, AsioConfigPath().c_str());
    if (midiCh < 0) midiCh = 0;
    if (midiCh > 16) midiCh = 0;
    swprintf(b, 32, L"%d", midiCh);
    WritePrivateProfileStringW(L"asio", L"midichannel", b, AsioConfigPath().c_str());
    /* MIDI 输出（MME，ASIO 后端用） */
    WritePrivateProfileStringW(L"asio", L"midiout", midiOutOn ? L"1" : L"0",
                               AsioConfigPath().c_str());
    swprintf(b, 32, L"%d", midiOutDev);
    WritePrivateProfileStringW(L"asio", L"midioutdevice", b, AsioConfigPath().c_str());

    /* 音频正在运行则重启以应用新采样率/缓冲/MIDI */
    if (m_pAsio && m_pAsio->IsOpen())
    {
        StopAudio();
        StartAudio();
    }
    else
    {
        /* 未运行：仅立即应用 MIDI 设置 */
        m_midiEnabled = midiOn;
        m_midiDevice = midiDev;
        m_midiChannel = midiCh;
        if (midiOn)
            m_midiInput.Open(midiDev);
        else
            m_midiInput.Close();
        m_midiOutEnabled = midiOutOn;
        m_midiOutDevice = midiOutDev;
        if (midiOutOn)
            m_midiOut.Open(midiOutDev);
        else
            m_midiOut.Close();
    }
}

/*****************************************************************************/
/* ApplyMidiChannel : 仅应用 MIDI 通道（JACK 模式下 ASIO/MIDI 其他设置失效）  */
/*****************************************************************************/
void CMainFrame::ApplyMidiChannel(int midiCh)
{
    if (midiCh < 0) midiCh = 0;
    if (midiCh > 16) midiCh = 0;
    wchar_t b[8];
    swprintf(b, 8, L"%d", midiCh);
    WritePrivateProfileStringW(L"asio", L"midichannel", b,
                               AsioConfigPath().c_str());
    m_midiChannel = midiCh;   /* 实时回调读，立即生效 */
}

/*****************************************************************************/
/* CloseAction : 关闭行为（0=每次询问 1=最小化到托盘 2=完全关闭，[View]）      */
/*****************************************************************************/
int CMainFrame::CloseAction() const
{
    int a = GetPrivateProfileIntW(L"View", L"closeaction", 0,
                                  AsioConfigPath().c_str());
    if (a < 0 || a > 2)
        a = 0;
    return a;
}

void CMainFrame::ApplyCloseAction(int action)
{
    if (action < 0 || action > 2)
        action = 0;
    wchar_t b[8];
    swprintf(b, 8, L"%d", action);
    WritePrivateProfileStringW(L"View", L"closeaction", b,
                               AsioConfigPath().c_str());
}

/*****************************************************************************/
/* 托盘图标：隐藏窗口后驻留系统托盘，双击/左键恢复，右键弹出菜单              */
/*****************************************************************************/
void CMainFrame::AddTrayIcon()
{
    if (m_bTrayVisible)
        return;
    memset(&m_nid, 0, sizeof(m_nid));
    m_nid.cbSize = sizeof(m_nid);
    m_nid.hWnd = m_hWnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon = AfxGetApp()->LoadIcon(IDR_APP_ICON);
    std::wstring tip = ComputeHostName();
    wcsncpy_s(m_nid.szTip, tip.c_str(), _TRUNCATE);
    m_bTrayVisible = Shell_NotifyIconW(NIM_ADD, &m_nid) != FALSE;
}

void CMainFrame::RemoveTrayIcon()
{
    if (!m_bTrayVisible)
        return;
    Shell_NotifyIconW(NIM_DELETE, &m_nid);
    m_bTrayVisible = false;
}

void CMainFrame::MinimizeToTray()
{
    AddTrayIcon();
    ShowWindow(SW_HIDE);
}

void CMainFrame::RestoreFromTray()
{
    RemoveTrayIcon();
    ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    SetFocus();
}

/*****************************************************************************/
/* OnTrayIcon : 托盘回调（左键/双击恢复；右键弹出菜单）                        */
/*****************************************************************************/
LRESULT CMainFrame::OnTrayIcon(WPARAM wParam, LPARAM lParam)
{
    if (lParam == WM_LBUTTONDBLCLK || lParam == WM_LBUTTONUP)
    {
        RestoreFromTray();
        return 0;
    }
    if (lParam == WM_RBUTTONUP)
    {
        CMenu menu;
        menu.CreatePopupMenu();
        menu.AppendMenu(MF_STRING, IDM_TRAY_SHOW, _T("显示窗口(&S)"));
        menu.AppendMenu(MF_SEPARATOR);
        menu.AppendMenu(MF_STRING, IDM_TRAY_EXIT, _T("退出(&X)"));
        POINT pt;
        GetCursorPos(&pt);
        ::SetForegroundWindow(m_hWnd);
        menu.TrackPopupMenu(TPM_RIGHTBUTTON | TPM_LEFTALIGN,
                            pt.x, pt.y, this);
        PostMessage(WM_NULL, 0, 0);   /* 让菜单正常消失 */
    }
    return 0;
}

/*****************************************************************************/
/* OnTrayShow : 托盘菜单“显示窗口”                                            */
/*****************************************************************************/
void CMainFrame::OnTrayShow()
{
    RestoreFromTray();
}

/*****************************************************************************/
/* OnTrayExit : 托盘菜单“退出”（直接完全关闭）                                */
/*****************************************************************************/
void CMainFrame::OnTrayExit()
{
    RealClose();
}

/*****************************************************************************/
/* OnClose : 按关闭行为分发（询问 / 最小化到托盘 / 完全关闭）                  */
/*****************************************************************************/
void CMainFrame::OnClose()
{
    int action = CloseAction();
    if (action == 1)              /* 最小化到托盘 */
    {
        MinimizeToTray();
        return;
    }
    if (action == 2)              /* 完全关闭 */
    {
        RealClose();
        return;
    }

    /* 每次询问：最小化到托盘 / 完全关闭 + 不再提示 */
    CClosePromptDlg dlg(this);
    if (dlg.DoModal() != IDOK)
        return;                   /* 取消：不动作 */
    if (dlg.m_result == 1)
    {
        if (dlg.m_dontAsk)
            ApplyCloseAction(1);
        MinimizeToTray();
    }
    else if (dlg.m_result == 2)
    {
        if (dlg.m_dontAsk)
            ApplyCloseAction(2);
        RealClose();
    }
}

/*****************************************************************************/
/* OnSysCommand : 最小化按钮（关闭行为=托盘时隐藏到托盘）                      */
/*****************************************************************************/
void CMainFrame::OnSysCommand(UINT nID, LPARAM lParam)
{
    if ((nID & 0xFFF0) == SC_MINIMIZE && CloseAction() == 1)
    {
        MinimizeToTray();
        return;
    }
    CFrameWnd::OnSysCommand(nID, lParam);
}

/*****************************************************************************/
/* RealClose : 真正退出（停音频 + 移除托盘图标 + 关闭窗口）                     */
/*****************************************************************************/
void CMainFrame::RealClose()
{
    StopAudio();
    RemoveTrayIcon();
    CFrameWnd::OnClose();
}

/*****************************************************************************/
/* 电平表：显示开关 / 布局 / 绘制                                              */
/*****************************************************************************/
void CMainFrame::ShowMeters(bool show)
{
    m_bShowMeters = show;
    if (GetMenu())
        GetMenu()->CheckMenuItem(IDM_VIEW_METERS,
                                 MF_BYCOMMAND | (show ? MF_CHECKED : MF_UNCHECKED));
    wchar_t buf[8];
    swprintf(buf, 8, L"%d", show ? 1 : 0);
    WritePrivateProfileStringW(L"View", L"meters", buf, AsioConfigPath().c_str());

    /* 重排布局（view 左右让出/收回电平表区域）+ 重绘 */
    CRect rc;
    GetClientRect(&rc);
    OnSize(SIZE_RESTORED, rc.Width(), rc.Height());
    Invalidate(FALSE);
}

void CMainFrame::OnViewMeters()
{
    ShowMeters(!m_bShowMeters);
}

/*****************************************************************************/
/* ApplyPeakMenu : 峰值保持菜单勾选（按当前配置）                             */
/*****************************************************************************/
void CMainFrame::ApplyPeakMenu()
{
    if (!m_hPeakMenu)
        return;
    for (int i = 0; i < 5; i++)
        ::CheckMenuItem(m_hPeakMenu, IDM_VIEW_PEAK_BASE + i,
                        MF_BYCOMMAND | MF_UNCHECKED);
    int idx = 2;                        /* 默认 1 秒 */
    if (m_peakHoldSeconds <= 0.0) idx = 0;
    else if (m_peakHoldSeconds <= 0.5) idx = 1;
    else if (m_peakHoldSeconds <= 1.0) idx = 2;
    else if (m_peakHoldSeconds <= 2.0) idx = 3;
    else idx = 4;
    ::CheckMenuItem(m_hPeakMenu, IDM_VIEW_PEAK_BASE + idx,
                    MF_BYCOMMAND | MF_CHECKED);
}

/*****************************************************************************/
/* OnPeakSelect : 峰值保持时长选择（0 / 0.5 / 1 / 2 / 5 秒）                  */
/*****************************************************************************/
void CMainFrame::OnPeakSelect(UINT nID)
{
    int idx = (int)(nID - IDM_VIEW_PEAK_BASE);
    if (idx < 0 || idx > 4)
        return;
    static const double secs[] = { 0.0, 0.5, 1.0, 2.0, 5.0 };
    m_peakHoldSeconds = secs[idx];

    /* 记入配置（[View] peakhold，×10 存） */
    wchar_t buf[16];
    swprintf(buf, 16, L"%d", (int)(m_peakHoldSeconds * 10));
    WritePrivateProfileStringW(L"View", L"peakhold", buf,
                               AsioConfigPath().c_str());

    /* 切换时长时清零保持值，按新时长重新计时 */
    memset((void *)m_inHold, 0, sizeof(m_inHold));
    memset((void *)m_outHold, 0, sizeof(m_outHold));
    memset((void *)m_inHoldFrames, 0, sizeof(m_inHoldFrames));
    memset((void *)m_outHoldFrames, 0, sizeof(m_outHoldFrames));

    ApplyPeakMenu();
}

/*****************************************************************************/
/* ApplyRefreshMenu : 电平表刷新频率菜单勾选（按当前配置）                    */
/*****************************************************************************/
void CMainFrame::ApplyRefreshMenu()
{
    if (!m_hRefreshMenu)
        return;
    for (int i = 0; i < 4; i++)
        ::CheckMenuItem(m_hRefreshMenu, IDM_VIEW_REFRESH_BASE + i,
                        MF_BYCOMMAND | MF_UNCHECKED);
    int idx = 1;                        /* 默认 50 ms */
    if (m_meterRefreshMs <= 30) idx = 0;
    else if (m_meterRefreshMs <= 50) idx = 1;
    else if (m_meterRefreshMs <= 80) idx = 2;
    else idx = 3;
    ::CheckMenuItem(m_hRefreshMenu, IDM_VIEW_REFRESH_BASE + idx,
                    MF_BYCOMMAND | MF_CHECKED);
}

/*****************************************************************************/
/* RefreshMeterTimer : 按当前刷新周期重设电平表定时器（ID 2）                 */
/*****************************************************************************/
void CMainFrame::RefreshMeterTimer()
{
    if (m_hWnd)
    {
        KillTimer(2);
        SetTimer(2, (UINT)m_meterRefreshMs, NULL);
    }
}

/*****************************************************************************/
/* OnRefreshSelect : 电平表刷新频率选择（30 / 50 / 80 / 100 ms）              */
/*****************************************************************************/
void CMainFrame::OnRefreshSelect(UINT nID)
{
    int idx = (int)(nID - IDM_VIEW_REFRESH_BASE);
    if (idx < 0 || idx > 3)
        return;
    static const int ms[] = { 30, 50, 80, 100 };
    m_meterRefreshMs = ms[idx];

    /* 记入配置（[View] meterrefresh，ms） */
    wchar_t buf[16];
    swprintf(buf, 16, L"%d", m_meterRefreshMs);
    WritePrivateProfileStringW(L"View", L"meterrefresh", buf,
                               AsioConfigPath().c_str());

    RefreshMeterTimer();
    /* 独立响度表窗口同步刷新周期（与峰值表一致） */
    if (m_pMeterDlg)
        m_pMeterDlg->ResyncTimer();
    ApplyRefreshMenu();
}

/*****************************************************************************/
/* CSV 响度日志：开关 / 间隔 / 文件夹 / 重置开新文件（文件名=开始系统时间）    */
/*****************************************************************************/
void CMainFrame::ApplyCsvSettings(bool log, int intervalMs, const std::wstring &folder)
{
    m_bCsvLog = log;
    m_csvIntervalMs = intervalMs;
    m_csvFolder = folder;

    wchar_t buf[16];
    swprintf(buf, 16, L"%d", log ? 1 : 0);
    WritePrivateProfileStringW(L"View", L"csvlog", buf, AsioConfigPath().c_str());
    swprintf(buf, 16, L"%d", m_csvIntervalMs);
    WritePrivateProfileStringW(L"View", L"csvinterval", buf, AsioConfigPath().c_str());
    WritePrivateProfileStringW(L"View", L"csvfolder", m_csvFolder.c_str(),
                               AsioConfigPath().c_str());

    if (m_hWnd)
    {
        KillTimer(3);
        SetTimer(3, (UINT)m_csvIntervalMs, NULL);
    }
    if (m_bCsvLog)
        OpenCsvFile();
    else
        CloseCsvFile();
}

void CMainFrame::SetCsvFolder(const std::wstring &f)
{
    m_csvFolder = f;
    WritePrivateProfileStringW(L"View", L"csvfolder", m_csvFolder.c_str(),
                               AsioConfigPath().c_str());
}

void CMainFrame::OpenCsvFile()
{
    CloseCsvFile();
    if (!m_bCsvLog)
        return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t name[80];
    swprintf(name, 80, L"loudness_%04d%02d%02d_%02d%02d%02d.csv",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    std::wstring path = m_csvFolder;
    if (!path.empty() && path.back() != L'\\')
        path += L'\\';
    path += name;
    _wfopen_s(&m_csvFile, path.c_str(), L"w, ccs=UTF-8");
    if (m_csvFile)
    {
        m_csvPath = path;
        fwprintf(m_csvFile,
                 L"Time,In_Momentary,In_Short,In_Integ,In_LRA,In_TP,"
                 L"Out_Momentary,Out_Short,Out_Integ,Out_LRA,Out_TP\n");
    }
}

void CMainFrame::CloseCsvFile()
{
    if (m_csvFile)
    {
        fclose(m_csvFile);
        m_csvFile = NULL;
    }
    m_csvPath.clear();
}

void CMainFrame::WriteCsvRow()
{
    if (!m_csvFile)
        return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    LoudnessCore::Result ri = m_loudIn.GetResult();
    LoudnessCore::Result ro = m_loudOut.GetResult();
    double tpIn = (ri.peak > 0.000001) ? 20.0 * log10(ri.peak) : -70.0;
    double tpOut = (ro.peak > 0.000001) ? 20.0 * log10(ro.peak) : -70.0;
    fwprintf(m_csvFile,
             L"%02d:%02d:%02d,%.2f,%.2f,%.2f,%.2f,%.2f,"
             L"%.2f,%.2f,%.2f,%.2f,%.2f\n",
             st.wHour, st.wMinute, st.wSecond,
             ri.momentary, ri.shortTerm, ri.integrated, ri.lra, tpIn,
             ro.momentary, ro.shortTerm, ro.integrated, ro.lra, tpOut);
    fflush(m_csvFile);
}

/*****************************************************************************/
/* OnMeterSettings : 打开电平表设置窗口（模态）                              */
/*****************************************************************************/
void CMainFrame::OnMeterSettings()
{
    CMeterSettingsDlg dlg(this);
    dlg.DoModal();
}

/*****************************************************************************/
/* OnMeterWindow : 开关独立电平表窗口（无模式 toggle）                        */
/*****************************************************************************/
void CMainFrame::OnMeterWindow()
{
    if (m_pMeterDlg)
    {
        m_pMeterDlg->DestroyWindow();   /* PostNcDestroy -> OnMeterWindowClosed */
        return;
    }
    m_pMeterDlg = new CLevelMeterDlg(this);
    if (!m_pMeterDlg->Create(this))
    {
        delete m_pMeterDlg;
        m_pMeterDlg = NULL;
        return;
    }
    m_pMeterDlg->ShowWindow(SW_SHOW);
    if (GetMenu())
        GetMenu()->CheckMenuItem(IDM_VIEW_METER_WINDOW,
                                 MF_BYCOMMAND | MF_CHECKED);
}

/*****************************************************************************/
/* OnMeterWindowClosed : 独立窗口销毁回调（置空指针 + 取消勾选）              */
/*****************************************************************************/
void CMainFrame::OnMeterWindowClosed()
{
    m_pMeterDlg = NULL;
    if (GetMenu())
        GetMenu()->CheckMenuItem(IDM_VIEW_METER_WINDOW,
                                 MF_BYCOMMAND | MF_UNCHECKED);
}

/*****************************************************************************/
/* SetLoudnessStd : 响度标准（0=BS.1770-4 1=EBU R128 2=ATSC A/85）           */
/*****************************************************************************/
void CMainFrame::SetLoudnessStd(int std)
{
    if (std < 0) std = 0;
    if (std >= g_loudnessStdCount) std = g_loudnessStdCount - 1;
    m_loudnessStd = std;
    wchar_t buf[8];
    swprintf(buf, 8, L"%d", std);
    WritePrivateProfileStringW(L"View", L"loudnessstd", buf,
                               AsioConfigPath().c_str());
}

/*****************************************************************************/
/* SetSilenceReset : Integrated 静音重置时长（0=不重置）                      */
/*****************************************************************************/
void CMainFrame::SetSilenceReset(double sec)
{
    m_silenceReset = sec;
    m_loudIn.SetSilenceReset(sec);
    m_loudOut.SetSilenceReset(sec);
    wchar_t buf[16];
    swprintf(buf, 16, L"%d", (int)sec);
    WritePrivateProfileStringW(L"View", L"lra_silreset", buf,
                               AsioConfigPath().c_str());
}

/*****************************************************************************/
/* SetSilenceThresh : 静音阈值（LUFS）                                       */
/*****************************************************************************/
void CMainFrame::SetSilenceThresh(double lufs)
{
    m_silenceThresh = lufs;
    m_loudIn.SetSilenceThreshold(lufs);
    m_loudOut.SetSilenceThreshold(lufs);
    wchar_t buf[16];
    swprintf(buf, 16, L"%d", (int)lufs);
    WritePrivateProfileStringW(L"View", L"silence_thresh", buf,
                               AsioConfigPath().c_str());
}

/*****************************************************************************/
/* ResetLoudness : 手动重置 I/O 响度累积（Integrated/LRA）                    */
/*****************************************************************************/
void CMainFrame::ResetLoudness()
{
    m_loudIn.Reset();
    m_loudOut.Reset();
    if (m_bCsvLog)
        OpenCsvFile();   /* 重置后直接开新文件记录 */
}

/*****************************************************************************/
/* ApplyMeterSettings : 电平表设置窗口确定时统一应用                          */
/*****************************************************************************/
void CMainFrame::ApplyMeterSettings(bool show, int refreshMs, double peakHold,
                                    bool peakLine, bool valueBox, int std,
                                    double silence, double silenceThresh)
{
    m_bPeakLine = peakLine;
    m_bValueBox = valueBox;
    m_meterRefreshMs = refreshMs;
    m_peakHoldSeconds = peakHold;

    /* 峰值保持时长变化时清零保持值 */
    memset((void *)m_inHold, 0, sizeof(m_inHold));
    memset((void *)m_outHold, 0, sizeof(m_outHold));
    memset((void *)m_inHoldFrames, 0, sizeof(m_inHoldFrames));
    memset((void *)m_outHoldFrames, 0, sizeof(m_outHoldFrames));

    /* 写配置 + 应用 */
    wchar_t buf[16];
    swprintf(buf, 16, L"%d", peakLine ? 1 : 0);
    WritePrivateProfileStringW(L"View", L"peakline", buf, AsioConfigPath().c_str());
    swprintf(buf, 16, L"%d", valueBox ? 1 : 0);
    WritePrivateProfileStringW(L"View", L"metervalue", buf, AsioConfigPath().c_str());
    swprintf(buf, 16, L"%d", (int)(peakHold * 10));
    WritePrivateProfileStringW(L"View", L"peakhold", buf, AsioConfigPath().c_str());

    ShowMeters(show);
    SetLoudnessStd(std);
    SetSilenceReset(silence);
    SetSilenceThresh(silenceThresh);
    RefreshMeterTimer();
    /* 独立响度表窗口同步刷新周期（与峰值表一致） */
    if (m_pMeterDlg)
        m_pMeterDlg->ResyncTimer();
    ApplyRefreshMenu();
    ApplyPeakMenu();
    Invalidate(FALSE);
}

/*****************************************************************************/
/* MeterInCh / MeterOutCh : 每侧通道数（供独立电平表窗口）                    */
/*****************************************************************************/
int CMainFrame::MeterInCh() const
{
    /* 乐器（VSTi）不显示输入电平表 */
    if (m_pHost && m_pHost->IsLoaded() && m_pHost->Get()->IsInstrument())
        return 0;
    int n = m_pHost && m_pHost->IsLoaded() ? m_pHost->Get()->GetInputChannels() : 0;
    if (n > METER_MAX_CH) n = METER_MAX_CH;
    return n;
}

int CMainFrame::MeterOutCh() const
{
    int n = m_pHost && m_pHost->IsLoaded() ? m_pHost->Get()->GetOutputChannels() : 0;
    if (n > METER_MAX_CH) n = METER_MAX_CH;
    return n;
}

/*****************************************************************************/
/* OnPaint : 背景 + 电平表条带（状态栏上方）                                   */
/*****************************************************************************/
void CMainFrame::OnPaint()
{
    CPaintDC dc(this);
    CRect rcPaint = dc.m_ps.rcPaint;
    dc.FillSolidRect(rcPaint, ::GetSysColor(COLOR_BTNFACE));
    if (m_bShowMeters)
        DrawMeters(dc);
}

/*****************************************************************************/
/* MeterInWidth / MeterOutWidth : 左右电平表带宽（0 = 该侧无通道）             */
/*****************************************************************************/
int CMainFrame::MeterInWidth() const
{
    /* 乐器（VSTi）不显示输入电平表 */
    if (m_pHost && m_pHost->IsLoaded() && m_pHost->Get()->IsInstrument())
        return 0;
    int n = m_pHost && m_pHost->IsLoaded() ? m_pHost->Get()->GetInputChannels() : 0;
    if (n > METER_MAX_CH) n = METER_MAX_CH;
    return n > 0 ? n * METER_CH_W : 0;   /* 纯通道条（无左侧标签列） */
}

int CMainFrame::MeterOutWidth() const
{
    int n = m_pHost && m_pHost->IsLoaded() ? m_pHost->Get()->GetOutputChannels() : 0;
    if (n > METER_MAX_CH) n = METER_MAX_CH;
    return n > 0 ? n * METER_CH_W : 0;
}

/*****************************************************************************/
/* DrawMeterBank : 绘制一组垂直电平条                                        */
/*   rc 整条带区域；label 侧标签；lvl 当前峰值；hold 峰值保持值               */
/*   布局：顶部标签 / 条带区（通道间明显分割线 + 峰值保持线）/ 底部数值框     */
/*****************************************************************************/
static void DrawMeterBank(CDC &dc, const CRect &rc, LPCTSTR label,
                          COLORREF labelCol, const volatile float *lvl,
                          const volatile float *hold, int nCh,
                          CFont *pFont, CFont *pFontVal,
                          bool showPeakLine, bool showValueBox)
{
    CFont *pOld = NULL;
    if (pFont)
        pOld = dc.SelectObject(pFont);

    dc.FillSolidRect(rc, RGB(20, 20, 20));
    CRect r = rc;                       /* DrawEdge 需要非 const LPRECT */
    dc.DrawEdge(&r, EDGE_ETCHED, BF_RECT);
    dc.SetBkMode(TRANSPARENT);
    /* 标题（IN 2 / OUT 2）在条带顶部水平居中 */
    dc.SetTextColor(labelCol);
    CString lbl;
    lbl.Format(_T("%s %d"), label, nCh);
    CSize ts = dc.GetTextExtent(lbl);
    int tx = rc.left + (rc.Width() - ts.cx) / 2;
    if (tx < rc.left + 2) tx = rc.left + 2;
    dc.TextOut(tx, rc.top + 3, lbl);

    /* 布局：顶部标签 / 条带区 / 底部数值框（可隐藏） */
    const int valH = 18;                /* 底部数值框高 */
    int barTop = rc.top + 20;
    int valTop = showValueBox ? (rc.bottom - 4 - valH) : rc.bottom;
    int baseY = valTop;
    int barH = baseY - barTop;
    if (barH < 4) barH = 4;
    int bw = METER_CH_W - 3;            /* 条宽（留分割线位置） */
    if (bw < 3) bw = 3;

    int x0 = rc.left;   /* 无左侧标签列，条带贴左缘 */

    /* 通道间明显分割线（每条右侧 1px 竖线） */
    int x = x0;
    for (int c = 0; c < nCh; c++)
    {
        int xl = x + METER_CH_W - 1;
        dc.FillSolidRect(CRect(xl, barTop, xl + 1, baseY), RGB(90, 90, 90));
        x += METER_CH_W;
    }

    /* 电平条 + 峰值保持线 + 底部数值框（dB 刻度：-60 ~ +12，0dB 以上红区） */
    const double dBMin = -60.0, dBMax = 12.0;
    const double span = dBMax - dBMin;
    const double redF = (0.0 - dBMin) / span;    /* 0dB 位置（红区起） */
    const double yelF = (-9.0 - dBMin) / span;   /* -9dB 位置 */
    x = x0;
    for (int c = 0; c < nCh; c++)
    {
        float l = lvl[c];
        if (l < 0.f) l = 0.f;
        double db = (l > 0.000001f) ? 20.0 * log10((double)l) : dBMin;
        if (db < dBMin) db = dBMin;
        if (db > dBMax) db = dBMax;
        int h = (int)(barH * (db - dBMin) / span);
        if (h > 0)
        {
            int top = baseY - h;
            int redY = baseY - (int)(barH * redF);
            int yelY = baseY - (int)(barH * yelF);
            /* 从条顶向下分段：红(0~+12) → 黄(-9~0) → 绿(-60~-9) */
            if (top < redY) dc.FillSolidRect(CRect(x, top, x + bw, redY), RGB(240, 60, 50));
            int yTop = (top > redY) ? top : redY;
            if (yTop < yelY) dc.FillSolidRect(CRect(x, yTop, x + bw, yelY), RGB(240, 200, 40));
            int gTop = (top > yelY) ? top : yelY;
            if (gTop < baseY) dc.FillSolidRect(CRect(x, gTop, x + bw, baseY), RGB(60, 200, 80));
        }
        /* 峰值保持线（亮白，停在最近峰值位置；可隐藏；dB 映射） */
        float hd = hold[c];
        if (showPeakLine && hd > 0.001f)
        {
            double dbh = (hd > 0.000001f) ? 20.0 * log10((double)hd) : dBMin;
            if (dbh < dBMin) dbh = dBMin;
            if (dbh > dBMax) dbh = dBMax;
            int hy = baseY - (int)(barH * (dbh - dBMin) / span);
            if (hy < barTop) hy = barTop;
            if (hy >= baseY) hy = baseY - 1;
            dc.FillSolidRect(CRect(x, hy, x + bw, hy + 2), RGB(255, 255, 255));
        }
        /* 底部数值框：当前电平 dB（小字体；可隐藏；无信号显示 -Inf） */
        if (showValueBox)
        {
            CRect vrc(x, valTop + 1, x + METER_CH_W, rc.bottom - 2);
            dc.FillSolidRect(vrc, RGB(12, 12, 12));
            CString vt;
            if (l <= 0.0001f)
                vt = _T("-Inf");
            else if (db <= -60.0)
                vt = _T("-60");
            else if (db >= 0.0)
                vt.Format(_T("+%.0f"), db);
            else
                vt.Format(_T("%.0f"), db);
            if (pFontVal)
                dc.SelectObject(pFontVal);
            dc.SetTextColor(RGB(170, 200, 170));
            dc.TextOut(vrc.left + 2, vrc.top + 2, vt);
            dc.SetTextColor(labelCol);
            if (pFontVal)
                dc.SelectObject(pFont);
        }
        x += METER_CH_W;
    }

    if (pOld)
        dc.SelectObject(pOld);
}

/*****************************************************************************/
/* DrawMeters : 绘制左右边缘垂直电平表（IN 左 / OUT 右，高度自适应窗口）       */
/*****************************************************************************/
void CMainFrame::DrawMeters(CDC &dc)
{
    if (!m_wndStatusBar.m_hWnd)
        return;
    CRect rcStatus, rcClient;
    m_wndStatusBar.GetWindowRect(&rcStatus);
    GetClientRect(&rcClient);
    rcClient.bottom -= rcStatus.Height();       /* 高度随窗口自适应 */

    int nIn = 0;
    int nOut = 0;
    if (m_pHost && m_pHost->IsLoaded())
    {
        /* 乐器（VSTi）不显示输入电平表 */
        if (!m_pHost->Get()->IsInstrument())
            nIn = m_pHost->Get()->GetInputChannels();
        nOut = m_pHost->Get()->GetOutputChannels();
    }
    if (nIn > METER_MAX_CH) nIn = METER_MAX_CH;
    if (nOut > METER_MAX_CH) nOut = METER_MAX_CH;

    /* 左侧输入 */
    if (nIn > 0)
    {
        CRect rc(rcClient.left, rcClient.top,
                 rcClient.left + nIn * METER_CH_W, rcClient.bottom);
        DrawMeterBank(dc, rc, _T("I"), RGB(180, 220, 180), m_inLevel, m_inHold,
                      nIn, &m_fontUI, &m_fontMeter, m_bPeakLine, m_bValueBox);
    }

    /* 右侧输出 */
    if (nOut > 0)
    {
        CRect rc(rcClient.right - nOut * METER_CH_W, rcClient.top,
                 rcClient.right, rcClient.bottom);
        DrawMeterBank(dc, rc, _T("O"), RGB(200, 180, 220), m_outLevel, m_outHold,
                      nOut, &m_fontUI, &m_fontMeter, m_bPeakLine, m_bValueBox);
    }
}

/*****************************************************************************/
/* 命令处理                                                                    */
/*****************************************************************************/
/*****************************************************************************/
/* OnDropFiles : 拖放插件加载                                                  */
/*****************************************************************************/
void CMainFrame::OnDropFiles(HDROP hDropInfo)
{
    if (!hDropInfo)
        return;
    wchar_t szFile[1024];
    if (DragQueryFileW(hDropInfo, 0, szFile, 1024) > 0)
        DoLoad(szFile);
    DragFinish(hDropInfo);
}

/*****************************************************************************/
/* OnSettingChange : 系统主题变化时重新应用 DWM 风格并重绘                     */
/*****************************************************************************/
void CMainFrame::OnSettingChange(UINT uFlags, LPCTSTR lpszSection)
{
    CFrameWnd::OnSettingChange(uFlags, lpszSection);

    /* 仅响应主题相关通知（深色/浅色切换） */
    if (lpszSection && _tcsicmp(lpszSection, _T("ImmersiveColorSet")) == 0)
    {
        ApplySystemStyle(m_hWnd);
        if (m_pPluginView)
            m_pPluginView->Invalidate();
    }
}

/*****************************************************************************/
/* OnInitMenuPopup : 菜单弹出前更新启用状态                                   */
/*   JACK 模式下禁用所有 ASIO 相关项（设备/控制面板/刷新/通道分配）           */
/*****************************************************************************/
void CMainFrame::OnInitMenuPopup(CMenu *pPopupMenu, UINT nIndex, BOOL bSysMenu)
{
    CFrameWnd::OnInitMenuPopup(pPopupMenu, nIndex, bSysMenu);
    if (bSysMenu || !pPopupMenu)
        return;

    BOOL bJack = (m_backendMode == 1);
    if (!bJack)
        return;

    UINT n = pPopupMenu->GetMenuItemCount();
    for (UINT i = 0; i < n; i++)
    {
        /* 父级子菜单项（ASIO 设备）通过子菜单句柄识别；命令项按 ID 识别 */
        CMenu *pSub = pPopupMenu->GetSubMenu(i);
        UINT id = pPopupMenu->GetMenuItemID(i);
        BOOL bAsio = (id == IDM_AUDIO_CPANEL || id == IDM_AUDIO_REFRESH ||
                      id == IDM_AUDIO_MAP ||
                      (pSub && (HMENU)*pSub == m_hAsioMenu));
        if (bAsio)
            pPopupMenu->EnableMenuItem(i, MF_BYPOSITION | MF_GRAYED);
    }
}

void CMainFrame::OnFileOpen()
{
    CFileDialog dlg(TRUE, L"dll", NULL,
                    OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                    L"VST2 插件 (*.dll)|*.dll|VST3 插件 (*.vst3)|*.vst3|所有文件 (*.*)|*.*||",
                    this);
    if (dlg.DoModal() != IDOK)
        return;
    DoLoad(dlg.GetPathName().GetString());
}

void CMainFrame::OnFileClose()
{
    if (!m_pHost)
        return;
    /* 关闭插件前保存调整后的预设（计划书 §5.9：关闭/退出时保存） */
    m_pHost->SaveStateFile();
    StopAudio();                    /* 先停音频：实时线程不再访问插件 */
    ClosePluginEditor();
    m_pHost->Close();
    RebuildInternalMenu();
    UpdateStatus();
    /* 关闭插件后恢复 exe 名标题 */
    SetWindowTextW(ComputeHostName().c_str());
}

void CMainFrame::OnFileExit()
{
    /* 菜单“退出”直接完全关闭（与关闭按钮的询问/托盘流程不同） */
    RealClose();
}

/*****************************************************************************/
/* OnFileSaveExe : 另存为 (Shell文件名)内部效果器.exe（Shell 快捷方式）       */
/*****************************************************************************/
void CMainFrame::OnFileSaveExe()
{
    if (!m_pHost || !m_pHost->IsLoaded())
        return;
    if (m_pHost->GetInternalCount() <= 1)
    {
        AfxMessageBox(_T("当前插件不是 Shell（多组件），无需另存快捷方式。"),
                      MB_OK | MB_ICONINFORMATION);
        return;
    }

    IPlugin *p = m_pHost->Get();
    const char *name = p ? p->GetName() : NULL;
    if (!name || !name[0])
    {
        AfxMessageBox(_T("无法获取当前内部效果器名称。"), MB_OK | MB_ICONERROR);
        return;
    }

    /* 当前 exe 路径与目录 */
    wchar_t szExe[1024];
    GetModuleFileNameW(NULL, szExe, 1024);
    std::wstring exePath = szExe;
    size_t slash = exePath.find_last_of(L"\\/");
    std::wstring dir = (slash == std::wstring::npos) ? L"" : exePath.substr(0, slash + 1);

    /* Shell 模块文件名主干 */
    std::wstring mod = m_pHost->GetModulePath();
    size_t ms = mod.find_last_of(L"\\/");
    if (ms != std::wstring::npos)
        mod = mod.substr(ms + 1);
    size_t md = mod.find_last_of(L'.');
    if (md != std::wstring::npos)
        mod = mod.substr(0, md);

    std::wstring target = dir + MakeShellExeName(mod, AnsiToWide(name));

    if (GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        CString msg;
        msg.Format(_T("目标文件已存在：\n%s\n\n是否覆盖？"), target.c_str());
        if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
            return;
    }
    if (!CopyFileW(exePath.c_str(), target.c_str(), FALSE))
    {
        AfxMessageBox(_T("保存失败。"), MB_OK | MB_ICONERROR);
        return;
    }

    CString msg2;
    msg2.Format(_T("已保存快捷方式：\n%s\n\n以后运行该 exe 将直接加载：%S"),
                target.c_str(), name);
    AfxMessageBox(msg2, MB_OK | MB_ICONINFORMATION);
}

void CMainFrame::OnAppAbout()
{
    AfxMessageBox(_T("Single VST Host 1.6（vsthost）\n单插件 VST2/VST3 宿主（ASIO / JACK2 音频后端）\n\n本仓库的改造与代码由 AI（GitHub Copilot）辅助编写，\n衍生自 Arakula/vsthost（尊重原开发者）。"),
                  MB_OK | MB_ICONINFORMATION);
}

/*****************************************************************************/
/* OnAppAboutPlugin : 关于插件（详细插件 + 音频信息）                         */
/*   状态栏只保留 DSP（左下角）与通道数（右下角），详细内容在此查看           */
/*****************************************************************************/
void CMainFrame::OnAppAboutPlugin()
{
    CString s;
    if (!m_pHost || !m_pHost->IsLoaded())
    {
        AfxMessageBox(_T("未加载插件。"), MB_OK | MB_ICONINFORMATION);
        return;
    }
    IPlugin *p = m_pHost->Get();

    s += _T("插件: ");
    const char *name = p->GetName();
    s += (name && name[0]) ? CString(name) : CString(_T("(未知)"));
    s += _T("\n模块: ");
    s += m_pHost->GetModulePath().c_str();
    s.AppendFormat(_T("\n通道: %d 进 / %d 出"),
                   p->GetInputChannels(), p->GetOutputChannels());
    s.AppendFormat(_T("\nMIDI: 输入 %s / 输出 %s"),
                   p->WantMidiInput() ? _T("支持") : _T("无"),
                   p->WantMidiOutput() ? _T("支持") : _T("无"));

    int nInt = m_pHost->GetInternalCount();
    if (nInt > 1)
    {
        PluginInternalInfo info;
        int cur = m_pHost->GetCurrentInternal();
        if (cur >= 0 && m_pHost->GetInternalInfo(cur, &info))
            s.AppendFormat(_T("\n内部效果器: %d 个（当前: %S）"), nInt, info.name);
        else
            s.AppendFormat(_T("\n内部效果器: %d 个"), nInt);
    }

    /* 音频 */
    if (m_pJack && m_pJack->IsOpen())
    {
        /* JACK：客户端名 / 采样率 / 块大小均取自 JACK 服务器 */
        s.AppendFormat(_T("\n\nJACK: %S"), m_pJack->GetDriverName());
        if (m_pJack->IsRunning())
            s.AppendFormat(_T("   %.0f Hz / %d 帧（服务器）   DSP %.0f%%"),
                           m_pJack->GetSampleRate(), m_pJack->GetBufferSize(),
                           m_pJack->GetDspUsage());
        else
            s += _T("（已停止）");
    }
    else if (m_pAsio && m_pAsio->IsOpen())
    {
        s.AppendFormat(_T("\n\nASIO: %S"), m_pAsio->GetDriverName());
        if (m_pAsio->IsRunning())
            s.AppendFormat(_T("   %.0f Hz / %d 帧   DSP %.0f%%"),
                           m_pAsio->GetSampleRate(), m_pAsio->GetBufferSize(),
                           m_pAsio->GetDspUsage());
        else
            s += _T("（已停止）");
    }
    else
        s += _T("\n\n音频: 未启动");
    s += m_bSineTest ? _T("\n测试信号: 开启（1kHz 正弦）")
                     : _T("\n测试信号: 关闭");

    AfxMessageBox(s, MB_OK | MB_ICONINFORMATION);
}

/*****************************************************************************/
/* OnPluginTestProc : 处理 1 块音频，验证插件音频通路                         */
/*****************************************************************************/
void CMainFrame::OnPluginTestProc()
{
    if (!m_pHost || !m_pHost->IsLoaded())
    {
        AfxMessageBox(_T("未加载插件。"), MB_OK | MB_ICONINFORMATION);
        return;
    }
    IPlugin *p = m_pHost->Get();
    int inCh = p->GetInputChannels();
    int outCh = p->GetOutputChannels();
    const int frames = 512;
    static float inBuf[16][512];
    static float outBuf[16][512];
    float *in[16], *out[16];
    for (int c = 0; c < inCh; c++) { in[c] = inBuf[c]; memset(inBuf[c], 0, sizeof(inBuf[c])); }
    for (int c = 0; c < outCh; c++) { out[c] = outBuf[c]; memset(outBuf[c], 0, sizeof(outBuf[c])); }
    /* 1kHz 正弦测试信号（仅第一输入通道） */
    if (inCh > 0)
        for (int i = 0; i < frames; i++)
            in[0][i] = (float)(0.5 * sin(2.0 * 3.14159265358979 * 1000.0 * i / 44100.0));
    p->Process(in, out, frames, inCh, outCh, NULL);
    /* 检查输出是否有能量 */
    float energy = 0.f;
    for (int c = 0; c < outCh; c++)
        for (int i = 0; i < frames; i++)
            energy += out[c][i] * out[c][i];
    CString msg;
    msg.Format(_T("处理完成：%d 进 / %d 出，输出能量 = %.3f（非零说明音频通路正常）"),
               inCh, outCh, energy);
    AfxMessageBox(msg, MB_OK | MB_ICONINFORMATION);
}

/*****************************************************************************/
/* OnPluginSineTest : 测试信号开关（1kHz 正弦注入插件输入，经 ASIO 出声）     */
/*****************************************************************************/
void CMainFrame::OnPluginSineTest()
{
    if (!m_pHost || !m_pHost->IsLoaded())
    {
        AfxMessageBox(_T("请先加载插件并启动音频。"), MB_OK | MB_ICONINFORMATION);
        return;
    }
    m_bSineTest = !m_bSineTest;
    m_sinePhase = 0.0;
    if (GetMenu())
        GetMenu()->CheckMenuItem(IDM_PLUGIN_SINETEST,
                                 MF_BYCOMMAND | (m_bSineTest ? MF_CHECKED : MF_UNCHECKED));
    UpdateAsioStatus();
}

/*****************************************************************************/
/* OnPluginMidiMap : 打开 MIDI 参数映射设置窗口                              */
/*****************************************************************************/
void CMainFrame::OnPluginMidiMap()
{
    std::vector<std::string> pnames;
    if (m_pHost && m_pHost->IsLoaded())
    {
        IPlugin *p = m_pHost->Get();
        int n = p->GetNumParams();
        for (int i = 0; i < n; i++)
        {
            char buf[256] = "";
            p->GetParamName(i, buf, 256);
            pnames.push_back(buf);
        }
    }
    CMidiMapDialog dlg(this, pnames);
    dlg.DoModal();
}

/*****************************************************************************/
/* MIDI 映射：加载/保存/读写（[MidiMap] mapN=ch,cc,param）                    */
/*****************************************************************************/
void CMainFrame::LoadMidiMap()
{
    EnterCriticalSection(&m_midiMapCs);
    m_midiMap.clear();
    int n = GetPrivateProfileIntW(L"MidiMap", L"count", 0, AsioConfigPath().c_str());
    if (n > 64) n = 64;
    for (int i = 0; i < n; i++)
    {
        wchar_t key[16], val[64];
        swprintf(key, 16, L"map%d", i);
        GetPrivateProfileStringW(L"MidiMap", key, L"", val, 64,
                                 AsioConfigPath().c_str());
        MidiMapEntry e = { 0, 0, -1 };
        if (swscanf_s(val, L"%d,%d,%d", &e.ch, &e.cc, &e.param) == 3)
            m_midiMap.push_back(e);
    }
    LeaveCriticalSection(&m_midiMapCs);
}

void CMainFrame::SaveMidiMap() const
{
    wchar_t b[16];
    swprintf(b, 16, L"%d", (int)m_midiMap.size());
    WritePrivateProfileStringW(L"MidiMap", L"count", b, AsioConfigPath().c_str());
    for (size_t i = 0; i < m_midiMap.size(); i++)
    {
        wchar_t key[16], val[64];
        swprintf(key, 16, L"map%zu", i);
        swprintf(val, 64, L"%d,%d,%d", m_midiMap[i].ch, m_midiMap[i].cc,
                 m_midiMap[i].param);
        WritePrivateProfileStringW(L"MidiMap", key, val, AsioConfigPath().c_str());
    }
}

void CMainFrame::GetMidiMap(std::vector<MidiMapEntry> &out) const
{
    EnterCriticalSection(&m_midiMapCs);
    out = m_midiMap;
    LeaveCriticalSection(&m_midiMapCs);
}

void CMainFrame::SetMidiMap(const std::vector<MidiMapEntry> &map)
{
    EnterCriticalSection(&m_midiMapCs);
    m_midiMap = map;
    LeaveCriticalSection(&m_midiMapCs);
    SaveMidiMap();
}

void CMainFrame::OnInternalSelect(UINT nID)
{
    if (!m_pHost)
        return;
    int idx = (int)(nID - IDM_INTERNAL_BASE);
    StopAudio();                    /* 切换内部效果器会重实例化，先停音频 */
    ClosePluginEditor();
    if (!m_pHost->SwitchInternal(idx))
    {
        AfxMessageBox(_T("内部效果器切换失败。"), MB_OK | MB_ICONERROR);
        OpenPluginEditor();
        StartAudio();
        return;
    }
    OpenPluginEditor();     /* 切换后重建编辑器与菜单勾选 */
    StartAudio();           /* 用新通道数重开音频 */
}
