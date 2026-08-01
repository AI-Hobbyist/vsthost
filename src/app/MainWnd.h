// MainWnd.h : 主窗口（SDI 单文档 + 插件宿主）
/******************************************************************************/
#pragma once

#include <afxwin.h>
#include <afxext.h>
#include <afxcmn.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include "../dsp/LoudnessCore.h"
#include "../host/MidiInput.h"
#include "../host/MidiOutput.h"

class CSingleHost;
class IPlugin;
class CAsioBackend;
class CJackBackend;
class IAudioBackend;
class CLevelMeterDlg;

// MIDI 参数映射：<MIDI 通道, CC 号> -> 插件参数索引
struct MidiMapEntry
{
    int ch;    // MIDI 通道 1~16
    int cc;    // CC 号 0~127
    int param; // 插件参数索引（-1 无效）
};

// 命令 ID
#define IDM_FILE_OPEN       0x9001
#define IDM_FILE_EXIT       0x9002
#define IDM_APP_ABOUT       0x9003
#define IDM_FILE_CLOSE      0x9004
#define IDM_PLUGIN_TESTPROC 0x9005
#define IDM_FILE_SAVE_EXE   0x9006
#define IDM_PLUGIN_SINETEST 0x9007   // 测试信号（1kHz 正弦，toggle）
#define IDM_PLUGIN_MIDIMAP  0x9008   // MIDI 参数映射设置
#define IDM_FILE_SETTINGS   0x9009   // 全局设置
#define IDM_INTERNAL_BASE   0x9100
#define IDM_INTERNAL_MAX    0x91FF
// 音频（ASIO）
#define IDM_AUDIO_BASE      0x9200   // ASIO 设备列表（勾选当前）
#define IDM_AUDIO_MAX       0x92FF
#define IDM_AUDIO_CPANEL    0x9301   // 驱动控制面板
#define IDM_AUDIO_REFRESH   0x9302   // 刷新设备列表
#define IDM_AUDIO_MAP       0x9303   // ASIO 通道分配
#define IDM_AUDIO_BACKEND_ASIO 0x9305 // 音频后端：ASIO
#define IDM_AUDIO_BACKEND_JACK 0x9306 // 音频后端：JACK
#define IDM_VIEW_METERS     0x9401   // 显示电平表（勾选）
#define IDM_APP_ABOUT_PLUGIN 0x9402  // 关于插件（详细音频/插件信息）
#define IDM_VIEW_PEAK_BASE  0x9403   // 峰值保持时长（0=关 / 0.5 / 1 / 2 / 5 秒）
#define IDM_VIEW_PEAK_MAX   0x9407
#define IDM_VIEW_REFRESH_BASE 0x9408 // 电平表刷新频率（30/50/80/100 ms）
#define IDM_VIEW_REFRESH_MAX  0x940B
#define IDM_VIEW_METER_SETTINGS 0x940C // 电平表设置…
#define IDM_VIEW_METER_WINDOW   0x940D // 独立电平表窗口（toggle）

// 托盘图标
#define WM_TRAYICON   (WM_APP + 0x100)   // 托盘回调消息
#define IDM_TRAY_SHOW 0x9501             // 托盘菜单：显示窗口
#define IDM_TRAY_EXIT 0x9502             // 托盘菜单：退出
#define IDR_APP_ICON  128                // 应用图标资源（vsthost.rc IDR_MAINFRAME）

// 电平表（左右边缘垂直条，每通道条宽含间隙；高度随窗口高度自适应）
#define METER_CH_W    22          // 每通道条宽 + 间隙（含下方数值框宽度）
#define METER_MAX_CH  16          // 每侧最多显示的通道数（超出仅显示前 N）

// 插件编辑器宿主窗格：在此嵌入 VST2/VST3 编辑器
class CPluginHostView : public CWnd
{
public:
    CPluginHostView() {}
    virtual ~CPluginHostView() {}

    BOOL Create(CWnd *pParent, UINT nID);

protected:
    afx_msg void OnPaint();
    DECLARE_MESSAGE_MAP()
};

class CMainFrame : public CFrameWnd
{
public:
    CMainFrame();
    virtual ~CMainFrame();

    BOOL Create();
    void SetHost(CSingleHost *h) { m_pHost = h; }

    // 启动时同名自动加载（计划书 §5.1）
    void AutoLoad();
    // 手动加载
    void DoLoad(const std::wstring &path);

