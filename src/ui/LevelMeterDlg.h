// LevelMeterDlg.h : 独立电平表窗口（无模式自绘对话框）
//   显示输入/输出峰值 + ITU-R BS.1770 / EBU R128 / ATSC A/85 响度
//   （Momentary / Short-term / Integrated / LRA），标准与静音重置可切换
/******************************************************************************/
#pragma once

#include <afxwin.h>
#include <vector>
#include "../dsp/LoudnessCore.h"

class CMainFrame;

class CLevelMeterDlg : public CDialog
{
public:
    CLevelMeterDlg(CMainFrame *owner);
    BOOL Create(CWnd *pParent);

    CFont m_fontUI;
    CFont m_fontVal;      // 数值框小字体
    static BOOL CALLBACK SetChildFont(HWND hWnd, LPARAM lParam);

protected:
    BOOL OnInitDialog() override;
    void OnOK() override;          // 回车不关闭
    void OnCancel() override;      // ESC 关闭
    void OnClose();
    void PostNcDestroy() override;
    void OnTimer(UINT_PTR nIDEvent);
    void OnSize(UINT nType, int cx, int cy);
    void OnPaint();
    void OnStdChanged();
    void OnResetIntegrated();
    void OnLoudSrcClicked();

    void DrawMeters(CDC &dc);
    void DrawChannelRow(CDC &dc, const CRect &rc, int nCh,
                        const volatile float *lvl,
                        const volatile float *hold);

    // 控件创建
    void AddRadio(int id, LPCWSTR text, int x, int y, int w, int h, bool group);

protected:
    CMainFrame *m_pOwner;
    std::vector<BYTE> m_tmpl;
    int m_pxX, m_pxY;              // DLU -> 像素
    CRect m_drawRc;                // 自绘区（OnPaint 内重算）
    bool  m_bLoudInput;            // 响度源：false=输出（默认） true=输入

    void AddCombo(int id, int x, int y, int w, int h);
    void AddButton(int id, LPCWSTR text, int x, int y, int w, int h);
    void AddStatic(int id, LPCWSTR text, int x, int y, int w, int h);

    DECLARE_MESSAGE_MAP()
};
