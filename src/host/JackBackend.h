// JackBackend.h : JACK2 音频后端（动态链接 libjack64/libjack，计划书 §5.6）
//   端口数量/类型由插件实际能力决定：
//     音频 in_1..N / out_1..M（N、M = 插件输入/输出通道数，不写死 2）
//     midi_in / midi_out（按插件 WantMidiInput / WantMidiOutput 注册）
//   process 回调由 JACK 实时线程直接驱动（不再另起工作线程）。
//   libjack64.dll 缺失时给出“请安装/启动 JACK2”提示（动态加载，非硬链接）。
/******************************************************************************/
#pragma once

#include "IAudioBackend.h"
#include "IPlugin.h"

#include <windows.h>
#include <string>

#include <jack/jack.h>
#include <jack/midiport.h>

#define JACKBACKEND_MAX_CH    32     // 每侧最多音频端口数
#define JACKBACKEND_MAX_MIDI  512    // 每块最多收集的 MIDI 输入事件数

class CJackBackend : public IAudioBackend
{
public:
    CJackBackend();
    virtual ~CJackBackend();

    bool   Open(const char *clientName, double wantRate, int wantBufSize,
                int wantIn, int wantOut, void *sysRef) override;
    void   Close() override;
    bool   Start() override;
    void   Stop() override;

    double GetSampleRate() const override { return m_sampleRate; }
    int    GetBufferSize() const override { return (int)m_bufSize; }
    int    GetInputChannels() const override { return (int)m_inCh; }
    int    GetOutputChannels() const override { return (int)m_outCh; }
    const char *GetDriverName() const override { return m_clientName.c_str(); }

    bool   IsOpen() const { return m_client != NULL; }
    bool   IsRunning() const { return m_bRunning; }
    // DSP 使用率（%）：最近一块处理耗时 / 块周期；未运行返回 0
    double GetDspUsage() const;

    void SetProcessCallback(IAudioBackend::ProcessCB cb, void *ctx) override
    { m_cb = cb; m_ctx = ctx; }

    // MIDI 端口是否注册（Open 前设置，随插件能力；VST2 receiveVstMidiEvent / VST3 事件总线）
    void SetMidiPorts(bool in, bool out) { m_midiIn = in; m_midiOut = out; }
    bool HasMidiIn() const { return m_midiIn; }

    // 通知窗口（buffer size / shutdown 事件发给它，UI 线程处理）
    void SetNotifyWindow(HWND h) { m_hNotify = h; }

    // process 回调线程内：把插件产生的 MIDI 输出事件写入 midi_out 端口
    // （须在 JACK process 回调中调用；midi_out 缓冲已 clear，可重复取缓冲）
    void WriteMidiOut(const IPlugin::PluginMidiEvent *ev, int n);

    // 静态：lib 是否可加载 / JACK 服务器是否在运行
    static bool IsAvailable();
    static bool ServerAvailable();

    // 实时回调内：本帧收到的 MIDI 输入事件
    // （同一 JACK 回调内由后端写入、随后被处理回调消费，单线程无需锁）
    void ClearMidiIn() { m_midiInCount = 0; }
    int  GetMidiInCount() const { return m_midiInCount; }
    void GetMidiIn(int i, unsigned char *data, int &len) const;

    // 发给通知窗口的消息
    static const UINT WM_JACK_SHUTDOWN = WM_APP + 0x200;  // 服务器退出
    static const UINT WM_JACK_BUFSIZE  = WM_APP + 0x201;  // 块大小变化

private:
    static int  JackProcessCB(jack_nframes_t nframes, void *arg);
    static void JackShutdownCB(void *arg);
    static int  JackBufferSizeCB(jack_nframes_t nframes, void *arg);

    bool LoadApi();      // LoadLibrary + GetProcAddress
    void UnloadApi();

private:
    HMODULE m_hLib;
    jack_client_t *m_client;
    bool m_bRunning;
    double m_sampleRate;
    jack_nframes_t m_bufSize;
    jack_nframes_t m_notifiedBufsize;   // 上次已通知的块大小（防重复通知）
    int  m_inCh, m_outCh;          // 插件通道数（= 端口数）
    volatile long m_dspElapsedUs;  // 最近一块处理耗时（微秒，实时线程写/UI 读）
    bool m_midiIn, m_midiOut;      // 是否注册 MIDI 端口
    std::string m_clientName;      // 实际客户端名（UTF-8/ANSI）
    HWND m_hNotify;

    jack_port_t *m_inPorts[JACKBACKEND_MAX_CH];
    jack_port_t *m_outPorts[JACKBACKEND_MAX_CH];
    jack_port_t *m_midiInPort, *m_midiOutPort;

    IAudioBackend::ProcessCB m_cb;
    void     *m_ctx;
    // 实时回调内收集的 MIDI 输入
    unsigned char m_midiInData[JACKBACKEND_MAX_MIDI][3];
    int  m_midiInLen[JACKBACKEND_MAX_MIDI];
    int  m_midiInCount;

    // ---- 动态加载的 JACK API（LoadLibrary + GetProcAddress） ----
#define JACK_MEMBER(ret, name, args) ret (__cdecl *name) args;
    JACK_MEMBER(jack_client_t *, jack_client_open, (const char *, jack_options_t, jack_status_t *, ...))
    JACK_MEMBER(int, jack_client_close, (jack_client_t *))
    JACK_MEMBER(char *, jack_get_client_name, (jack_client_t *))
    JACK_MEMBER(int, jack_activate, (jack_client_t *))
    JACK_MEMBER(int, jack_deactivate, (jack_client_t *))
    JACK_MEMBER(int, jack_set_process_callback, (jack_client_t *, JackProcessCallback, void *))
    JACK_MEMBER(int, jack_set_buffer_size_callback, (jack_client_t *, JackBufferSizeCallback, void *))
    JACK_MEMBER(void, jack_on_shutdown, (jack_client_t *, JackShutdownCallback, void *))
    JACK_MEMBER(jack_nframes_t, jack_get_sample_rate, (jack_client_t *))
    JACK_MEMBER(jack_nframes_t, jack_get_buffer_size, (jack_client_t *))
    JACK_MEMBER(jack_port_t *, jack_port_register, (jack_client_t *, const char *, const char *, unsigned long, unsigned long))
    JACK_MEMBER(int, jack_port_unregister, (jack_client_t *, jack_port_t *))
    JACK_MEMBER(void *, jack_port_get_buffer, (jack_port_t *, jack_nframes_t))
    JACK_MEMBER(const char *, jack_port_name, (const jack_port_t *))
    JACK_MEMBER(int, jack_connect, (jack_client_t *, const char *, const char *))
    JACK_MEMBER(const char **, jack_get_ports, (jack_client_t *, const char *, const char *, unsigned long))
    JACK_MEMBER(void, jack_free, (void *))
    JACK_MEMBER(uint32_t, jack_midi_get_event_count, (void *))
    JACK_MEMBER(int, jack_midi_event_get, (jack_midi_event_t *, void *, uint32_t))
    JACK_MEMBER(void, jack_midi_clear_buffer, (void *))
    JACK_MEMBER(int, jack_midi_event_write, (void *, jack_nframes_t, const jack_midi_data_t *, size_t))
#undef JACK_MEMBER
};