    CPluginHostView *GetPluginView() { return m_pPluginView; }

    // ---- 电平表 / 响度（独立窗口与设置窗口访问，public） ----
    LoudnessCore &LoudIn() { return m_loudIn; }
    LoudnessCore &LoudOut() { return m_loudOut; }
    int  MeterInCh() const;
    int  MeterOutCh() const;
    const volatile float *InLevel() const { return m_inLevel; }
    const volatile float *InHold()  const { return m_inHold; }
    const volatile float *OutLevel() const { return m_outLevel; }
    const volatile float *OutHold()  const { return m_outHold; }
    int    LoudnessStd() const { return m_loudnessStd; }
    double SilenceReset() const { return m_silenceReset; }
    double SilenceThresh() const { return m_silenceThresh; }
    int    MeterRefreshMs() const { return m_meterRefreshMs; }
    double PeakHoldSeconds() const { return m_peakHoldSeconds; }
    bool   ShowMeters() const { return m_bShowMeters; }
    bool   ShowPeakLine() const { return m_bPeakLine; }
    bool   ShowValueBox() const { return m_bValueBox; }
    void   SetLoudnessStd(int std);      // 存 ini + 应用
    void   SetSilenceReset(double sec);  // 存 ini + 应用到响度核心
    void   SetSilenceThresh(double lufs);// 静音阈值（LUFS，存 ini + 应用）
    void   ResetLoudness();              // 重置 I/O 响度累积
    void   OnMeterWindowClosed();        // 独立窗口销毁时回调
    void   ApplyMeterSettings(bool show, int refreshMs, double peakHold,
                              bool peakLine, bool valueBox, int std,
                              double silence, double silenceThresh);

    // ---- CSV 响度日志 ----
    bool CsvLog() const { return m_bCsvLog; }
    int  CsvIntervalMs() const { return m_csvIntervalMs; }
    const std::wstring &CsvFolder() const { return m_csvFolder; }
    void ApplyCsvSettings(bool log, int intervalMs, const std::wstring &folder);
    void SetCsvFolder(const std::wstring &f);   // 仅写 ini + 更新成员
    void OpenCsvFile();      // 按当前系统时间开新文件
    void CloseCsvFile();
    void WriteCsvRow();      // 定时采样写一行
    void ReopenCsvFile();    // 重置后开新文件

    // ---- 音频（M4 ASIO / M5 JACK） ----
    // 插件加载后启动音频（默认后端 = ASIO，见 §5.5 / §5.6）
    void StartAudio();
    // 插件关闭/切换前停止音频
    void StopAudio();
    // 后端模式：0=ASIO 1=JACK（ini [View] backend）
    int  BackendMode() const { return m_backendMode; }
    void ApplyBackendMode(int mode);   // 写 ini + 音频运行则重启
    // ASIO / JACK 实时处理回调（通道映射 + 插件处理 + 电平采集，共用）
    static void AudioProcessCB(void *ctx, float **in, float **out,
                               int frames, int inCh, int outCh);

    // ---- ASIO 采样率 / 缓冲 / MIDI 输入（设置对话框访问） ----
    double AsioSampleRate() const;   // ini 记忆或当前驱动实际值
    int    AsioBufferSize() const;
    bool   MidiInputEnabled() const;
    int    MidiDeviceIndex() const;
    int    MidiChannel() const;   // 0=Omni（全部）1~16
    bool   MidiOutputEnabled() const;   // MME MIDI 输出（ASIO 后端用）
    int    MidiOutputDeviceIndex() const;
    void   ApplyAsioSettings(double rate, int buf, bool midiOn, int midiDev,
                             int midiCh, bool midiOutOn, int midiOutDev);
    void   ApplyMidiChannel(int midiCh);   // 仅应用 MIDI 通道（JACK 模式用）

    // ---- 关闭行为（0=每次询问 1=最小化到托盘 2=完全关闭，ini [View] closeaction） ----
    int  CloseAction() const;
    void ApplyCloseAction(int action);

    // ---- MIDI 参数映射 ----
    void GetMidiMap(std::vector<MidiMapEntry> &out) const;
    void SetMidiMap(const std::vector<MidiMapEntry> &map);

protected:
    void OpenPluginEditor();
    void ClosePluginEditor();
    void RebuildInternalMenu();
    void UpdateStatus();
    void FitWindowToEditor(int w, int h);   // 主窗口随插件编辑器尺寸自适应

