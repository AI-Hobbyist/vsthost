// JackBackend.cpp : JACK2 音频后端实现（动态链接 libjack64/libjack）
/******************************************************************************/
#include "JackBackend.h"

#include <cstdio>
#include <cstring>

// x64 用 libjack64.dll，x86 用 libjack.dll（JACK2 标准命名）
#ifdef _WIN64
static const wchar_t *kJackDll = L"libjack64.dll";
#else
static const wchar_t *kJackDll = L"libjack.dll";
#endif

CJackBackend::CJackBackend()
    : m_hLib(NULL), m_client(NULL), m_bRunning(false),
      m_sampleRate(44100.0), m_bufSize(512), m_notifiedBufsize(0),
      m_inCh(0), m_outCh(0),
      m_midiIn(false), m_midiOut(false), m_hNotify(NULL),
      m_midiInPort(NULL), m_midiOutPort(NULL),
      m_cb(NULL), m_ctx(NULL), m_midiInCount(0), m_dspElapsedUs(0)
{
    memset(m_inPorts, 0, sizeof(m_inPorts));
    memset(m_outPorts, 0, sizeof(m_outPorts));
}

CJackBackend::~CJackBackend()
{
    Close();
    UnloadApi();
}

/*****************************************************************************/
/* LoadApi : LoadLibrary + GetProcAddress 加载全部 JACK 函数                  */
/*****************************************************************************/
bool CJackBackend::LoadApi()
{
    if (m_hLib)
        return true;

    m_hLib = ::LoadLibraryW(kJackDll);
    if (!m_hLib)
        return false;

    bool ok = true;
#define JACK_LOAD(name) \
    name = reinterpret_cast<decltype(name)>(::GetProcAddress(m_hLib, #name)); \
    if (!name) ok = false;
    JACK_LOAD(jack_client_open)
    JACK_LOAD(jack_client_close)
    JACK_LOAD(jack_get_client_name)
    JACK_LOAD(jack_activate)
    JACK_LOAD(jack_deactivate)
    JACK_LOAD(jack_set_process_callback)
    JACK_LOAD(jack_set_buffer_size_callback)
    JACK_LOAD(jack_on_shutdown)
    JACK_LOAD(jack_get_sample_rate)
    JACK_LOAD(jack_get_buffer_size)
    JACK_LOAD(jack_port_register)
    JACK_LOAD(jack_port_unregister)
    JACK_LOAD(jack_port_get_buffer)
    JACK_LOAD(jack_port_name)
    JACK_LOAD(jack_connect)
    JACK_LOAD(jack_get_ports)
    JACK_LOAD(jack_free)
    JACK_LOAD(jack_midi_get_event_count)
    JACK_LOAD(jack_midi_event_get)
    JACK_LOAD(jack_midi_clear_buffer)
    JACK_LOAD(jack_midi_event_write)
#undef JACK_LOAD

    if (!ok)
    {
        UnloadApi();
        return false;
    }
    return true;
}

void CJackBackend::UnloadApi()
{
    if (m_hLib)
    {
        ::FreeLibrary(m_hLib);
        m_hLib = NULL;
    }
}

/*****************************************************************************/
/* 静态：lib 是否可加载 / JACK 服务器是否在运行                               */
/*****************************************************************************/
bool CJackBackend::IsAvailable()
{
    HMODULE h = ::LoadLibraryW(kJackDll);
    if (h)
    {
        ::FreeLibrary(h);
        return true;
    }
    return false;
}

bool CJackBackend::ServerAvailable()
{
    CJackBackend tmp;
    if (!tmp.LoadApi())
        return false;
    jack_status_t status = (jack_status_t)0;
    jack_client_t *c = tmp.jack_client_open("vsthost_probe",
                                            (jack_options_t)(JackNullOption | JackNoStartServer),
                                            &status);
    if (!c)
        return false;
    tmp.jack_client_close(c);
    return true;
}

/*****************************************************************************/
/* Open : 打开 JACK client + 按插件能力注册端口（不启动）                      */
/*****************************************************************************/
bool CJackBackend::Open(const char *clientName, double wantRate, int wantBufSize,
                        int wantIn, int wantOut, void * /*sysRef*/)
{
    if (m_client)
        Close();
    if (!m_hLib && !LoadApi())
        return false;

    jack_status_t status = (jack_status_t)0;
    m_client = jack_client_open(clientName,
                                (jack_options_t)(JackNullOption | JackNoStartServer),
                                &status);
    if (!m_client)
        return false;

    if (jack_get_client_name)
    {
        const char *n = jack_get_client_name(m_client);
        if (n)
            m_clientName = n;
    }
    if (m_clientName.empty())
        m_clientName = clientName;

    m_sampleRate = jack_get_sample_rate ? (double)jack_get_sample_rate(m_client)
                                        : (wantRate > 0 ? wantRate : 44100.0);
    m_bufSize    = jack_get_buffer_size ? jack_get_buffer_size(m_client)
                                        : (jack_nframes_t)(wantBufSize > 0 ? wantBufSize : 512);

    m_inCh  = (wantIn  > 0 && wantIn  <= JACKBACKEND_MAX_CH) ? wantIn  : 0;
    m_outCh = (wantOut > 0 && wantOut <= JACKBACKEND_MAX_CH) ? wantOut : 0;

    /* 音频端口（数量 = 插件通道数） */
    char nm[64];
    for (int i = 0; i < m_inCh; i++)
    {
        sprintf(nm, "in_%d", i + 1);
        m_inPorts[i] = jack_port_register(m_client, nm, JACK_DEFAULT_AUDIO_TYPE,
                                          JackPortIsInput, 0);
        if (!m_inPorts[i])
        {
            Close();
            return false;
        }
    }
    for (int i = 0; i < m_outCh; i++)
    {
        sprintf(nm, "out_%d", i + 1);
        m_outPorts[i] = jack_port_register(m_client, nm, JACK_DEFAULT_AUDIO_TYPE,
                                           JackPortIsOutput, 0);
        if (!m_outPorts[i])
        {
            Close();
            return false;
        }
    }

    /* MIDI 端口（随插件能力） */
    m_midiInPort = m_midiOutPort = NULL;
    if (m_midiIn)
    {
        m_midiInPort = jack_port_register(m_client, "midi_in",
                                          JACK_DEFAULT_MIDI_TYPE,
                                          JackPortIsInput, 0);
        if (!m_midiInPort)
        {
            Close();
            return false;
        }
    }
    if (m_midiOut)
    {
        m_midiOutPort = jack_port_register(m_client, "midi_out",
                                           JACK_DEFAULT_MIDI_TYPE,
                                           JackPortIsOutput, 0);
        if (!m_midiOutPort)
        {
            Close();
            return false;
        }
    }

    if (jack_set_process_callback)
        jack_set_process_callback(m_client, &CJackBackend::JackProcessCB, this);
    if (jack_set_buffer_size_callback)
        jack_set_buffer_size_callback(m_client, &CJackBackend::JackBufferSizeCB, this);
    if (jack_on_shutdown)
        jack_on_shutdown(m_client, &CJackBackend::JackShutdownCB, this);

    return true;
}

bool CJackBackend::Start()
{
    if (!m_client || m_bRunning)
        return m_bRunning;
    if (!jack_activate)
        return false;
    if (jack_activate(m_client) == 0)
    {
        m_bRunning = true;
        /* 激活后再取一次服务端块大小（部分服务器激活时才确定最终值） */
        if (jack_get_buffer_size)
            m_bufSize = jack_get_buffer_size(m_client);
        return true;
    }
    return false;
}

void CJackBackend::Stop()
{
    if (m_client && m_bRunning && jack_deactivate)
        jack_deactivate(m_client);
    m_bRunning = false;
}

void CJackBackend::Close()
{
    Stop();
    m_hNotify = NULL;               /* 关闭期间不再通知（防 UI 重启重入） */
    if (m_client)
    {
        if (jack_client_close)
            jack_client_close(m_client);
        m_client = NULL;
    }
    memset(m_inPorts, 0, sizeof(m_inPorts));
    memset(m_outPorts, 0, sizeof(m_outPorts));
    m_midiInPort = m_midiOutPort = NULL;
    m_inCh = m_outCh = 0;
    m_midiInCount = 0;
}

/*****************************************************************************/
/* ProcessCB : JACK 实时回调（直接驱动插件处理）                               */
/*****************************************************************************/
int CJackBackend::JackProcessCB(jack_nframes_t nframes, void *arg)
{
    CJackBackend *b = (CJackBackend *)arg;
    if (!b || !b->m_cb)
        return 0;

    LARGE_INTEGER qpf, t0, t1;
    QueryPerformanceFrequency(&qpf);
    QueryPerformanceCounter(&t0);

    float *in[JACKBACKEND_MAX_CH], *out[JACKBACKEND_MAX_CH];
    for (int i = 0; i < JACKBACKEND_MAX_CH; i++)
    {
        in[i] = NULL;
        out[i] = NULL;
    }

    for (int i = 0; i < b->m_inCh && i < JACKBACKEND_MAX_CH; i++)
        if (b->m_inPorts[i])
            in[i] = (float *)b->jack_port_get_buffer(b->m_inPorts[i], nframes);

    for (int i = 0; i < b->m_outCh && i < JACKBACKEND_MAX_CH; i++)
    {
        if (b->m_outPorts[i])
        {
            out[i] = (float *)b->jack_port_get_buffer(b->m_outPorts[i], nframes);
            if (out[i])                       /* 输出清零（插件可能不写满） */
                memset(out[i], 0, nframes * sizeof(float));
        }
    }

    /* MIDI 输入：转短消息存数组，随后被处理回调消费（同一线程） */
    b->ClearMidiIn();
    if (b->m_midiInPort && b->jack_midi_get_event_count)
    {
        void *mbuf = b->jack_port_get_buffer(b->m_midiInPort, nframes);
        if (mbuf)
        {
            uint32_t n = b->jack_midi_get_event_count(mbuf);
            for (uint32_t i = 0; i < n && b->m_midiInCount < JACKBACKEND_MAX_MIDI; i++)
            {
                jack_midi_event_t ev;
                if (b->jack_midi_event_get(&ev, mbuf, i) == 0 && ev.size <= 3)
                {
                    unsigned char *dst = b->m_midiInData[b->m_midiInCount];
                    dst[0] = ev.buffer[0];
                    dst[1] = (ev.size >= 2) ? ev.buffer[1] : 0;
                    dst[2] = (ev.size >= 3) ? ev.buffer[2] : 0;
                    b->m_midiInLen[b->m_midiInCount] = (int)ev.size;
                    b->m_midiInCount++;
                }
            }
        }
    }

    /* MIDI 输出：清空缓冲（暂不写事件，输出事件收集待后续） */
    if (b->m_midiOutPort && b->jack_midi_clear_buffer)
    {
        void *mbuf = b->jack_port_get_buffer(b->m_midiOutPort, nframes);
        if (mbuf)
            b->jack_midi_clear_buffer(mbuf);
    }

    b->m_cb(b->m_ctx, in, out, (int)nframes, b->m_inCh, b->m_outCh);

    /* DSP 使用率 = 本块处理耗时 / 块周期 */
    QueryPerformanceCounter(&t1);
    if (qpf.QuadPart > 0)
        b->m_dspElapsedUs = (long)((double)(t1.QuadPart - t0.QuadPart)
                                   * 1e6 / (double)qpf.QuadPart);
    return 0;
}

/*****************************************************************************/
/* GetDspUsage : DSP 使用率（%）                                              */
/*****************************************************************************/
double CJackBackend::GetDspUsage() const
{
    if (!m_bRunning || m_bufSize <= 0 || m_sampleRate <= 0.0)
        return 0.0;
    double periodUs = (double)m_bufSize / m_sampleRate * 1e6;
    if (periodUs <= 0.0)
        return 0.0;
    return (double)m_dspElapsedUs * 100.0 / periodUs;
}

/*****************************************************************************/
/* WriteMidiOut : 把插件产生的 MIDI 输出事件写入 midi_out 端口（实时线程）     */
/*****************************************************************************/
void CJackBackend::WriteMidiOut(const IPlugin::PluginMidiEvent *ev, int n)
{
    if (!m_client || !m_midiOutPort || !ev || n <= 0)
        return;
    if (!jack_midi_event_write || !jack_port_get_buffer)
        return;
    /* process 回调线程内：midi_out 缓冲已 clear，可重复取缓冲（同一缓冲） */
    void *mbuf = jack_port_get_buffer(m_midiOutPort, m_bufSize);
    if (!mbuf)
        return;
    for (int i = 0; i < n; i++)
        jack_midi_event_write(mbuf, 0, ev[i].d, (size_t)ev[i].len);
}

void CJackBackend::GetMidiIn(int i, unsigned char *data, int &len) const
{
    if (i < 0 || i >= m_midiInCount)
    {
        len = 0;
        return;
    }
    data[0] = m_midiInData[i][0];
    data[1] = m_midiInData[i][1];
    data[2] = m_midiInData[i][2];
    len = m_midiInLen[i];
}

/*****************************************************************************/
/* ShutdownCB : JACK 服务器退出（通知 UI 线程，不能在回调里 close）            */
/*****************************************************************************/
void CJackBackend::JackShutdownCB(void *arg)
{
    CJackBackend *b = (CJackBackend *)arg;
    if (b && b->m_hNotify)
        ::PostMessageW(b->m_hNotify, WM_JACK_SHUTDOWN, 0, 0);
}

/*****************************************************************************/
/* BufferSizeCB : JACK 块大小变化（通知 UI 线程重新 setupProcessing）          */
/*****************************************************************************/
int CJackBackend::JackBufferSizeCB(jack_nframes_t nframes, void *arg)
{
    CJackBackend *b = (CJackBackend *)arg;
    if (!b)
        return 0;
    /* 只在实际变化时通知一次（服务器在客户端激活时也会回调当前值，
       若不判断会触发 UI 反复重启 → 死循环） */
    if (nframes == b->m_notifiedBufsize)
        return 0;
    b->m_notifiedBufsize = nframes;
    b->m_bufSize = nframes;
    if (b->m_hNotify)
        ::PostMessageW(b->m_hNotify, WM_JACK_BUFSIZE, (WPARAM)nframes, 0);
    return 0;
}
