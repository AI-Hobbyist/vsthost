// LevelMeterDlg.cpp : 独立电平表窗口实现（无模式自绘）
/******************************************************************************/
#include "LevelMeterDlg.h"
#include "../app/MainWnd.h"        // 访问 CMainFrame 数据（峰值/响度/设置）
#include "loudness_std.h"

#include <cmath>

// 控件 ID
#define IDC_LM_STD      0xF301
#define IDC_LM_THRESH   0xF305
#define IDC_LM_SILENCE  0xF302
#define IDC_LM_RESET    0xF303

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

BEGIN_MESSAGE_MAP(CLevelMeterDlg, CDialog)
    ON_WM_TIMER()
    ON_WM_SIZE()
    ON_WM_PAINT()
    ON_WM_CLOSE()
    ON_CBN_SELCHANGE(IDC_LM_STD, &CLevelMeterDlg::OnStdChanged)
    ON_BN_CLICKED(IDC_LM_RESET, &CLevelMeterDlg::OnResetIntegrated)
    ON_BN_CLICKED(0xF306, &CLevelMeterDlg::OnLoudSrcClicked)
    ON_BN_CLICKED(0xF307, &CLevelMeterDlg::OnLoudSrcClicked)
END_MESSAGE_MAP()

CLevelMeterDlg::CLevelMeterDlg(CMainFrame *owner)
    : m_pOwner(owner), m_pxX(1), m_pxY(1), m_bLoudInput(false)
{
    /* 窗口 DLU：360 x 204 */
    BeginDlg(m_tmpl, DS_SETFONT | DS_MODALFRAME | DS_CENTER |
                     WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
             0, 0, 360, 204, L"电平表");
}

BOOL CLevelMeterDlg::Create(CWnd *pParent)
{
    if (!CreateIndirect(m_tmpl.data(), pParent))
        return FALSE;
    return TRUE;
}

void CLevelMeterDlg::AddStatic(int id, LPCWSTR text, int x, int y, int w, int h)
{
    ::CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                    x, y, w, h, m_hWnd, (HMENU)(INT_PTR)id, NULL, NULL);
}
void CLevelMeterDlg::AddCombo(int id, int x, int y, int w, int h)
{
    ::CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST |
                    WS_VSCROLL | WS_TABSTOP, x, y, w, h, m_hWnd,
                    (HMENU)(INT_PTR)id, NULL, NULL);
}
void CLevelMeterDlg::AddButton(int id, LPCWSTR text, int x, int y, int w, int h)
{
    ::CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON |
                    WS_TABSTOP, x, y, w, h, m_hWnd, (HMENU)(INT_PTR)id, NULL, NULL);
}
void CLevelMeterDlg::AddRadio(int id, LPCWSTR text, int x, int y, int w, int h,
                              bool group)
{
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON;
    if (group) style |= WS_GROUP;
    ::CreateWindowW(L"BUTTON", text, style, x, y, w, h, m_hWnd,
                    (HMENU)(INT_PTR)id, NULL, NULL);
}

