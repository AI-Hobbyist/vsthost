// MidiMapDialog.h : MIDI 参数映射设置（模态对话框）
//   把 MIDI 控制器的 CC 消息映射到插件参数：<通道, CC> -> 插件参数索引
//   配置存 ini [MidiMap]，ASIO 回调收到对应 CC 时设置插件参数
/******************************************************************************/
#pragma once

#include <afxwin.h>
#include <vector>
#include <string>

class CMainFrame;

class CMidiMapDialog : public CDialog
{
public:
    CMidiMapDialog(CMainFrame *owner, const std::vector<std::string> &paramNames);

    CFont m_fontDlg;
    static BOOL CALLBACK SetChildFont(HWND hWnd, LPARAM lParam);

protected:
    BOOL OnInitDialog() override;
    void OnOK() override;
    void OnCancel() override;
    void OnAdd();
    void OnDel();

protected:
    CMainFrame *m_pOwner;
    std::vector<std::string> m_paramNames;
    std::vector<BYTE> m_tmpl;

    struct Entry { int ch; int cc; int param; };
    std::vector<Entry> m_entries;

    void RefreshList();
    void AddStatic(int id, LPCWSTR text, int x, int y, int w, int h);
    void AddCombo(int id, int x, int y, int w, int h);
    void AddList(int id, int x, int y, int w, int h);
    void AddButton(int id, LPCWSTR text, int x, int y, int w, int h, DWORD extra);

    enum
    {
        IDC_MM_LIST   = 0xF701,
        IDC_MM_CH     = 0xF702,
        IDC_MM_CC     = 0xF703,
        IDC_MM_PARAM  = 0xF704,
        IDC_MM_ADD    = 0xF705,
        IDC_MM_DEL    = 0xF706,
        IDC_MM_OK     = IDOK,
        IDC_MM_CANCEL = IDCANCEL
    };

    DECLARE_MESSAGE_MAP()
};
