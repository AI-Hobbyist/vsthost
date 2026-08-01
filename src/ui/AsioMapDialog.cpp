// AsioMapDialog.cpp : ASIO 通道分配对话框实现
/******************************************************************************/
#include "AsioMapDialog.h"

#include <vector>

/*===========================================================================*/
/* 动态对话框模板构建（DLGTEMPLATE 字节流，Win32 标准布局）                    */
/*===========================================================================*/
namespace {

inline void Align4(std::vector<BYTE> &v) { while (v.size() & 3) v.push_back(0); }
inline void W(std::vector<BYTE> &v, WORD w) { v.push_back((BYTE)(w & 0xff)); v.push_back((BYTE)((w >> 8) & 0xff)); }
inline void DW(std::vector<BYTE> &v, DWORD d) { W(v, (WORD)(d & 0xffff)); W(v, (WORD)((d >> 16) & 0xffff)); }

/* 写入字符串并 4 字节对齐 */
void SZ(std::vector<BYTE> &v, LPCWSTR s)
{
    while (s && *s) { W(v, (WORD)*s); s++; }
    W(v, 0);
    Align4(v);
}

/* 对话框头（无菜单/无窗口类，DS_SETFONT 8pt MS Shell Dlg） */
void BeginDlg(std::vector<BYTE> &v, DWORD style, DWORD exStyle,
              WORD ctrlCount, short cx, short cy, LPCWSTR title)
{
    DW(v, style); DW(v, exStyle); W(v, ctrlCount);
    W(v, 0); W(v, 0); W(v, (WORD)cx); W(v, (WORD)cy);
    SZ(v, L"");            // 菜单
    SZ(v, L"");            // 窗口类
    SZ(v, title);          // 标题
    W(v, 8);               // 字体点数
    SZ(v, L"MS Shell Dlg");
}

} // namespace

/*===========================================================================*/
/* CAsioMapDialog                                                             */
/*===========================================================================*/
BEGIN_MESSAGE_MAP(CAsioMapDialog, CDialog)
    ON_COMMAND(IDM_ASIO_RESET, &CAsioMapDialog::OnReset)
END_MESSAGE_MAP()

CAsioMapDialog::CAsioMapDialog(const std::vector<std::string> &asioIn,
                               const std::vector<std::string> &asioOut,
                               int plugIn, int plugOut,
                               std::vector<int> &mapIn, std::vector<int> &mapOut)
    : m_asioIn(asioIn), m_asioOut(asioOut),
      m_plugIn(plugIn), m_plugOut(plugOut),
      m_mapIn(mapIn), m_mapOut(mapOut)
{
    /* 高度（DLU）：标题 + 输入行 + 输出行 + 按钮 */
    int dlgH = 8;
    dlgH += 16;                     /* 输入区标题 */
    dlgH += m_plugIn * 16;
    dlgH += 12;                     /* 分隔 */
    dlgH += 16;                     /* 输出区标题 */
    dlgH += m_plugOut * 16;
    dlgH += 12 + 16;                /* 按钮 + 边距 */

    /* 仅对话框头部（cdit=0），控件在 OnInitDialog 用代码创建 */
    BeginDlg(m_tmpl, DS_SETFONT | DS_MODALFRAME | DS_CENTER |
                     WS_POPUP | WS_CAPTION | WS_SYSMENU,
             0, 0, 360, (WORD)dlgH, L"ASIO 通道分配");
    InitModalIndirect((LPCDLGTEMPLATE)m_tmpl.data());
}

/*****************************************************************************/
/* 代码创建控件（STATIC / COMBOBOX / BUTTON）                                */
/*****************************************************************************/
void CAsioMapDialog::AddStatic(int id, LPCWSTR text, int x, int y, int w, int h)
{
    ::CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                    x, y, w, h, m_hWnd, (HMENU)(INT_PTR)id, NULL, NULL);
}

void CAsioMapDialog::AddCombo(int id, int x, int y, int w, int h)
{
    ::CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST |
                    WS_VSCROLL | WS_TABSTOP, x, y, w, h, m_hWnd,
                    (HMENU)(INT_PTR)id, NULL, NULL);
}

void CAsioMapDialog::AddButton(int id, LPCWSTR text, int x, int y,
                               int w, int h, DWORD extra)
{
    ::CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON |
                    WS_TABSTOP | extra, x, y, w, h, m_hWnd,
                    (HMENU)(INT_PTR)id, NULL, NULL);
}