BOOL CLevelMeterDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    CRect rcDlu(0, 0, 8, 16);
    MapDialogRect(&rcDlu);
    m_pxX = rcDlu.Width() / 8;  if (m_pxX < 1) m_pxX = 1;
    m_pxY = rcDlu.Height() / 16; if (m_pxY < 1) m_pxY = 1;

    /* 顶部操作栏：标准下拉 / 响度源单选 / 重置按钮（同一行，不覆盖表头） */
    int y = 5 * m_pxY, ch = 18 * m_pxY;
    AddStatic(0xF300, L"标准", 8 * m_pxX, y, 30 * m_pxX, 12 * m_pxY);
    AddCombo(IDC_LM_STD, 32 * m_pxX, y, 98 * m_pxX, 90 * m_pxY);
    AddRadio(0xF306, L"输出", 136 * m_pxX, y, 60 * m_pxX, 14 * m_pxY, TRUE);
    AddRadio(0xF307, L"输入", 200 * m_pxX, y, 60 * m_pxX, 14 * m_pxY, FALSE);
    AddButton(IDC_LM_RESET, L"重置 I", 268 * m_pxX, y, 88 * m_pxX, ch);
    ((CButton *)GetDlgItem(0xF306))->SetCheck(BST_CHECKED);   /* 默认输出 */

    /* 重置按钮与标准下拉框等高（顶部对齐） */
    {
        HWND hCombo = ::GetDlgItem(m_hWnd, IDC_LM_STD);
        HWND hBtn = ::GetDlgItem(m_hWnd, IDC_LM_RESET);
        if (hCombo && hBtn)
        {
            RECT rcC, rcB;
            ::GetWindowRect(hCombo, &rcC);
            ::GetWindowRect(hBtn, &rcB);
            POINT pt = { rcB.left, rcB.top };
            ::ScreenToClient(m_hWnd, &pt);
            ::MoveWindow(hBtn, pt.x, pt.y, rcB.right - rcB.left,
                         rcC.bottom - rcC.top, TRUE);
        }
    }

    /* 系统 UI 字体应用到全部子控件（控件全部创建后设置） */
    NONCLIENTMETRICSW ncm;
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        m_fontUI.CreateFontIndirectW(&ncm.lfMessageFont);
    if ((HFONT)m_fontUI)
    {
        SetFont(&m_fontUI);
        EnumChildWindows(m_hWnd, SetChildFont, (LPARAM)(HFONT)m_fontUI);
        /* 数值框小字体（基于系统 UI 字体缩小） */
        LOGFONTW lf;
        m_fontUI.GetLogFont(&lf);
        lf.lfHeight = -11;
        m_fontVal.CreateFontIndirectW(&lf);
    }

    /* 填充标准（含平台参考） */
    CComboBox *pStd = (CComboBox *)GetDlgItem(IDC_LM_STD);
    if (pStd)
    {
        for (int i = 0; i < g_loudnessStdCount; i++)
            pStd->AddString(g_loudnessStds[i].name);
        int std = m_pOwner ? m_pOwner->LoudnessStd() : 0;
        if (std < 0) std = 0;
        if (std >= g_loudnessStdCount) std = g_loudnessStdCount - 1;
        pStd->SetCurSel(std);
    }

    /* 刷新周期跟随主窗口峰值表（默认 50ms，可配 30/50/80/100ms） */
    ResyncTimer();
    return TRUE;
}

BOOL CALLBACK CLevelMeterDlg::SetChildFont(HWND hWnd, LPARAM lParam)
{
    ::SendMessageW(hWnd, WM_SETFONT, lParam, TRUE);
    return TRUE;
}

void CLevelMeterDlg::OnOK()       { OnClose(); }
void CLevelMeterDlg::OnCancel()   { OnClose(); }

void CLevelMeterDlg::OnClose()
{
    DestroyWindow();
}

void CLevelMeterDlg::PostNcDestroy()
{
    if (m_pOwner)
        m_pOwner->OnMeterWindowClosed();
    CDialog::PostNcDestroy();
    delete this;
}

void CLevelMeterDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 1 && m_hWnd)
        InvalidateRect(&m_drawRc, FALSE);
    CDialog::OnTimer(nIDEvent);
}

/*****************************************************************************/
/* ResyncTimer : 按主窗口当前刷新周期重设定时器（与峰值表平滑刷新一致）       */
/*****************************************************************************/
void CLevelMeterDlg::ResyncTimer()
{
    if (!m_hWnd)
        return;
    int ms = m_pOwner ? m_pOwner->MeterRefreshMs() : 50;
    if (ms < 20) ms = 20;
    if (ms > 200) ms = 200;
    KillTimer(1);
    SetTimer(1, (UINT)ms, NULL);
}