    // ---- 托盘图标 / 关闭流程 ----
    void AddTrayIcon();
    void RemoveTrayIcon();
    void MinimizeToTray();       // 隐藏窗口 + 托盘图标
    void RestoreFromTray();      // 托盘图标恢复窗口
    void RealClose();            // 真正退出（停音频 + 关窗口）
    void RestartHost();          // 保存状态并重启自身（切后端后彻底生效）

    // ---- 音频辅助 ----
    void StartAsioAudio();
    bool StartJackAudio();   // 返回 false 表示 JACK 不可用/启动失败（上层回退 ASIO）
    void RebuildAsioMenu();
    void UpdateAsioStatus();
    std::string PickDefaultDriver();            // 记忆驱动 / FL Studio ASIO / 第一个
    std::wstring AsioConfigPath() const;        // exe 同目录 <exe名>.ini
    void SaveAsioConfig(const char *driverName);
    void ApplyAudioToPlugin();                  // 用实际采样率/块大小重配置插件
    // 实时回调内把一条 MIDI 事件喂给插件（通道过滤 + CC 参数映射 + SendMidiIn）
    void ProcessMidiEvent(IPlugin *p, const unsigned char *mev, int mlen);
    // 通道映射（[AsioMap] 存 exe ini；-1 = 静音/丢弃）
    void LoadAsioMap(int plugIn, int plugOut, int asioIn, int asioOut);
    void SaveAsioMap() const;
    // MIDI 参数映射（[MidiMap] mapN=ch,cc,param）
    void LoadMidiMap();
    void SaveMidiMap() const;
    // 电平表
    void DrawMeters(CDC &dc);
    void ShowMeters(bool show);                 // 开关并重排布局
    int  MeterInWidth() const;                  // 左侧输入表宽（0 = 无输入通道）
    int  MeterOutWidth() const;                 // 右侧输出表宽（0 = 无输出通道）
    void ApplyPeakMenu();                       // 峰值保持菜单勾选（按当前配置）
    void ApplyRefreshMenu();                    // 刷新频率菜单勾选（按当前配置）
    void RefreshMeterTimer();                   // 重设定时器 2（电平表刷新周期）
    void InvalidateMeters();                    // 失效左右电平表条带区域（定时重绘）
    // 测试信号（1kHz 正弦注入插件输入，验证音频输入输出通路）
    void ToggleSineTest();

protected:
    CStatusBar        m_wndStatusBar;
    CPluginHostView  *m_pPluginView;
    CSingleHost      *m_pHost;
    HMENU             m_hInternalMenu;
    CFont             m_fontUI;       // 当前系统 UI 字体（全界面统一）
    CFont             m_fontMeter;    // 电平表数值框小字体

    CAsioBackend     *m_pAsio;        // ASIO 后端（懒创建）
    CJackBackend     *m_pJack;        // JACK 后端（懒创建）
    IAudioBackend    *m_pBackend;     // 当前活跃后端（指向 m_pAsio 或 m_pJack）
    int               m_backendMode;  // 0=ASIO 1=JACK（[View] backend）
    DWORD             m_lastJackBufsizeTick;  // JACK 块大小变化防抖时间戳
    HMENU             m_hAsioMenu;    // “音频->ASIO 设备”子菜单
    HMENU             m_hPeakMenu;    // “视图->峰值保持”子菜单
    HMENU             m_hRefreshMenu; // “视图->电平表刷新频率”子菜单
    std::vector<std::string> m_asioDrivers;   // 最近一次枚举
    std::string       m_asioCurrent;  // 当前驱动名

    // 通道映射：插件通道 -> ASIO 通道索引（-1 = 静音/丢弃）
    std::vector<int>  m_mapIn, m_mapOut;

    // 电平表
    bool              m_bShowMeters;
    volatile float    m_inLevel[32];   // 输入电平（峰值，ASIO 线程写/UI 读）
    volatile float    m_outLevel[32];

