// MeterSettingsDlg.cpp : 电平表设置窗口实现（滑块 + 输入框）
/******************************************************************************/
#include "MeterSettingsDlg.h"
#include "../app/MainWnd.h"        // 访问 CMainFrame 设置
#include "loudness_std.h"

#include <commctrl.h>
#include <shobjidl.h>

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

BEGIN_MESSAGE_MAP(CMeterSettingsDlg, CDialog)
    ON_WM_HSCROLL()
    ON_CONTROL_RANGE(EN_KILLFOCUS, IDC_MS_ED_REFRESH, IDC_MS_ED_CH,
                     &CMeterSettingsDlg::OnEditKillFocus)
    ON_BN_CLICKED(IDC_MS_CSV_BROWSE, &CMeterSettingsDlg::OnBrowseCsvFolder)
END_MESSAGE_MAP()

CMeterSettingsDlg::CMeterSettingsDlg(CMainFrame *owner)
    : m_pOwner(owner), m_pxX(1), m_pxY(1)
{
    BeginDlg(m_tmpl, DS_SETFONT | DS_MODALFRAME | DS_CENTER |
                     WS_POPUP | WS_CAPTION | WS_SYSMENU,
             0, 0, 250, 294, L"电平表设置");
    InitModalIndirect((LPCDLGTEMPLATE)m_tmpl.data());
}

BOOL CALLBACK CMeterSettingsDlg::SetChildFont(HWND hWnd, LPARAM lParam)
{
    ::SendMessageW(hWnd, WM_SETFONT, lParam, TRUE);
    return TRUE;
}

void CMeterSettingsDlg::AddStatic(int id, LPCWSTR text, int x, int y, int w, int h)
{
    ::CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                    x, y, w, h, m_hWnd, (HMENU)(INT_PTR)id, NULL, NULL);
}
void CMeterSettingsDlg::AddCombo(int id, int x, int y, int w, int h)
{
    ::CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST |
                    WS_VSCROLL | WS_TABSTOP, x, y, w, h, m_hWnd,
                    (HMENU)(INT_PTR)id, NULL, NULL);
}
void CMeterSettingsDlg::AddCheck(int id, LPCWSTR text, int x, int y, int w, int h)
{
    ::CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX |
                    WS_TABSTOP, x, y, w, h, m_hWnd, (HMENU)(INT_PTR)id, NULL, NULL);
}
void CMeterSettingsDlg::AddButton(int id, LPCWSTR text, int x, int y, int w, int h,
                                  DWORD extra)
{
    ::CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON |
                    WS_TABSTOP | extra, x, y, w, h, m_hWnd,
                    (HMENU)(INT_PTR)id, NULL, NULL);
}
void CMeterSettingsDlg::AddSlider(int id, int x, int y, int w, int h)
{
    ::CreateWindowW(L"msctls_trackbar32", L"",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_AUTOTICKS,
                    x, y, w, h, m_hWnd, (HMENU)(INT_PTR)id, NULL, NULL);
}
void CMeterSettingsDlg::AddEdit(int id, int x, int y, int w, int h)
{
    ::CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                    ES_AUTOHSCROLL | ES_RIGHT,
                    x, y, w, h, m_hWnd, (HMENU)(INT_PTR)id, NULL, NULL);
}

