// GlobalSettingsDlg.h : 全局设置窗口（模态对话框）
//   关闭行为（每次询问/最小化到托盘/完全关闭）+ 音频/MIDI 设置
//   （采样率/缓冲/MIDI 启用/设备/通道，原 ASIO 设置窗口内容并入此处）
/******************************************************************************/
#pragma once

#include <afxwin.h>
#include <vector>

class CMainFrame;

class CGlobalSettingsDlg : public CDialog
{
public:
    CGlobalSettingsDlg(CMainFrame *owner);

    CFont m_fontDlg;
    static BOOL CALLBACK SetChildFont(HWND hWnd, LPARAM lParam);

protected:
    BOOL OnInitDialog() override;
    void OnOK() override;
    void OnCancel() override;

protected:
    CMainFrame *m_pOwner;
    std::vector<BYTE> m_tmpl;

    void AddStatic(int id, LPCWSTR text, int x, int y, int w, int h);
    void AddCombo(int id, int x, int y, int w, int h, bool editable);
    void AddCheck(int id, LPCWSTR text, int x, int y, int w, int h);
    void AddRadio(int id, LPCWSTR text, int x, int y, int w, int h, bool first);
    void AddButton(int id, LPCWSTR text, int x, int y, int w, int h, DWORD extra);

    enum
    {
        IDC_GS_ASK     = 0xF201,   // 关闭时每次询问
        IDC_GS_TRAY    = 0xF202,   // 最小化到托盘
        IDC_GS_EXIT    = 0xF203,   // 完全关闭

        IDC_GS_RATE    = 0xF204,   // 采样率 Combo（可编辑）
        IDC_GS_BUFFER  = 0xF205,   // 缓冲 Combo（可编辑）

        IDC_GS_MIDION  = 0xF206,   // MIDI 输入开关
        IDC_GS_MIDIDEV = 0xF207,   // MIDI 设备 Combo
        IDC_GS_MIDICH  = 0xF208,   // MIDI 通道 Combo（0=Omni 1~16）
        IDC_GS_MIDIOUT    = 0xF209,   // MIDI 输出开关
        IDC_GS_MIDIOUTDEV = 0xF20A,   // MIDI 输出设备 Combo

        IDC_GS_OK      = IDOK,
        IDC_GS_CANCEL  = IDCANCEL
    };

    DECLARE_MESSAGE_MAP()
};
