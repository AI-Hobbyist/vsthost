// GlobalSettingsDlg.cpp : 全局设置窗口实现（关闭行为 + 音频/MIDI 设置）
/******************************************************************************/
#include "GlobalSettingsDlg.h"
#include "../app/MainWnd.h"
#include "../host/MidiInput.h"
#include "../host/MidiOutput.h"

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

BEGIN_MESSAGE_MAP(CGlobalSettingsDlg, CDialog)
END_MESSAGE_MAP()

CGlobalSettingsDlg::CGlobalSettingsDlg(CMainFrame *owner)
    : m_pOwner(owner)
{
    BeginDlg(m_tmpl, DS_SETFONT | DS_MODALFRAME | DS_CENTER |
                     WS_POPUP | WS_CAPTION | WS_SYSMENU,
             0, 0, 250, 314, L"全局设置");
    InitModalIndirect((LPCDLGTEMPLATE)m_tmpl.data());
}

BOOL CALLBACK CGlobalSettingsDlg::SetChildFont(HWND hWnd, LPARAM lParam)
{
    ::SendMessageW(hWnd, WM_SETFONT, lParam, TRUE);
    return TRUE;
}

void CGlobalSettingsDlg::AddStatic(int id, LPCWSTR text, int x, int y, int w, int h)
{
    ::CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                    x, y, w, h, m_hWnd, (HMENU)(INT_PTR)id, NULL, NULL);
}
void CGlobalSettingsDlg::AddCombo(int id, int x, int y, int w, int h, bool editable)
{
    DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP |
                  (editable ? CBS_DROPDOWN : CBS_DROPDOWNLIST);
    ::CreateWindowW(L"COMBOBOX", L"", style, x, y, w, h, m_hWnd,
                    (HMENU)(INT_PTR)id, NULL, NULL);
}
void CGlobalSettingsDlg::AddCheck(int id, LPCWSTR text, int x, int y, int w, int h)
{
    ::CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX |
                    WS_TABSTOP, x, y, w, h, m_hWnd, (HMENU)(INT_PTR)id, NULL, NULL);
}
void CGlobalSettingsDlg::AddRadio(int id, LPCWSTR text, int x, int y, int w, int h,
                                  bool first)
{
    DWORD style = WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_TABSTOP |
                  (first ? WS_GROUP : 0);
    ::CreateWindowW(L"BUTTON", text, style, x, y, w, h, m_hWnd,
                    (HMENU)(INT_PTR)id, NULL, NULL);
}
void CGlobalSettingsDlg::AddButton(int id, LPCWSTR text, int x, int y, int w, int h,
                                   DWORD extra)
{
    ::CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON |
                    WS_TABSTOP | extra, x, y, w, h, m_hWnd,
                    (HMENU)(INT_PTR)id, NULL, NULL);
}