/*****************************************************************************/
/* OnInitDialog : 创建控件 + 填充各 ComboBox（第一个 = 静音）                 */
/*****************************************************************************/
BOOL CAsioMapDialog::OnInitDialog()
{
    CDialog::OnInitDialog();

    /* DLU -> 像素（对话框单位） */
    CRect rcDlu(0, 0, 8, 16);
    MapDialogRect(&rcDlu);
    int pxX = rcDlu.Width() / 8;
    int pxY = rcDlu.Height() / 16;
    if (pxX < 1) pxX = 1;
    if (pxY < 1) pxY = 1;

    int lx = 8 * pxX, lw = 68 * pxX;
    int cx0 = 80 * pxX, cw = 272 * pxX;
    int y = 8 * pxY;

    /* 输入区标题 + 行 */
    AddStatic(0xF001, L"插件输入  ->  ASIO 输入", 8 * pxX, y, 200 * pxX, 12 * pxY);
    y += 16 * pxY;
    for (int i = 0; i < m_plugIn; i++)
    {
        CString lbl;
        lbl.Format(_T("In %d"), i + 1);
        AddStatic(0xF100 + i, lbl, lx, y, lw, 12 * pxY);
        AddCombo(100 + i, cx0, y, cw, 110 * pxY);
        y += 16 * pxY;
    }

    /* 输出区标题 + 行 */
    y += 4 * pxY;
    AddStatic(0xF002, L"插件输出  ->  ASIO 输出", 8 * pxX, y, 200 * pxX, 12 * pxY);
    y += 16 * pxY;
    for (int i = 0; i < m_plugOut; i++)
    {
        CString lbl;
        lbl.Format(_T("Out %d"), i + 1);
        AddStatic(0xF200 + i, lbl, lx, y, lw, 12 * pxY);
        AddCombo(200 + i, cx0, y, cw, 110 * pxY);
        y += 16 * pxY;
    }

    /* 按钮 */
    y += 8 * pxY;
    AddButton(IDM_ASIO_RESET, L"重置", 130 * pxX, y, 46 * pxX, 14 * pxY, 0);
    AddButton(IDOK, L"确定", 184 * pxX, y, 46 * pxX, 14 * pxY, BS_DEFPUSHBUTTON);
    AddButton(IDCANCEL, L"取消", 238 * pxX, y, 46 * pxX, 14 * pxY, 0);

    /* 系统 UI 字体（Segoe UI）应用到对话框与所有子控件：
       必须在控件创建之后设置（EnumChildWindows 才能枚举到全部） */
    NONCLIENTMETRICSW ncm;
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
    {
        if (m_fontDlg.CreateFontIndirectW(&ncm.lfMessageFont))
        {
            SetFont(&m_fontDlg, TRUE);
            EnumChildWindows(m_hWnd, SetChildFont, (LPARAM)(HFONT)m_fontDlg);
        }
    }

    FillCombos();
    ApplyMap();
    return TRUE;
}

/*****************************************************************************/
/* SetChildFont : EnumChildWindows 回调，给子控件设置系统 UI 字体             */
/*****************************************************************************/
BOOL CALLBACK CAsioMapDialog::SetChildFont(HWND hWnd, LPARAM lParam)
{
    ::SendMessageW(hWnd, WM_SETFONT, lParam, TRUE);
    return TRUE;
}

void CAsioMapDialog::FillCombos()
{
    for (int i = 0; i < m_plugIn; i++)
    {
        CComboBox *cb = (CComboBox *)GetDlgItem(100 + i);
        if (!cb) continue;
        cb->ResetContent();
        cb->AddString(_T("（静音）"));
        for (size_t j = 0; j < m_asioIn.size(); j++)
            cb->AddString(CString(m_asioIn[j].c_str()));
    }
    for (int i = 0; i < m_plugOut; i++)
    {
        CComboBox *cb = (CComboBox *)GetDlgItem(200 + i);
        if (!cb) continue;
        cb->ResetContent();
        cb->AddString(_T("（静音）"));
        for (size_t j = 0; j < m_asioOut.size(); j++)
            cb->AddString(CString(m_asioOut[j].c_str()));
    }
}

void CAsioMapDialog::ApplyMap()
{
    for (int i = 0; i < m_plugIn; i++)
    {
        CComboBox *cb = (CComboBox *)GetDlgItem(100 + i);
        if (!cb) continue;
        int v = (i < (int)m_mapIn.size()) ? m_mapIn[i] : -1;
        cb->SetCurSel(v + 1);       /* -1 -> 0（静音） */
    }
    for (int i = 0; i < m_plugOut; i++)
    {
        CComboBox *cb = (CComboBox *)GetDlgItem(200 + i);
        if (!cb) continue;
        int v = (i < (int)m_mapOut.size()) ? m_mapOut[i] : -1;
        cb->SetCurSel(v + 1);
    }
}

void CAsioMapDialog::ReadBack()
{
    for (int i = 0; i < m_plugIn; i++)
    {
        CComboBox *cb = (CComboBox *)GetDlgItem(100 + i);
        if (!cb) continue;
        int sel = cb->GetCurSel() - 1;      /* 0 = 静音 */
        if (i < (int)m_mapIn.size())
            m_mapIn[i] = (sel >= (int)m_asioIn.size()) ? -1 : sel;
    }
    for (int i = 0; i < m_plugOut; i++)
    {
        CComboBox *cb = (CComboBox *)GetDlgItem(200 + i);
        if (!cb) continue;
        int sel = cb->GetCurSel() - 1;
        if (i < (int)m_mapOut.size())
            m_mapOut[i] = (sel >= (int)m_asioOut.size()) ? -1 : sel;
    }
}

void CAsioMapDialog::OnReset()
{
    /* 恢复默认：插件通道 i -> ASIO 通道 i（越界则静音） */
    for (int i = 0; i < m_plugIn && i < (int)m_mapIn.size(); i++)
        m_mapIn[i] = (i < (int)m_asioIn.size()) ? i : -1;
    for (int i = 0; i < m_plugOut && i < (int)m_mapOut.size(); i++)
        m_mapOut[i] = (i < (int)m_asioOut.size()) ? i : -1;
    ApplyMap();
}

void CAsioMapDialog::OnOK()
{
    ReadBack();
    CDialog::OnOK();
}