BOOL CMeterSettingsDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    /* 初始化滑块控件公共类 */
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    CRect rcDlu(0, 0, 8, 16);
    MapDialogRect(&rcDlu);
    m_pxX = rcDlu.Width() / 8;   if (m_pxX < 1) m_pxX = 1;
    m_pxY = rcDlu.Height() / 16; if (m_pxY < 1) m_pxY = 1;
    int pxX = m_pxX, pxY = m_pxY;

    /* 布局：标签(12,62) 滑块(78,82) 输入框(166,34) 单位(204,40) */
    int y = 8 * pxY;
    AddCheck(IDC_MS_SHOW, L"显示电平表", 12 * pxX, y, 200 * pxX, 14 * pxY);
    y += 22 * pxY;

    AddStatic(0xF410, L"刷新频率", 12 * pxX, y, 62 * pxX, 12 * pxY);
    AddSlider(IDC_MS_SL_REFRESH, 78 * pxX, y, 82 * pxX, 16 * pxY);
    AddEdit(IDC_MS_ED_REFRESH, 166 * pxX, y, 34 * pxX, 14 * pxY);
    AddStatic(0xF411, L"ms", 204 * pxX, y, 40 * pxX, 12 * pxY);
    y += 22 * pxY;

    AddStatic(0xF412, L"峰值保持时长", 12 * pxX, y, 62 * pxX, 12 * pxY);
    AddSlider(IDC_MS_SL_PEAK, 78 * pxX, y, 82 * pxX, 16 * pxY);
    AddEdit(IDC_MS_ED_PEAK, 166 * pxX, y, 34 * pxX, 14 * pxY);
    AddStatic(0xF413, L"s", 204 * pxX, y, 40 * pxX, 12 * pxY);
    y += 22 * pxY;

    AddCheck(IDC_MS_PEAKLINE, L"显示峰值保持线", 12 * pxX, y, 200 * pxX, 14 * pxY);
    y += 22 * pxY;

    AddCheck(IDC_MS_VALUEBOX, L"显示底部数值框", 12 * pxX, y, 200 * pxX, 14 * pxY);
    y += 22 * pxY;

    AddStatic(0xF414, L"响度标准", 12 * pxX, y, 62 * pxX, 12 * pxY);
    AddCombo(IDC_MS_STD, 78 * pxX, y, 160 * pxX, 80 * pxY);
    y += 22 * pxY;

    AddStatic(0xF415, L"静音重置", 12 * pxX, y, 62 * pxX, 12 * pxY);
    AddSlider(IDC_MS_SL_SIL, 78 * pxX, y, 82 * pxX, 16 * pxY);
    AddEdit(IDC_MS_ED_SIL, 166 * pxX, y, 34 * pxX, 14 * pxY);
    AddStatic(0xF416, L"s", 204 * pxX, y, 40 * pxX, 12 * pxY);
    y += 22 * pxY;

    AddStatic(0xF417, L"静音阈值", 12 * pxX, y, 62 * pxX, 12 * pxY);
    AddSlider(IDC_MS_SL_THRESH, 78 * pxX, y, 82 * pxX, 16 * pxY);
    AddEdit(IDC_MS_ED_THRESH, 166 * pxX, y, 34 * pxX, 14 * pxY);
    AddStatic(0xF418, L"LUFS", 204 * pxX, y, 40 * pxX, 12 * pxY);
    y += 22 * pxY;

    /* CSV 响度日志 */
    AddCheck(IDC_MS_CSVLOG, L"CSV 响度日志", 12 * pxX, y, 150 * pxX, 14 * pxY);
    y += 22 * pxY;

    AddStatic(0xF419, L"记录间隔", 12 * pxX, y, 62 * pxX, 12 * pxY);
    AddSlider(IDC_MS_SL_CSV, 78 * pxX, y, 82 * pxX, 16 * pxY);
    AddEdit(IDC_MS_ED_CSV, 166 * pxX, y, 34 * pxX, 14 * pxY);
    AddStatic(0xF41A, L"s", 204 * pxX, y, 40 * pxX, 12 * pxY);
    y += 22 * pxY;

    AddStatic(0xF41C, L"通道上限", 12 * pxX, y, 62 * pxX, 12 * pxY);
    AddSlider(IDC_MS_SL_CH, 78 * pxX, y, 82 * pxX, 16 * pxY);
    AddEdit(IDC_MS_ED_CH, 166 * pxX, y, 34 * pxX, 14 * pxY);
    AddStatic(0xF41D, L"ch", 204 * pxX, y, 40 * pxX, 12 * pxY);
    y += 22 * pxY;

    AddStatic(0xF41B, L"输出文件夹", 12 * pxX, y, 62 * pxX, 12 * pxY);
    ::CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL |
                    ES_READONLY, 78 * pxX, y, 130 * pxX, 14 * pxY, m_hWnd,
                    (HMENU)(INT_PTR)IDC_MS_CSVFOLDER, NULL, NULL);
    AddButton(IDC_MS_CSV_BROWSE, L"浏览...", 212 * pxX, y, 30 * pxX, 16 * pxY, 0);
    y += 24 * pxY;

    AddButton(IDC_MS_OK, L"确定", 78 * pxX, y, 60 * pxX, 16 * pxY, BS_DEFPUSHBUTTON);
    AddButton(IDC_MS_CANCEL, L"取消", 148 * pxX, y, 60 * pxX, 16 * pxY, 0);

    /* 系统 UI 字体（控件全部创建后设置） */
    NONCLIENTMETRICSW ncm;
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        m_fontDlg.CreateFontIndirectW(&ncm.lfMessageFont);
    if ((HFONT)m_fontDlg)
    {
        SetFont(&m_fontDlg);
        EnumChildWindows(m_hWnd, SetChildFont, (LPARAM)(HFONT)m_fontDlg);
    }

    /* 当前值（从 owner） */
    int refreshMs = m_pOwner ? m_pOwner->MeterRefreshMs() : 50;
    double peakHold = m_pOwner ? m_pOwner->PeakHoldSeconds() : 1.0;
    double sil = m_pOwner ? m_pOwner->SilenceReset() : 0.0;
    double th = m_pOwner ? m_pOwner->SilenceThresh() : -70.0;
    int std = m_pOwner ? m_pOwner->LoudnessStd() : 0;

    /* 滑块范围 + 位置 + 输入框同步 */
    CSliderCtrl *sl;
    sl = (CSliderCtrl *)GetDlgItem(IDC_MS_SL_REFRESH);
    if (sl) { sl->SetRange(30, 200, TRUE); sl->SetTicFreq(10); sl->SetPos(refreshMs); }
    SyncSliderToEdit(IDC_MS_SL_REFRESH, IDC_MS_ED_REFRESH, MODE_REFRESH);

    sl = (CSliderCtrl *)GetDlgItem(IDC_MS_SL_PEAK);
    if (sl) { sl->SetRange(0, 50, TRUE); sl->SetTicFreq(5); sl->SetPos((int)(peakHold * 10)); }
    SyncSliderToEdit(IDC_MS_SL_PEAK, IDC_MS_ED_PEAK, MODE_PEAK);

    sl = (CSliderCtrl *)GetDlgItem(IDC_MS_SL_SIL);
    if (sl) { sl->SetRange(0, 60, TRUE); sl->SetTicFreq(10); sl->SetPos((int)sil); }
    SyncSliderToEdit(IDC_MS_SL_SIL, IDC_MS_ED_SIL, MODE_SILENCE);

    sl = (CSliderCtrl *)GetDlgItem(IDC_MS_SL_THRESH);
    if (sl) { sl->SetRange(0, 50, TRUE); sl->SetTicFreq(5); sl->SetPos((int)(th + 90)); }
    SyncSliderToEdit(IDC_MS_SL_THRESH, IDC_MS_ED_THRESH, MODE_THRESH);

    sl = (CSliderCtrl *)GetDlgItem(IDC_MS_SL_CSV);
    if (sl) { sl->SetRange(1, 60, TRUE); sl->SetTicFreq(5); sl->SetPos(m_pOwner ? m_pOwner->CsvIntervalMs() / 1000 : 1); if (sl->GetPos() < 1) sl->SetPos(1); }
    SyncSliderToEdit(IDC_MS_SL_CSV, IDC_MS_ED_CSV, MODE_CSV);

    sl = (CSliderCtrl *)GetDlgItem(IDC_MS_SL_CH);
    if (sl) { sl->SetRange(1, 255, TRUE); sl->SetTicFreq(16); sl->SetPos(m_pOwner ? m_pOwner->MeterMaxCh() : 17); if (sl->GetPos() < 1) sl->SetPos(1); }
    SyncSliderToEdit(IDC_MS_SL_CH, IDC_MS_ED_CH, MODE_CH);

    CComboBox *p = (CComboBox *)GetDlgItem(IDC_MS_STD);
    if (p)
    {
        for (int i = 0; i < g_loudnessStdCount; i++)
            p->AddString(g_loudnessStds[i].name);
        if (std < 0) std = 0;
        if (std >= g_loudnessStdCount) std = g_loudnessStdCount - 1;
        p->SetCurSel(std);
    }

    /* 勾选状态 */
    if (m_pOwner)
    {
        ((CButton *)GetDlgItem(IDC_MS_SHOW))->SetCheck(
            m_pOwner->ShowMeters() ? BST_CHECKED : BST_UNCHECKED);
        ((CButton *)GetDlgItem(IDC_MS_PEAKLINE))->SetCheck(
            m_pOwner->ShowPeakLine() ? BST_CHECKED : BST_UNCHECKED);
        ((CButton *)GetDlgItem(IDC_MS_VALUEBOX))->SetCheck(
            m_pOwner->ShowValueBox() ? BST_CHECKED : BST_UNCHECKED);
        ((CButton *)GetDlgItem(IDC_MS_CSVLOG))->SetCheck(
            m_pOwner->CsvLog() ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemTextW(IDC_MS_CSVFOLDER, m_pOwner->CsvFolder().c_str());
    }
    return TRUE;
}