BOOL CGlobalSettingsDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    CRect rcDlu(0, 0, 8, 16);
    MapDialogRect(&rcDlu);
    int pxX = rcDlu.Width() / 8;   if (pxX < 1) pxX = 1;
    int pxY = rcDlu.Height() / 16; if (pxY < 1) pxY = 1;

    /* 组框：关闭行为 */
    ::CreateWindowW(L"BUTTON", L"关闭行为",
                    WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                    12 * pxX, 8 * pxY, 226 * pxX, 68 * pxY,
                    m_hWnd, NULL, NULL, NULL);
    AddRadio(IDC_GS_ASK, L"关闭时每次询问", 24 * pxX, 26 * pxY, 190 * pxX, 14 * pxY, TRUE);
    AddRadio(IDC_GS_TRAY, L"最小化到托盘", 24 * pxX, 42 * pxY, 190 * pxX, 14 * pxY, FALSE);
    AddRadio(IDC_GS_EXIT, L"完全关闭", 24 * pxX, 58 * pxY, 190 * pxX, 14 * pxY, FALSE);

    /* 组框：音频（ASIO） */
    ::CreateWindowW(L"BUTTON", L"音频（ASIO）",
                    WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                    12 * pxX, 80 * pxY, 226 * pxX, 56 * pxY,
                    m_hWnd, NULL, NULL, NULL);
    AddStatic(0xF211, L"采样率 (Hz)", 24 * pxX, 94 * pxY, 72 * pxX, 12 * pxY);
    AddCombo(IDC_GS_RATE, 98 * pxX, 92 * pxY, 130 * pxX, 100 * pxY, TRUE);
    AddStatic(0xF212, L"缓冲 (帧)", 24 * pxX, 116 * pxY, 72 * pxX, 12 * pxY);
    AddCombo(IDC_GS_BUFFER, 98 * pxX, 114 * pxY, 130 * pxX, 100 * pxY, TRUE);

    /* 组框：MIDI（输入 + 输出） */
    ::CreateWindowW(L"BUTTON", L"MIDI",
                    WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                    12 * pxX, 140 * pxY, 226 * pxX, 118 * pxY,
                    m_hWnd, NULL, NULL, NULL);
    AddCheck(IDC_GS_MIDION, L"启用 MIDI 输入", 24 * pxX, 156 * pxY, 120 * pxX, 14 * pxY);
    AddCombo(IDC_GS_MIDIDEV, 148 * pxX, 154 * pxY, 82 * pxX, 100 * pxY, FALSE);
    AddStatic(0xF213, L"MIDI 通道", 24 * pxX, 176 * pxY, 60 * pxX, 12 * pxY);
    AddCombo(IDC_GS_MIDICH, 88 * pxX, 174 * pxY, 62 * pxX, 100 * pxY, FALSE);
    AddStatic(0xF215, L"MIDI 输出", 24 * pxX, 206 * pxY, 80 * pxX, 12 * pxY);
    AddCheck(IDC_GS_MIDIOUT, L"启用 MIDI 输出", 24 * pxX, 224 * pxY, 120 * pxX, 14 * pxY);
    AddCombo(IDC_GS_MIDIOUTDEV, 148 * pxX, 222 * pxY, 82 * pxX, 100 * pxY, FALSE);

    AddButton(IDC_GS_OK, L"确定", 70 * pxX, 290 * pxY, 52 * pxX, 16 * pxY, BS_DEFPUSHBUTTON);
    AddButton(IDC_GS_CANCEL, L"取消", 132 * pxX, 290 * pxY, 52 * pxX, 16 * pxY, 0);

    /* JACK 模式提示（平时隐藏） */
    ::CreateWindowW(L"STATIC", L"",
                    WS_CHILD | WS_VISIBLE | SS_LEFT,
                    12 * pxX, 266 * pxY, 226 * pxX, 14 * pxY,
                    m_hWnd, (HMENU)(INT_PTR)0xF214, NULL, NULL);

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

    /* 关闭行为：0=询问 1=托盘 2=完全关闭 */
    int action = m_pOwner ? m_pOwner->CloseAction() : 0;
    ((CButton *)GetDlgItem(IDC_GS_ASK))->SetCheck(action == 0 ? BST_CHECKED : BST_UNCHECKED);
    ((CButton *)GetDlgItem(IDC_GS_TRAY))->SetCheck(action == 1 ? BST_CHECKED : BST_UNCHECKED);
    ((CButton *)GetDlgItem(IDC_GS_EXIT))->SetCheck(action == 2 ? BST_CHECKED : BST_UNCHECKED);

    /* 采样率：常见值 + 当前/记忆值 */
    CComboBox *p = (CComboBox *)GetDlgItem(IDC_GS_RATE);
    if (p)
    {
        static const int rates[] = { 44100, 48000, 88200, 96000, 176400, 192000 };
        for (int i = 0; i < 6; i++)
        {
            CString s;
            s.Format(_T("%d"), rates[i]);
            p->AddString(s);
        }
        double cur = m_pOwner ? m_pOwner->AsioSampleRate() : 44100.0;
        CString cs;
        cs.Format(_T("%.0f"), cur);
        p->SetWindowText(cs);
    }
    /* 缓冲：常见值 + 当前/记忆值 */
    p = (CComboBox *)GetDlgItem(IDC_GS_BUFFER);
    if (p)
    {
        static const int bufs[] = { 64, 128, 256, 512, 1024, 2048 };
        for (int i = 0; i < 6; i++)
        {
            CString s;
            s.Format(_T("%d"), bufs[i]);
            p->AddString(s);
        }
        int cur = m_pOwner ? m_pOwner->AsioBufferSize() : 512;
        CString cs;
        cs.Format(_T("%d"), cur);
        p->SetWindowText(cs);
    }
    /* MIDI 输入 */
    if (m_pOwner)
    {
        ((CButton *)GetDlgItem(IDC_GS_MIDION))->SetCheck(
            m_pOwner->MidiInputEnabled() ? BST_CHECKED : BST_UNCHECKED);
    }
    CComboBox *pm = (CComboBox *)GetDlgItem(IDC_GS_MIDIDEV);
    if (pm)
    {
        int n = CMidiInput::GetDeviceCount();
        for (int i = 0; i < n; i++)
            pm->AddString(CString(CMidiInput::GetDeviceName(i).c_str()));
        if (n == 0)
            pm->AddString(_T("（无 MIDI 输入设备）"));
        int sel = m_pOwner ? m_pOwner->MidiDeviceIndex() : 0;
        if (sel < 0) sel = 0;
        if (sel >= n) sel = 0;
        pm->SetCurSel(sel);
    }
    /* MIDI 通道：Omni（全部）/ 1~16 */
    CComboBox *pch = (CComboBox *)GetDlgItem(IDC_GS_MIDICH);
    if (pch)
    {
        pch->AddString(_T("Omni（全部）"));
        for (int i = 1; i <= 16; i++)
        {
            CString s;
            s.Format(_T("通道 %d"), i);
            pch->AddString(s);
        }
        int ch = m_pOwner ? m_pOwner->MidiChannel() : 0;
        if (ch < 0) ch = 0;
        if (ch > 16) ch = 0;
        pch->SetCurSel(ch);
    }
    /* MIDI 输出 */
    if (m_pOwner)
    {
        ((CButton *)GetDlgItem(IDC_GS_MIDIOUT))->SetCheck(
            m_pOwner->MidiOutputEnabled() ? BST_CHECKED : BST_UNCHECKED);
    }
    CComboBox *pmo = (CComboBox *)GetDlgItem(IDC_GS_MIDIOUTDEV);
    if (pmo)
    {
        int n = CMidiOutput::GetDeviceCount();
        for (int i = 0; i < n; i++)
            pmo->AddString(CString(CMidiOutput::GetDeviceName(i).c_str()));
        if (n == 0)
            pmo->AddString(_T("（无 MIDI 输出设备）"));
        int sel = m_pOwner ? m_pOwner->MidiOutputDeviceIndex() : 0;
        if (sel < 0) sel = 0;
        if (sel >= n) sel = 0;
        pmo->SetCurSel(sel);
    }

    /* JACK 模式：ASIO（采样率/缓冲）与 MME MIDI（启用/设备）设置失效，
       仅 MIDI 通道仍可用（JACK 输入来自 midi_in 端口，输出走 midi_out） */
    bool isJack = m_pOwner && m_pOwner->BackendMode() == 1;
    if (isJack)
    {
        GetDlgItem(IDC_GS_RATE)->EnableWindow(FALSE);
        GetDlgItem(IDC_GS_BUFFER)->EnableWindow(FALSE);
        GetDlgItem(0xF211)->EnableWindow(FALSE);   /* 采样率标签 */
        GetDlgItem(0xF212)->EnableWindow(FALSE);   /* 缓冲标签 */
        GetDlgItem(IDC_GS_MIDION)->EnableWindow(FALSE);
        GetDlgItem(IDC_GS_MIDIDEV)->EnableWindow(FALSE);
        GetDlgItem(IDC_GS_MIDIOUT)->EnableWindow(FALSE);
        GetDlgItem(IDC_GS_MIDIOUTDEV)->EnableWindow(FALSE);
        GetDlgItem(0xF215)->EnableWindow(FALSE);   /* MIDI 输出标签 */
        CWnd *pTip = GetDlgItem(0xF214);
        if (pTip)
            pTip->SetWindowText(_T("JACK 模式：采样率/缓冲由服务器决定，MIDI 走 midi_in / midi_out 端口"));
    }
    return TRUE;
}

