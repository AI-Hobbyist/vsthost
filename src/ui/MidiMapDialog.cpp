// MidiMapDialog.cpp : MIDI 参数映射设置实现
/******************************************************************************/
#include "MidiMapDialog.h"
#include "../app/MainWnd.h"   // MidiMapEntry / GetMidiMap / SetMidiMap

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

BEGIN_MESSAGE_MAP(CMidiMapDialog, CDialog)
    ON_BN_CLICKED(IDC_MM_ADD, &CMidiMapDialog::OnAdd)
    ON_BN_CLICKED(IDC_MM_DEL, &CMidiMapDialog::OnDel)
END_MESSAGE_MAP()

CMidiMapDialog::CMidiMapDialog(CMainFrame *owner,
                               const std::vector<std::string> &paramNames)
    : m_pOwner(owner), m_paramNames(paramNames)
{
    BeginDlg(m_tmpl, DS_SETFONT | DS_MODALFRAME | DS_CENTER |
                     WS_POPUP | WS_CAPTION | WS_SYSMENU,
             0, 0, 300, 150, L"MIDI 参数映射");
    InitModalIndirect((LPCDLGTEMPLATE)m_tmpl.data());
}

BOOL CALLBACK CMidiMapDialog::SetChildFont(HWND hWnd, LPARAM lParam)
{
    ::SendMessageW(hWnd, WM_SETFONT, lParam, TRUE);
    return TRUE;
}

void CMidiMapDialog::AddStatic(int id, LPCWSTR text, int x, int y, int w, int h)
{
    ::CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                    x, y, w, h, m_hWnd, (HMENU)(INT_PTR)id, NULL, NULL);
}
void CMidiMapDialog::AddCombo(int id, int x, int y, int w, int h)
{
    ::CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST |
                    WS_VSCROLL | WS_TABSTOP, x, y, w, h, m_hWnd,
                    (HMENU)(INT_PTR)id, NULL, NULL);
}
void CMidiMapDialog::AddList(int id, int x, int y, int w, int h)
{
    ::CreateWindowW(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                    WS_TABSTOP | LBS_NOINTEGRALHEIGHT, x, y, w, h, m_hWnd,
                    (HMENU)(INT_PTR)id, NULL, NULL);
}
void CMidiMapDialog::AddButton(int id, LPCWSTR text, int x, int y, int w, int h,
                               DWORD extra)
{
    ::CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON |
                    WS_TABSTOP | extra, x, y, w, h, m_hWnd,
                    (HMENU)(INT_PTR)id, NULL, NULL);
}

BOOL CMidiMapDialog::OnInitDialog()
{
    CDialog::OnInitDialog();

    CRect rcDlu(0, 0, 8, 16);
    MapDialogRect(&rcDlu);
    int pxX = rcDlu.Width() / 8;   if (pxX < 1) pxX = 1;
    int pxY = rcDlu.Height() / 16; if (pxY < 1) pxY = 1;

    /* 映射列表 */
    AddList(IDC_MM_LIST, 12 * pxX, 8 * pxY, 276 * pxX, 80 * pxY);
    /* 编辑区 */
    int ey = 96 * pxY;
    AddStatic(0xF710, L"通道", 12 * pxX, ey, 34 * pxX, 12 * pxY);
    AddCombo(IDC_MM_CH, 48 * pxX, ey, 44 * pxX, 90 * pxY);
    AddStatic(0xF711, L"CC", 100 * pxX, ey, 24 * pxX, 12 * pxY);
    AddCombo(IDC_MM_CC, 128 * pxX, ey, 44 * pxX, 90 * pxY);
    AddStatic(0xF712, L"参数", 180 * pxX, ey, 32 * pxX, 12 * pxY);
    AddCombo(IDC_MM_PARAM, 214 * pxX, ey, 74 * pxX, 100 * pxY);
    /* 按钮 */
    int by = 122 * pxY;
    AddButton(IDC_MM_ADD, L"添加", 12 * pxX, by, 56 * pxX, 16 * pxY, 0);
    AddButton(IDC_MM_DEL, L"删除", 76 * pxX, by, 56 * pxX, 16 * pxY, 0);
    AddButton(IDC_MM_OK, L"确定", 140 * pxX, by, 52 * pxX, 16 * pxY, BS_DEFPUSHBUTTON);
    AddButton(IDC_MM_CANCEL, L"取消", 200 * pxX, by, 52 * pxX, 16 * pxY, 0);

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

    /* 通道 1~16 */
    CComboBox *p = (CComboBox *)GetDlgItem(IDC_MM_CH);
    if (p)
    {
        for (int i = 1; i <= 16; i++)
        {
            CString s;
            s.Format(_T("%d"), i);
            p->AddString(s);
        }
        p->SetCurSel(0);
    }
    /* CC 0~127 */
    p = (CComboBox *)GetDlgItem(IDC_MM_CC);
    if (p)
    {
        for (int i = 0; i <= 127; i++)
        {
            CString s;
            s.Format(_T("%d"), i);
            p->AddString(s);
        }
        p->SetCurSel(1);
    }
    /* 参数列表 */
    p = (CComboBox *)GetDlgItem(IDC_MM_PARAM);
    if (p)
    {
        for (size_t i = 0; i < m_paramNames.size(); i++)
        {
            CString s(m_paramNames[i].c_str());
            if (s.IsEmpty())
                s.Format(_T("参数 %d"), (int)i);
            p->AddString(s);
        }
        if (m_paramNames.empty())
            p->AddString(_T("（无参数）"));
        p->SetCurSel(0);
    }

    /* 载入现有映射 */
    if (m_pOwner)
    {
        std::vector<MidiMapEntry> cur;
        m_pOwner->GetMidiMap(cur);
        for (size_t i = 0; i < cur.size(); i++)
        {
            Entry e;
            e.ch = cur[i].ch;
            e.cc = cur[i].cc;
            e.param = cur[i].param;
            m_entries.push_back(e);
        }
    }
    RefreshList();
    return TRUE;
}