/*****************************************************************************/
/* 滑块 <-> 输入框 同步                                                       */
/*   MODE_REFRESH: 位置=ms(30~200 步进10)                                    */
/*   MODE_PEAK   : 位置×0.1 = 秒(0~5.0 步进0.5)                              */
/*   MODE_SILENCE: 位置=秒(0~60)                                             */
/*   MODE_THRESH : 位置-90 = LUFS(-90~-40)                                   */
/*****************************************************************************/
bool CMeterSettingsDlg::ParseVal(const CString &s, int mode, int &sliderPos)
{
    CString t = s;
    t.Trim();
    double v = _wtof(t);
    double minV, maxV, step;
    switch (mode)
    {
    case MODE_REFRESH: minV = 30;  maxV = 200; step = 10;  break;
    case MODE_PEAK:    minV = 0;   maxV = 5;   step = 0.5; break;
    case MODE_SILENCE: minV = 0;   maxV = 60;  step = 1;   break;
    case MODE_CSV:     minV = 1;   maxV = 60;  step = 1;   break;
    case MODE_CH:      minV = 1;   maxV = 255; step = 1;   break;
    default:           minV = -90; maxV = -40; step = 1;   break;
    }
    if (v < minV) v = minV;
    if (v > maxV) v = maxV;
    /* 对齐步进 */
    v = minV + (int)((v - minV) / step + 0.5) * step;
    switch (mode)
    {
    case MODE_REFRESH: sliderPos = (int)v; break;
    case MODE_PEAK:    sliderPos = (int)(v * 10); break;
    case MODE_SILENCE: sliderPos = (int)v; break;
    case MODE_CSV:     sliderPos = (int)v; break;
    case MODE_CH:      sliderPos = (int)v; break;
    default:           sliderPos = (int)(v + 90); break;
    }
    return true;
}

