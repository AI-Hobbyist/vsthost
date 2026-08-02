// MeterSettingsDlg.h : 电平表设置窗口（模态对话框）
//   数值项（刷新频率/峰值保持时长/静音重置/静音阈值）用滑块 + 输入框，
//   可拖动滑块或直接输入数值；其余为开关与标准下拉
/******************************************************************************/
#pragma once

#include <afxwin.h>
#include <vector>

class CMainFrame;

class CMeterSettingsDlg : public CDialog
{
public:
    CMeterSettingsDlg(CMainFrame *owner);

    CFont m_fontDlg;
    static BOOL CALLBACK SetChildFont(HWND hWnd, LPARAM lParam);

protected:
    BOOL OnInitDialog() override;
    void OnOK() override;
    void OnCancel() override;
    void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar);
    void OnEditKillFocus(UINT nID);
    void OnBrowseCsvFolder();

    void AddCheck(int id, LPCWSTR text, int x, int y, int w, int h);
    void AddCombo(int id, int x, int y, int w, int h);
    void AddStatic(int id, LPCWSTR text, int x, int y, int w, int h);
    void AddButton(int id, LPCWSTR text, int x, int y, int w, int h, DWORD extra);
    void AddSlider(int id, int x, int y, int w, int h);
    void AddEdit(int id, int x, int y, int w, int h);

    // 滑块 <-> 输入框 同步（mode 见枚举）
    void SyncSliderToEdit(int sliderId, int editId, int mode);
    void SyncEditToSlider(int editId, int sliderId, int mode);
    static bool ParseVal(const CString &s, int mode, int &sliderPos);

protected:
    CMainFrame *m_pOwner;
    std::vector<BYTE> m_tmpl;
    int m_pxX, m_pxY;           // DLU -> 像素

    enum
    {
        // 值类型（滑块位置映射）
        MODE_REFRESH = 0,       // 30~200 ms 步进10
        MODE_PEAK,              // 位置0~50 (×0.1s) 步进5
        MODE_SILENCE,           // 0~60 s 步进1
        MODE_THRESH,            // 位置0~50 -> -90~-40 LUFS
        MODE_CSV,               // 1~60 s 步进1（CSV 记录间隔）
        MODE_CH,                // 1~255 通道数 步进1（每侧显示上限）

        IDC_MS_SHOW      = 0xF401,
        IDC_MS_PEAKLINE  = 0xF402,
        IDC_MS_VALUEBOX  = 0xF403,
        IDC_MS_STD       = 0xF404,
        IDC_MS_CSVLOG    = 0xF405,
        IDC_MS_CSVFOLDER = 0xF406,   // 路径编辑框
        IDC_MS_CSV_BROWSE = 0xF407,  // 浏览按钮

        IDC_MS_ED_REFRESH = 0xF501,
        IDC_MS_ED_PEAK    = 0xF502,
        IDC_MS_ED_SIL     = 0xF503,
        IDC_MS_ED_THRESH  = 0xF504,
        IDC_MS_ED_CSV     = 0xF505,
        IDC_MS_ED_CH      = 0xF506,

        IDC_MS_SL_REFRESH = 0xF511,
        IDC_MS_SL_PEAK    = 0xF512,
        IDC_MS_SL_SIL     = 0xF513,
        IDC_MS_SL_THRESH  = 0xF514,
        IDC_MS_SL_CSV     = 0xF515,
        IDC_MS_SL_CH      = 0xF516,

        IDC_MS_OK         = IDOK,
        IDC_MS_CANCEL     = IDCANCEL
    };

    DECLARE_MESSAGE_MAP()
};
