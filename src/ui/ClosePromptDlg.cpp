// ClosePromptDlg.cpp : 关闭时询问对话框实现
/******************************************************************************/
#include "ClosePromptDlg.h"
#include "../app/MainWnd.h"
#include "../host/HostNaming.h"

/*===========================================================================*/
/* 动态对话框模板（DLGTEMPLATE 字节流）                                       */
/*===========================================================================*/
namespace {
inline void Align4(std::vector<BYTE> &v) { while (v.size() & 3) v.push_back(0); }
inline void W(std::vector<BYTE> &v, WORD w) { v.push_back((BYTE)(w & 0xff)); v.push_back((BYTE)((w >> 8) & 0xff)); }
inline void DW(std::vector<BYTE> &v, DWORD d) { W(v, (WORD)(d & 0xffff)); W(v, (WORD)((d >> 16) & 0xffff)); }
void SZ(std::vector<BYTE> &v, LPCWSTR s)
{
    while (s && *s) { W(v, (WORD)*s); s++; }
    W(v, 0);
    Align4(v);
}
void BeginDlg(std::vector<BYTE> &v, DWORD style, DWORD exStyle,
              WORD ctrlCount, short cx, short cy, LPCWSTR title)
{
    DW(v, style); DW(v, exStyle); W(v, ctrlCount);
    W(v, 0); W(v, 0); W(v, (WORD)cx); W(v, (WORD)cy);
    SZ(v, L""); SZ(v, L""); SZ(v, title);
    W(v, 8); SZ(v, L"MS Shell Dlg");
}
} // namespace

BEGIN_MESSAGE_MAP(CClosePromptDlg, CDialog)
    ON_BN_CLICKED(IDC_CP_TRAY, &CClosePromptDlg::OnTray)
    ON_BN_CLICKED(IDC_CP_EXIT, &CClosePromptDlg::OnExit)
END_MESSAGE_MAP()

CClosePromptDlg::CClosePromptDlg(CMainFrame *owner)
    : m_pOwner(owner), m_result(0), m_dontAsk(false)
{
    BeginDlg(m_tmpl, DS_SETFONT | DS_MODALFRAME | DS_CENTER |
                     WS_POPUP | WS_CAPTION | WS_SYSMENU,
             0, 0, 260, 100, L"关闭 vsthost");
    InitModalIndirect((LPCDLGTEMPLATE)m_tmpl.data());
}

BOOL CALLBACK CClosePromptDlg::SetChildFont(HWND hWnd, LPARAM lParam)
{
    ::SendMessageW(hWnd, WM_SETFONT, lParam, TRUE);
    return TRUE;
}

BOOL CClosePromptDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    CRect rcDlu(0, 0, 8, 16);
    MapDialogRect(&rcDlu);
    int pxX = rcDlu.Width() / 8;   if (pxX < 1) pxX = 1;
    int pxY = rcDlu.Height() / 16; if (pxY < 1) pxY = 1;

    /* 提示文本 */
    ::CreateWindowW(L"STATIC",
                    L"关闭 vsthost，要执行哪个操作？",
                    WS_CHILD | WS_VISIBLE | SS_LEFT,
                    12 * pxX, 12 * pxY, 236 * pxX, 16 * pxY,
                    m_hWnd, NULL, NULL, NULL);

    /* 最小化到托盘（默认按钮） */
    ::CreateWindowW(L"BUTTON", L"最小化到托盘(&T)",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_DEFPUSHBUTTON,
                    40 * pxX, 40 * pxY, 92 * pxX, 16 * pxY,
                    m_hWnd, (HMENU)(INT_PTR)IDC_CP_TRAY, NULL, NULL);

    /* 完全关闭 */
    ::CreateWindowW(L"BUTTON", L"完全关闭(&X)",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    142 * pxX, 40 * pxY, 80 * pxX, 16 * pxY,
                    m_hWnd, (HMENU)(INT_PTR)IDC_CP_EXIT, NULL, NULL);

    /* 不再提示 */
    ::CreateWindowW(L"BUTTON", L"不再提示（记住本次选择）",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                    40 * pxX, 68 * pxY, 180 * pxX, 14 * pxY,
                    m_hWnd, (HMENU)(INT_PTR)IDC_CP_DONTASK, NULL, NULL);

    /* 系统 UI 字体 */
    NONCLIENTMETRICSW ncm;
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        m_fontDlg.CreateFontIndirectW(&ncm.lfMessageFont);
    if ((HFONT)m_fontDlg)
    {
        SetFont(&m_fontDlg);
        EnumChildWindows(m_hWnd, SetChildFont, (LPARAM)(HFONT)m_fontDlg);
    }

    /* 窗口标题取当前宿主名 */
    if (m_pOwner)
        SetWindowTextW((std::wstring(L"关闭 ") + ComputeHostName()).c_str());

    return TRUE;
}

void CClosePromptDlg::OnTray()
{
    m_result = 1;
    m_dontAsk = ((CButton *)GetDlgItem(IDC_CP_DONTASK))->GetCheck() == BST_CHECKED;
    EndDialog(IDOK);
}

void CClosePromptDlg::OnExit()
{
    m_result = 2;
    m_dontAsk = ((CButton *)GetDlgItem(IDC_CP_DONTASK))->GetCheck() == BST_CHECKED;
    EndDialog(IDOK);
}