void CGlobalSettingsDlg::OnOK()
{
    if (!m_pOwner)
    {
        CDialog::OnOK();
        return;
    }

    /* 关闭行为 */
    int action = 0;
    if (((CButton *)GetDlgItem(IDC_GS_TRAY))->GetCheck() == BST_CHECKED) action = 1;
    else if (((CButton *)GetDlgItem(IDC_GS_EXIT))->GetCheck() == BST_CHECKED) action = 2;
    m_pOwner->ApplyCloseAction(action);

    /* MIDI 通道（两种模式都有效） */
    int midiCh = 0;
    CComboBox *pch = (CComboBox *)GetDlgItem(IDC_GS_MIDICH);
    if (pch)
    {
        int sel = pch->GetCurSel();
        midiCh = (sel >= 0) ? sel : 0;
    }

    if (m_pOwner->BackendMode() == 1)
    {
        /* JACK：ASIO/MIDI 设置失效，只应用 MIDI 通道 */
        m_pOwner->ApplyMidiChannel(midiCh);
        CDialog::OnOK();
        return;
    }

    /* ASIO：采样率 / 缓冲 / MIDI 启用 / 设备 / 通道 */
    CString s;
    GetDlgItemTextW(IDC_GS_RATE, s.GetBuffer(64), 64);
    s.ReleaseBuffer();
    double rate = _wtof(s);
    GetDlgItemTextW(IDC_GS_BUFFER, s.GetBuffer(64), 64);
    s.ReleaseBuffer();
    int buf = _wtoi(s);
    if (rate < 8000) rate = 44100;
    if (rate > 768000) rate = 44100;
    if (buf < 16) buf = 512;
    if (buf > 8192) buf = 512;

    bool midiOn = ((CButton *)GetDlgItem(IDC_GS_MIDION))->GetCheck() == BST_CHECKED;
    int midiDev = 0;
    CComboBox *pm = (CComboBox *)GetDlgItem(IDC_GS_MIDIDEV);
    if (pm)
    {
        int sel = pm->GetCurSel();
        midiDev = (sel >= 0) ? sel : 0;
    }

    /* MIDI 输出（MME，ASIO 后端用） */
    bool midiOutOn = ((CButton *)GetDlgItem(IDC_GS_MIDIOUT))->GetCheck() == BST_CHECKED;
    int midiOutDev = 0;
    CComboBox *pmo = (CComboBox *)GetDlgItem(IDC_GS_MIDIOUTDEV);
    if (pmo)
    {
        int sel = pmo->GetCurSel();
        midiOutDev = (sel >= 0) ? sel : 0;
    }

    m_pOwner->ApplyAsioSettings(rate, buf, midiOn, midiDev, midiCh,
                                midiOutOn, midiOutDev);
    CDialog::OnOK();
}

void CGlobalSettingsDlg::OnCancel()
{
    CDialog::OnCancel();
}