void CMeterSettingsDlg::SyncSliderToEdit(int sliderId, int editId, int mode)
{
    CSliderCtrl *sl = (CSliderCtrl *)GetDlgItem(sliderId);
    if (!sl) return;
    int pos = sl->GetPos();
    CString s;
    switch (mode)
    {
    case MODE_REFRESH: s.Format(_T("%d"), pos); break;
    case MODE_PEAK:    s.Format(_T("%.1f"), pos * 0.1); break;
    case MODE_SILENCE: s.Format(_T("%d"), pos); break;
    case MODE_CSV:     s.Format(_T("%d"), pos); break;
    case MODE_CH:      s.Format(_T("%d"), pos); break;
    default:           s.Format(_T("%d"), pos - 90); break;
    }
    CWnd *ed = GetDlgItem(editId);
    if (ed) ed->SetWindowText(s);
}

void CMeterSettingsDlg::SyncEditToSlider(int editId, int sliderId, int mode)
{
    CWnd *ed = GetDlgItem(editId);
    CSliderCtrl *sl = (CSliderCtrl *)GetDlgItem(sliderId);
    if (!ed || !sl) return;
    CString s;
    ed->GetWindowText(s);
    int pos = sl->GetPos();
    if (ParseVal(s, mode, pos))
    {
        sl->SetPos(pos);
        SyncSliderToEdit(sliderId, editId, mode);   /* 规范化显示 */
    }
}

void CMeterSettingsDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar)
{
    if (pScrollBar)
    {
        int id = pScrollBar->GetDlgCtrlID();
        if (id >= IDC_MS_SL_REFRESH && id <= IDC_MS_SL_CH)
        {
            int mode = id - IDC_MS_SL_REFRESH;
            SyncSliderToEdit(id, id - 0x10, mode);
        }
    }
    CDialog::OnHScroll(nSBCode, nPos, pScrollBar);
}

