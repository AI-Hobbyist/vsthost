// ClosePromptDlg.h : 关闭时询问对话框（最小化到托盘 / 完全关闭 + 不再提示）
//   点“最小化到托盘”或“完全关闭”返回结果；勾选“不再提示”则记住本次选择
/******************************************************************************/
#pragma once

#include <afxwin.h>
#include <vector>

class CMainFrame;

class CClosePromptDlg : public CDialog
{
public:
    CClosePromptDlg(CMainFrame *owner);

    int  m_result;      // 0=取消 1=最小化到托盘 2=完全关闭
    bool m_dontAsk;     // 不再提示（记住本次选择）

    CFont m_fontDlg;
    static BOOL CALLBACK SetChildFont(HWND hWnd, LPARAM lParam);

protected:
    BOOL OnInitDialog() override;
    void OnTray();
    void OnExit();

protected:
    CMainFrame *m_pOwner;
    std::vector<BYTE> m_tmpl;

    enum
    {
        IDC_CP_TRAY    = 0xF301,   // 最小化到托盘
        IDC_CP_EXIT    = 0xF302,   // 完全关闭
        IDC_CP_DONTASK = 0xF303    // 不再提示
    };

    DECLARE_MESSAGE_MAP()
};
