// AsioMapDialog.h : ASIO 通道分配对话框（M4.1，计划书 §5.5）
// 手动指定 插件通道 <-> ASIO 通道 映射，动态构建对话框模板（无 .rc）
/******************************************************************************/
#pragma once

#include <afxwin.h>
#include <vector>
#include <string>

// 对话框命令 ID
#define IDM_ASIO_RESET    0x7E21

// ASIO 通道分配对话框：
//   每个插件输入/输出通道一行 ComboBox，列出该方向的 ASIO 通道（+ 静音）。
//   结果写回 mapIn/mapOut（-1 = 静音）。
class CAsioMapDialog : public CDialog
{
public:
    CAsioMapDialog(const std::vector<std::string> &asioIn,
                   const std::vector<std::string> &asioOut,
                   int plugIn, int plugOut,
                   std::vector<int> &mapIn, std::vector<int> &mapOut);

    // 诊断：暴露动态模板
    const BYTE *Tmpl() const { return m_tmpl.data(); }
    size_t      TmplSize() const { return m_tmpl.size(); }

    // 系统 UI 字体（Segoe UI），应用到对话框与全部子控件
    CFont m_fontDlg;
    static BOOL CALLBACK SetChildFont(HWND hWnd, LPARAM lParam);

protected:
    BOOL OnInitDialog() override;
    void OnOK() override;
    void OnReset();

protected:
    std::vector<BYTE> m_tmpl;                 // 动态对话框模板（仅头部）
    std::vector<std::string> m_asioIn, m_asioOut;
    int  m_plugIn, m_plugOut;
    std::vector<int> &m_mapIn, &m_mapOut;

    // 代码创建控件（OnInitDialog 中调用，避免手写 DLGITEMTEMPLATE）
    void AddStatic(int id, LPCWSTR text, int x, int y, int w, int h);
    void AddCombo(int id, int x, int y, int w, int h);
    void AddButton(int id, LPCWSTR text, int x, int y, int w, int h, DWORD extra);

    void FillCombos();
    void ApplyMap();          // 用 mapIn/mapOut 刷新各 ComboBox 选择
    void ReadBack();          // 从 ComboBox 读回 mapIn/mapOut

    DECLARE_MESSAGE_MAP()
};