void CMeterSettingsDlg::OnEditKillFocus(UINT nID)
{
    if (nID >= IDC_MS_ED_REFRESH && nID <= IDC_MS_ED_CH)
    {
        int mode = nID - IDC_MS_ED_REFRESH;
        SyncEditToSlider(nID, nID + 0x10, mode);
    }
}

void CMeterSettingsDlg::OnOK()
{
    if (!m_pOwner)
    {
        CDialog::OnOK();
        return;
    }

    /* 先同步输入框（若正在编辑） */
    for (int m = 0; m <= MODE_CH; m++)
        SyncEditToSlider(IDC_MS_ED_REFRESH + m, IDC_MS_SL_REFRESH + m, m);

    bool show = ((CButton *)GetDlgItem(IDC_MS_SHOW))->GetCheck() == BST_CHECKED;
    bool peakline = ((CButton *)GetDlgItem(IDC_MS_PEAKLINE))->GetCheck() == BST_CHECKED;
    bool valuebox = ((CButton *)GetDlgItem(IDC_MS_VALUEBOX))->GetCheck() == BST_CHECKED;

    CSliderCtrl *sl;
    int refreshMs = 50; double peakHold = 1.0; double sil = 0.0; double th = -70.0;
    int csvInterval = 1;
    sl = (CSliderCtrl *)GetDlgItem(IDC_MS_SL_REFRESH);
    if (sl) refreshMs = sl->GetPos();
    sl = (CSliderCtrl *)GetDlgItem(IDC_MS_SL_PEAK);
    if (sl) peakHold = sl->GetPos() * 0.1;
    sl = (CSliderCtrl *)GetDlgItem(IDC_MS_SL_SIL);
    if (sl) sil = sl->GetPos();
    sl = (CSliderCtrl *)GetDlgItem(IDC_MS_SL_THRESH);
    if (sl) th = sl->GetPos() - 90;
    sl = (CSliderCtrl *)GetDlgItem(IDC_MS_SL_CSV);
    if (sl) csvInterval = sl->GetPos();
    if (csvInterval < 1) csvInterval = 1;

    int maxCh = 17;
    sl = (CSliderCtrl *)GetDlgItem(IDC_MS_SL_CH);
    if (sl) maxCh = sl->GetPos();
    if (maxCh < 1) maxCh = 1;
    if (maxCh > 255) maxCh = 255;

    bool csvLog = ((CButton *)GetDlgItem(IDC_MS_CSVLOG))->GetCheck() == BST_CHECKED;
    CString csvDir;
    GetDlgItemTextW(IDC_MS_CSVFOLDER, csvDir.GetBuffer(1024), 1024);
    csvDir.ReleaseBuffer();

    int std = 0;
    CComboBox *p = (CComboBox *)GetDlgItem(IDC_MS_STD);
    if (p) std = p->GetCurSel();
    if (std < 0) std = 0;
    if (std >= g_loudnessStdCount) std = g_loudnessStdCount - 1;

    m_pOwner->ApplyMeterSettings(show, refreshMs, peakHold, peakline, valuebox,
                                 std, sil, th, maxCh);
    m_pOwner->ApplyCsvSettings(csvLog, csvInterval * 1000,
                               (const wchar_t *)csvDir);

    CDialog::OnOK();
}

/*****************************************************************************/
/* OnBrowseCsvFolder : 系统文件夹选择对话框                                  */
/*****************************************************************************/
void CMeterSettingsDlg::OnBrowseCsvFolder()
{
    IFileDialog *pfd = NULL;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
    if (FAILED(hr))
        return;
    DWORD opts = 0;
    pfd->GetOptions(&opts);
    pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    pfd->SetTitle(L"选择 CSV 日志输出文件夹");
    if (SUCCEEDED(pfd->Show(m_hWnd)))
    {
        IShellItem *psi = NULL;
        if (SUCCEEDED(pfd->GetResult(&psi)))
        {
            PWSTR path = NULL;
            if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &path)))
            {
                SetDlgItemTextW(IDC_MS_CSVFOLDER, path);
                if (m_pOwner)
                    m_pOwner->SetCsvFolder(path);
                CoTaskMemFree(path);
            }
            psi->Release();
        }
    }
    pfd->Release();
}

void CMeterSettingsDlg::OnCancel()
{
    CDialog::OnCancel();
}