    // 峰值保持（ASIO 线程写/UI 读；保持时长 UI 写）
    double            m_peakHoldSeconds;   // 0 = 关闭
    int               m_meterRefreshMs;    // 电平表刷新周期（ms，默认 50）
    bool              m_bPeakLine;         // 峰值保持线显示
    bool              m_bValueBox;         // 底部数值框显示
    int               m_loudnessStd;       // 响度标准 0=BS.1770-4 1=EBU R128 2=ATSC
    double            m_silenceReset;      // Integrated 静音重置秒数（0=不重置）
    double            m_silenceThresh;     // 静音阈值（LUFS，默认 -70）
    CLevelMeterDlg   *m_pMeterDlg;         // 独立电平表窗口（无模式，NULL=未开）
    LoudnessCore      m_loudIn, m_loudOut; // 输入/输出响度核心（ASIO 线程写/UI 读）
    CMidiInput        m_midiInput;         // MIDI 输入（winmm，ASIO 回调消费）
    bool              m_midiEnabled;       // 是否启用 MIDI 输入
    int               m_midiDevice;        // MIDI 设备索引
    int               m_midiChannel;       // MIDI 通道过滤 0=Omni 1~16
    CMidiOutput       m_midiOut;           // MIDI 输出（winmm，ASIO 后端用）
    bool              m_midiOutEnabled;    // 是否启用 MIDI 输出
    int               m_midiOutDevice;     // MIDI 输出设备索引
    mutable CRITICAL_SECTION m_midiMapCs;  // 保护 m_midiMap（实时回调读/UI 写）
    std::vector<MidiMapEntry> m_midiMap;   // MIDI CC -> 插件参数映射

    // CSV 响度日志
    bool              m_bCsvLog;           // 开启
    int               m_csvIntervalMs;     // 记录间隔（ms）
    std::wstring      m_csvFolder;         // 输出文件夹
    FILE             *m_csvFile;           // 当前文件
    std::wstring      m_csvPath;           // 当前文件路径
    long              m_lastAutoResets;    // 上次自动重置次数（检测开新文件）

    // 托盘
    NOTIFYICONDATAW   m_nid;               // 托盘图标信息
    bool              m_bTrayVisible;      // 托盘图标是否显示
    volatile float    m_inHold[32];        // 输入峰值保持值
    volatile float    m_outHold[32];
    volatile long     m_inHoldFrames[32];  // 保持累计帧数（计时）
    volatile long     m_outHoldFrames[32];

    // 测试信号（1kHz 正弦）
    volatile bool     m_bSineTest;     // ASIO 线程读，UI 线程写
    volatile double   m_sinePhase;     // ASIO 线程推进

    // 实时回调缓冲（StartAudio 时分配，ASIO 线程只读 data()，不分配）
    std::vector<std::vector<float> > m_plugIn;    // 插件输入（ASIO 不足时零填充）
    std::vector<std::vector<float> > m_plugOut;   // 插件输出（ASIO 不足时丢弃）
    std::vector<float *> m_plugInPtrs, m_plugOutPtrs;

    afx_msg int  OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnDropFiles(HDROP hDropInfo);
    afx_msg void OnSettingChange(UINT uFlags, LPCTSTR lpszSection);
    afx_msg void OnInitMenuPopup(CMenu *pPopupMenu, UINT nIndex, BOOL bSysMenu);
    afx_msg void OnClose();
    afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
    afx_msg LRESULT OnTrayIcon(WPARAM wParam, LPARAM lParam);
    afx_msg void OnFileOpen();
    afx_msg void OnFileClose();
    afx_msg void OnFileSaveExe();
    afx_msg void OnFileExit();
    afx_msg void OnFileSettings();
    afx_msg void OnTrayShow();
    afx_msg void OnTrayExit();
    afx_msg void OnAppAbout();
    afx_msg void OnAppAboutPlugin();
    afx_msg void OnPluginTestProc();
    afx_msg void OnPluginSineTest();
    afx_msg void OnPluginMidiMap();
    afx_msg void OnInternalSelect(UINT nID);
    afx_msg void OnAsioSelect(UINT nID);
    afx_msg void OnAsioControlPanel();
    afx_msg void OnAsioRefresh();
    afx_msg void OnAsioMap();
    afx_msg void OnBackendSelect(UINT nID);
    afx_msg LRESULT OnJackShutdown(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnJackBufsize(WPARAM wParam, LPARAM lParam);
    afx_msg void OnViewMeters();
    afx_msg void OnPeakSelect(UINT nID);
    afx_msg void OnRefreshSelect(UINT nID);
    afx_msg void OnMeterSettings();
    afx_msg void OnMeterWindow();
    afx_msg void OnPaint();
    DECLARE_MESSAGE_MAP()
};