void CLevelMeterDlg::OnSize(UINT nType, int cx, int cy)
{
    CDialog::OnSize(nType, cx, cy);
    CRect rc;
    GetClientRect(&rc);
    m_drawRc = CRect(rc.left, rc.top + 44, rc.right, rc.bottom);
    if (m_drawRc.Height() < 40) m_drawRc.bottom = m_drawRc.top + 40;
    Invalidate(FALSE);
}

void CLevelMeterDlg::OnStdChanged()
{
    if (!m_pOwner) return;
    CComboBox *p = (CComboBox *)GetDlgItem(IDC_LM_STD);
    if (p) m_pOwner->SetLoudnessStd(p->GetCurSel());
}

void CLevelMeterDlg::OnResetIntegrated()
{
    if (m_pOwner) m_pOwner->ResetLoudness();
}

void CLevelMeterDlg::OnLoudSrcClicked()
{
    CButton *bIn = (CButton *)GetDlgItem(0xF307);
    m_bLoudInput = bIn && bIn->GetCheck() == BST_CHECKED;
    if (m_hWnd) InvalidateRect(&m_drawRc, FALSE);
}

void CLevelMeterDlg::OnPaint()
{
    CPaintDC dc(this);
    DrawMeters(dc);
}

/*===========================================================================*/
/* 绘制：左区输入/输出峰值（左输入右输出，栏宽按通道数比例）+ 右区 M/S/I/LRA  */
/*===========================================================================*/
void CLevelMeterDlg::DrawMeters(CDC &dc)
{
    CRect rc = m_drawRc;
    if (rc.Width() < 80 || rc.Height() < 40)
        return;
    dc.FillSolidRect(rc, RGB(16, 16, 16));

    int nIn = m_pOwner ? m_pOwner->MeterInCh() : 0;
    int nOut = m_pOwner ? m_pOwner->MeterOutCh() : 0;
    if (nIn > 8) nIn = 8;
    if (nOut > 8) nOut = 8;

    int std = m_pOwner ? m_pOwner->LoudnessStd() : 0;
    if (std < 0 || std >= g_loudnessStdCount) std = 0;
    double offset = (g_loudnessStds[std].unit == 1) ? 23.0 : 0.0;  // EBU: 0 LU = -23 LUFS
    const wchar_t *unit = (g_loudnessStds[std].unit == 1) ? L"LU"
                         : ((g_loudnessStds[std].unit == 2) ? L"LKFS" : L"LUFS");
    double refLufs = g_loudnessStds[std].refLufs;

    LoudnessCore::Result ri, ro;
    if (m_pOwner)
    {
        ri = m_pOwner->LoudIn().GetResult();
        ro = m_pOwner->LoudOut().GetResult();
    }
    /* 响度源：默认输出，可切换输入 */
    LoudnessCore::Result rl = m_bLoudInput ? ri : ro;
    double tpDb = (rl.peak > 0.000001) ? 20.0 * log10(rl.peak) : -60.0;
    bool   tpValid = (rl.peak > 0.000001);

    CFont *pOld = (CFont *)dc.SelectObject(&m_fontUI);
    dc.SetBkMode(TRANSPARENT);

    /* ---- 左区：输入/输出峰值（左输入右输出，宽度按通道数比例） ---- */
    int lw = (int)(rc.Width() * 0.44);
    if (lw < 160) lw = 160;
    CRect lrc(rc.left, rc.top, rc.left + lw, rc.bottom);
    dc.FillSolidRect(lrc, RGB(20, 20, 20));
    dc.DrawEdge((LPRECT)(LPCRECT)lrc, EDGE_ETCHED, BF_RECT);

    int total = nIn + nOut;
    if (total < 1) total = 1;
    int inner = lrc.Width() - 12;
    int inW = (nIn > 0) ? (int)(inner * nIn / total) : 0;
    int outW = (nOut > 0) ? (inner - inW) : 0;

    int topY = lrc.top + 22;
    int botY = lrc.bottom - 4;
    if (botY - topY < 30) botY = topY + 30;

    if (nIn > 0)
    {
        CRect irc(lrc.left + 4, topY, lrc.left + 4 + inW, botY);
        dc.SetTextColor(RGB(180, 220, 180));
        CString t = _T("输入");
        CSize ts = dc.GetTextExtent(t);
        dc.TextOut(irc.left + (irc.Width() - ts.cx) / 2, lrc.top + 4, t);
        DrawChannelRow(dc, irc, nIn, m_pOwner ? m_pOwner->InLevel() : NULL,
                       m_pOwner ? m_pOwner->InHold() : NULL);
    }
    if (nOut > 0)
    {
        CRect orc(lrc.left + 8 + inW, topY, lrc.left + 4 + inW + outW, botY);
        dc.SetTextColor(RGB(200, 180, 220));
        CString t = _T("输出");
        CSize ts = dc.GetTextExtent(t);
        dc.TextOut(orc.left + (orc.Width() - ts.cx) / 2, lrc.top + 4, t);
        DrawChannelRow(dc, orc, nOut, m_pOwner ? m_pOwner->OutLevel() : NULL,
                       m_pOwner ? m_pOwner->OutHold() : NULL);
    }
    /* 输入/输出两栏之间分隔线 */
    if (nIn > 0 && nOut > 0)
    {
        int sx = lrc.left + 4 + inW + 2;
        dc.FillSolidRect(CRect(sx, topY, sx + 1, botY), RGB(90, 90, 90));
    }

    /* ---- 右区：M/S/I/LRA 四个垂直条 ---- */
    int rx = rc.left + lw + 10;
    int rw = rc.right - rx;
    if (rw < 220) rw = 220;
    CRect rrc(rx, rc.top, rx + rw, rc.bottom);
    dc.FillSolidRect(rrc, RGB(20, 20, 20));
    dc.DrawEdge((LPRECT)(LPCRECT)rrc, EDGE_ETCHED, BF_RECT);

    CString ttl;
    ttl.Format(_T("响度  (%s)"), unit);
    dc.SetTextColor(RGB(220, 200, 160));
    CSize ts = dc.GetTextExtent(ttl);
    int ttx = rrc.left + (rrc.Width() - ts.cx) / 2;
    if (ttx < rrc.left + 2) ttx = rrc.left + 2;
    dc.TextOut(ttx, rrc.top + 4, ttl);

    int barTop = rrc.top + 38;      /* 标题(4~19) + 标签(38-18=20) */
    int barBottom = rrc.bottom - 24;
    if (barBottom - barTop < 40) barBottom = barTop + 40;
    /* 五列固定等宽（统一宽度）+ 整体居中，窗口变化不溢出 */
    int gap = 8;
    int bw = (rrc.Width() - gap * 6) / 5;
    if (bw < 18) bw = 18;
    if (bw > 52) bw = 52;
    int totalW = bw * 5 + gap * 4;
    int x0 = rrc.left + (rrc.Width() - totalW) / 2;
    if (x0 < rrc.left + 2) x0 = rrc.left + 2;

    struct Bar
    {
        const wchar_t *label;
        double value;
        bool   valid;
        bool   isLra;
        bool   isTp;
    };
    Bar bars[5];
    bars[0] = { L"Momentary", rl.momentary, rl.haveMomentary, false, false };
    bars[1] = { L"Short-term", rl.shortTerm, rl.haveShortTerm, false, false };
    bars[2] = { L"Integrated", rl.integrated, rl.haveIntegrated, false, false };
    bars[3] = { L"LRA", rl.lra, true, true, false };
    bars[4] = { L"True Peak", tpDb, tpValid, false, true };

    for (int i = 0; i < 5; i++)
    {
        int bx = x0 + i * (bw + gap);
        CRect brc(bx, barTop, bx + bw, barBottom);

        /* 标签（全名，小字体居中于条上方） */
        CFont *pOldF = (CFont *)dc.SelectObject(&m_fontVal);
        dc.SetTextColor(RGB(200, 200, 200));
        CSize ts = dc.GetTextExtent(bars[i].label);
        int lx = bx + (bw - ts.cx) / 2;
        if (lx < rrc.left + 2) lx = rrc.left + 2;
        dc.TextOut(lx, barTop - 17, bars[i].label);
        dc.SelectObject(pOldF);

        if (bars[i].isTp)
        {
            /* True Peak：dBTP 刻度 -60~+12；分段 绿≤limit / 黄 limit~0 / 红 0~+12 */
            dc.Draw3dRect(brc, RGB(60, 60, 60), RGB(40, 40, 40));
            double v = bars[i].value;
            if (v < -60) v = -60;
            if (v > 12) v = 12;
            int h = (int)((v + 60.0) / 72.0 * brc.Height());
            if (h < 1) h = 1;
            int top = brc.bottom - h;
            double limDb = g_loudnessStds[std].tpLimit;   /* 黄区起点 = 标准真峰值上限 */
            int redY = brc.bottom - (int)(60.0 / 72.0 * brc.Height());        /* 0dB */
            int limY = brc.bottom - (int)((limDb + 60.0) / 72.0 * brc.Height()); /* limit */
            if (top < redY) dc.FillSolidRect(CRect(brc.left, top, brc.right, redY), RGB(240, 60, 50));
            int y2 = (top > redY) ? top : redY;
            if (y2 < limY) dc.FillSolidRect(CRect(brc.left, y2, brc.right, limY), RGB(240, 200, 40));
            int g2 = (top > limY) ? top : limY;
            if (g2 < brc.bottom) dc.FillSolidRect(CRect(brc.left, g2, brc.right, brc.bottom), RGB(60, 200, 80));
            /* 独立数值框 */
            CRect nrc(bx, barBottom - 18, bx + bw, barBottom);
            dc.FillSolidRect(nrc, RGB(12, 12, 12));
            CFont *pOld = (CFont *)dc.SelectObject(&m_fontVal);
            dc.SetTextColor(RGB(200, 220, 255));
            CString s;
            if (bars[i].valid)
                s.Format(_T("%.1f dBTP"), v);
            else
                s = _T("-Inf");
            dc.TextOut(nrc.left + 2, nrc.top + 2, s);
            dc.SelectObject(pOld);
        }
        else if (bars[i].isLra)
        {
            /* LRA：垂直条 0~20 LU（与 M/S/I 同布局） */
            dc.Draw3dRect(brc, RGB(60, 60, 60), RGB(40, 40, 40));
            double v = bars[i].value;
            if (v < 0) v = 0;
            if (v > 20) v = 20;
            int h = (int)(brc.Height() * v / 20.0);
            if (h > 0)
                dc.FillSolidRect(CRect(brc.left, brc.bottom - h, brc.right, brc.bottom),
                                 RGB(120, 170, 240));
            /* 独立数值框 */
            CRect nrc(bx, barBottom - 18, bx + bw, barBottom);
            dc.FillSolidRect(nrc, RGB(12, 12, 12));
            CFont *pOld = (CFont *)dc.SelectObject(&m_fontVal);
            dc.SetTextColor(RGB(170, 200, 240));
            CString s;
            s.Format(_T("%.1f LU"), bars[i].value);
            dc.TextOut(nrc.left + 2, nrc.top + 2, s);
            dc.SelectObject(pOld);
        }
        else
        {
            /* M/S/I：垂直条 -70~0 LUFS
               分段：绿 ≤ 参考线 / 黄 超过参考线但低于 -5 / 红 ≥ -5 */
            dc.Draw3dRect(brc, RGB(60, 60, 60), RGB(40, 40, 40));
            double v = bars[i].value;
            if (v < -70) v = -70;
            if (v > 0) v = 0;
            int h = (int)((v + 70.0) / 70.0 * brc.Height());
            if (h < 1) h = 1;
            int top = brc.bottom - h;
            int redY = brc.bottom - (int)((70.0 - 5.0) / 70.0 * brc.Height());   // -5：红区起
            int refY = brc.bottom - (int)((70.0 + refLufs) / 70.0 * brc.Height()); // 参考线：黄/绿界
            if (top < redY) dc.FillSolidRect(CRect(brc.left, top, brc.right, redY), RGB(240, 60, 50));
            int y2 = (top > redY) ? top : redY;
            if (y2 < refY) dc.FillSolidRect(CRect(brc.left, y2, brc.right, refY), RGB(240, 200, 40));
            int g2 = (top > refY) ? top : refY;
            if (g2 < brc.bottom) dc.FillSolidRect(CRect(brc.left, g2, brc.right, brc.bottom), RGB(60, 200, 80));

            /* 独立数值框 */
            CRect nrc(bx, barBottom - 18, bx + bw, barBottom);
            dc.FillSolidRect(nrc, RGB(12, 12, 12));
            CFont *pOld = (CFont *)dc.SelectObject(&m_fontVal);
            dc.SetTextColor(RGB(220, 220, 220));
            CString s;
            if (bars[i].valid)
                s.Format(_T("%.1f"), bars[i].value + offset);
            else
                s = _T("-Inf");
            dc.TextOut(nrc.left + 2, nrc.top + 2, s);
            dc.SelectObject(pOld);
        }
    }

    /* 各列（表）之间分隔线 */
    for (int i = 1; i < 5; i++)
    {
        int sx = x0 + i * (bw + gap) - gap / 2;
        dc.FillSolidRect(CRect(sx, barTop, sx + 1, barBottom), RGB(90, 90, 90));
    }

    /* 响度参考线：一条连续红线覆盖 M/S/I（不延伸 LRA），参考值在红线左端空白处 */
    {
        int refY2 = barBottom - (int)((70.0 + refLufs) / 70.0 * (barBottom - barTop));
        CString rs;
        rs.Format(_T("%d %s"), (int)refLufs, unit);
        CFont *pOldR = (CFont *)dc.SelectObject(&m_fontVal);
        dc.SetTextColor(RGB(255, 120, 120));
        CSize tsR = dc.GetTextExtent(rs);
        int tx = x0 - tsR.cx - 6;                 /* 文字右缘距红线左端 6px */
        if (tx < rrc.left + 4) tx = rrc.left + 4; /* 窗口窄时钳制 */
        dc.TextOut(tx, refY2 - 12, rs);
        dc.SelectObject(pOldR);
        /* 红线从文字右侧开始，延伸到 M/S/I 三列右端 */
        int lineX = x0;
        int textRight = tx + tsR.cx + 4;
        if (textRight > lineX) lineX = textRight;
        int refX2 = x0 + bw * 3 + gap * 2;        /* M/S/I 三列右端 */
        dc.FillSolidRect(CRect(lineX, refY2, refX2, refY2 + 2), RGB(255, 80, 80));
    }

    /* True Peak 参考线：红线覆盖 TP 列 limit 位置，参考值在右端空白区 */
    {
        double limDb = g_loudnessStds[std].tpLimit;
        int tpY = barBottom - (int)((limDb + 60.0) / 72.0 * (barBottom - barTop));
        CString ts;
        ts.Format(_T("%.1f dBTP"), limDb);
        CFont *pOldT = (CFont *)dc.SelectObject(&m_fontVal);
        dc.SetTextColor(RGB(255, 120, 120));
        CSize tsT = dc.GetTextExtent(ts);
        int tpX1 = x0 + bw * 4 + gap * 3;            /* True Peak 列左缘 */
        int tpX2 = x0 + bw * 5 + gap * 4;            /* True Peak 列右缘 */
        dc.FillSolidRect(CRect(tpX1, tpY, tpX2, tpY + 2), RGB(255, 80, 80));
        /* 参考值文字放在 TP 列右端空白区 */
        int tx2 = tpX2 + 6;
        if (tx2 + tsT.cx > rrc.right - 2)
            tx2 = rrc.right - 2 - tsT.cx;
        dc.TextOut(tx2, tpY - 12, ts);
        dc.SelectObject(pOldT);
    }

    dc.SelectObject(pOld);
}