void CMidiMapDialog::RefreshList()
{
    CListBox *lb = (CListBox *)GetDlgItem(IDC_MM_LIST);
    if (!lb)
        return;
    lb->ResetContent();
    for (size_t i = 0; i < m_entries.size(); i++)
    {
        CString s;
        const char *pname = (m_entries[i].param >= 0 &&
                             m_entries[i].param < (int)m_paramNames.size())
                                ? m_paramNames[m_entries[i].param].c_str() : "";
        if (pname && *pname)
            s.Format(_T("Ch %d  CC %d  ->  %S"), m_entries[i].ch, m_entries[i].cc,
                     pname);
        else
            s.Format(_T("Ch %d  CC %d  ->  #%d"), m_entries[i].ch, m_entries[i].cc,
                     m_entries[i].param);
        lb->AddString(s);
    }
}

void CMidiMapDialog::OnAdd()
{
    CComboBox *ch = (CComboBox *)GetDlgItem(IDC_MM_CH);
    CComboBox *cc = (CComboBox *)GetDlgItem(IDC_MM_CC);
    CComboBox *pm = (CComboBox *)GetDlgItem(IDC_MM_PARAM);
    if (!ch || !cc || !pm)
        return;
    Entry e;
    e.ch = ch->GetCurSel() + 1;
    e.cc = cc->GetCurSel();
    e.param = pm->GetCurSel();
    if (m_paramNames.empty())
        return;
    /* 避免重复 */
    for (size_t i = 0; i < m_entries.size(); i++)
        if (m_entries[i].ch == e.ch && m_entries[i].cc == e.cc)
        {
            m_entries[i].param = e.param;
            RefreshList();
            return;
        }
    m_entries.push_back(e);
    RefreshList();
}

void CMidiMapDialog::OnDel()
{
    CListBox *lb = (CListBox *)GetDlgItem(IDC_MM_LIST);
    if (!lb)
        return;
    int sel = lb->GetCurSel();
    if (sel < 0 || sel >= (int)m_entries.size())
        return;
    m_entries.erase(m_entries.begin() + sel);
    RefreshList();
}

void CMidiMapDialog::OnOK()
{
    if (m_pOwner)
    {
        std::vector<MidiMapEntry> out;
        for (size_t i = 0; i < m_entries.size(); i++)
        {
            MidiMapEntry e;
            e.ch = m_entries[i].ch;
            e.cc = m_entries[i].cc;
            e.param = m_entries[i].param;
            out.push_back(e);
        }
        m_pOwner->SetMidiMap(out);
    }
    CDialog::OnOK();
}

void CMidiMapDialog::OnCancel()
{
    CDialog::OnCancel();
}