/* 在给定栏内横排竖条（每通道一个垂直小条，居中；每条下方独立数值框）
   刻度：-60 ~ +12 dB（0dB 以上全红） */
void CLevelMeterDlg::DrawChannelRow(CDC &dc, const CRect &rc, int nCh,
                                    const volatile float *lvl,
                                    const volatile float *hold)
{
    if (nCh <= 0)
        return;
    const int valH = 16;                    /* 每条下方数值框高 */
    int barBottom = rc.bottom - valH;
    if (barBottom < rc.top + 4) barBottom = rc.top + 4;
    int bw = rc.Width() / nCh;
    if (bw > 22) bw = 22;
    if (bw < 6) bw = 6;
    int x = rc.left + (rc.Width() - bw * nCh) / 2;

    /* dB 刻度：底 -60 dB，顶 +12 dB；红区 0~+12、黄 -9~0、绿 -60~-9 */
    const double dBMin = -60.0, dBMax = 12.0;
    const double span = dBMax - dBMin;
    const double redF = (0.0 - dBMin) / span;    /* 0dB 位置 */
    const double yelF = (-9.0 - dBMin) / span;   /* -9dB 位置 */

    for (int c = 0; c < nCh; c++)
    {
        CRect cr(x, rc.top, x + bw - 2, barBottom);
        dc.FillSolidRect(cr, RGB(30, 30, 30));
        float l = lvl ? lvl[c] : 0.f;
        if (l < 0.f) l = 0.f;
        double db = (l > 0.000001f) ? 20.0 * log10((double)l) : dBMin;
        if (db < dBMin) db = dBMin;
        if (db > dBMax) db = dBMax;
        int h = (int)(cr.Height() * (db - dBMin) / span);
        if (h > 0)
        {
            int top = cr.bottom - h;
            int redY = cr.bottom - (int)(cr.Height() * redF);
            int yelY = cr.bottom - (int)(cr.Height() * yelF);
            if (top < redY) dc.FillSolidRect(CRect(cr.left, top, cr.right, redY), RGB(240, 60, 50));
            int y2 = (top > redY) ? top : redY;
            if (y2 < yelY) dc.FillSolidRect(CRect(cr.left, y2, cr.right, yelY), RGB(240, 200, 40));
            int g2 = (top > yelY) ? top : yelY;
            if (g2 < cr.bottom) dc.FillSolidRect(CRect(cr.left, g2, cr.right, cr.bottom), RGB(60, 200, 80));
        }
        /* 峰值保持线（dB 映射） */
        float hd = hold ? hold[c] : 0.f;
        if (hd > 0.001f)
        {
            double dbh = (hd > 0.000001f) ? 20.0 * log10((double)hd) : dBMin;
            if (dbh < dBMin) dbh = dBMin;
            if (dbh > dBMax) dbh = dBMax;
            int hy = cr.bottom - (int)(cr.Height() * (dbh - dBMin) / span);
            if (hy < cr.top) hy = cr.top;
            dc.FillSolidRect(CRect(cr.left, hy, cr.right, hy + 2), RGB(255, 255, 255));
        }
        /* 独立数值框：该通道当前电平 dB（无信号显示 -Inf） */
        CRect vrc(x, barBottom, x + bw - 2, rc.bottom);
        dc.FillSolidRect(vrc, RGB(12, 12, 12));
        CFont *pOld = (CFont *)dc.SelectObject(&m_fontVal);
        dc.SetTextColor(RGB(170, 200, 170));
        CString s;
        if (l <= 0.0001f)
            s = _T("-Inf");
        else if (db <= -60.0)
            s = _T("-60");
        else if (db >= 0.0)
            s.Format(_T("+%.0f"), db);
        else
            s.Format(_T("%.0f"), db);
        dc.TextOut(vrc.left + 2, vrc.top + 2, s);
        dc.SelectObject(pOld);
        x += bw;
    }
}
