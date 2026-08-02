// Vst2Plugin.cpp : VST2 单插件封装实现
/******************************************************************************/
#include "Vst2Plugin.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* 判断模块路径文件名是否含 "WaveShell"（忽略大小写） */
static bool ContainsWaveShell(const char *path)
{
    if (!path) return false;
    const char *p = strrchr(path, '\\');
    const char *base = p ? p + 1 : path;
    // 去掉扩展名
    char lower[256] = { 0 };
    strncpy(lower, base, sizeof(lower) - 1);
    char *dot = strrchr(lower, '.');
    if (dot) *dot = '\0';
    for (char *q = lower; *q; q++)
        if (*q >= 'A' && *q <= 'Z')
            *q = (char)(*q - 'A' + 'a');
    return strstr(lower, "waveshell") != NULL;
}

Vst2Plugin *Vst2Plugin::s_pLoading = NULL;

/*****************************************************************************/
/* Vst2Plugin : 构造 / 析构                                                  */
/*****************************************************************************/
Vst2Plugin::Vst2Plugin()
{
    m_pEff = NULL;
    m_szPath[0] = '\0';
    m_szName[0] = '\0';
    m_bShell = false;
    m_bLoaded = false;
    m_bInited = false;
    m_dSampleRate = 44100.0;
    m_nBlockSize = 1024;
    m_nCurInternal = -1;
    m_pInternals = NULL;
    m_nInternals = 0;
    m_nMidiCount = 0;
    m_bEdSizeChanged = 0;
    m_nEdW = m_nEdH = 0;

    m_pMidiEvents = (VstEvents *)malloc(sizeof(VstEvents) + VST2_MAX_MIDI * sizeof(void *));
    m_pMidiSlots = new VstMidiEvent[VST2_MAX_MIDI];
    memset(m_pMidiEvents, 0, sizeof(VstEvents) + VST2_MAX_MIDI * sizeof(void *));
    memset(m_pMidiSlots, 0, VST2_MAX_MIDI * sizeof(VstMidiEvent));

    memset(&m_vstTimeInfo, 0, sizeof(m_vstTimeInfo));
    m_vstTimeInfo.sampleRate = 44100.0;
    m_vstTimeInfo.tempo = 120.0;
    m_vstTimeInfo.timeSigNumerator = 4;
    m_vstTimeInfo.timeSigDenominator = 4;
    m_vstTimeInfo.smpteFrameRate = 1;
}

Vst2Plugin::~Vst2Plugin()
{
    Unload();
    if (m_pMidiEvents) free(m_pMidiEvents);
    delete[] m_pMidiSlots;
    if (m_pInternals) delete[] m_pInternals;
}

/*****************************************************************************/
/* Load : 加载模块；WaveShell 则先枚举内部效果器再加载所选                   */
/*****************************************************************************/
bool Vst2Plugin::Load(const char *path, unsigned long shellUid)
{
    Unload();
    if (!path || !*path) return false;
    strncpy(m_szPath, path, sizeof(m_szPath) - 1);
    m_szPath[sizeof(m_szPath) - 1] = '\0';

    m_bShell = ContainsWaveShell(path);

    if (!m_bShell)
    {
        m_pEff = new CEffect;
        m_pEff->nUniqueId = shellUid;
        s_pLoading = this;
        bool ok = m_pEff->Load(path, AudioMasterCallback);
        s_pLoading = NULL;
        if (!ok)
        {
            delete m_pEff;
            m_pEff = NULL;
            return false;
        }
        m_nCurInternal = -1;
        m_bLoaded = true;
        RefreshName();
        return true;
    }

    /* WaveShell：先以 uid=0 加载母体，枚举内部效果器 */
    CEffect *t = new CEffect;
    t->nUniqueId = 0;
    s_pLoading = this;
    bool ok = t->Load(path, AudioMasterCallback);
    s_pLoading = NULL;
    if (!ok || t->EffGetPlugCategory() != kPlugCategShell)
    {
        delete t;
        /* 文件名含 WaveShell 但非 shell 类别，按普通插件处理 */
        if (ok)
        {
            m_pEff = t;
            m_bShell = false;
            m_nCurInternal = -1;
            m_bLoaded = true;
            RefreshName();
            return true;
        }
        delete t;
        return false;
    }

    /* 枚举内部效果器 */
    m_pInternals = new PluginInternalInfo[VST2_MAX_INTERNALS];
    m_nInternals = 0;
    char szName[256];
    unsigned long ulID;
    while ((ulID = t->EffGetNextShellPlugin(szName)) != 0 && m_nInternals < VST2_MAX_INTERNALS)
    {
        PluginInternalInfo &info = m_pInternals[m_nInternals];
        info.uid = ulID;
        strncpy(info.name, szName, sizeof(info.name) - 1);
        info.name[sizeof(info.name) - 1] = '\0';
        m_nInternals++;
    }
    delete t;

    if (m_nInternals <= 0)
    {
        delete[] m_pInternals;
        m_pInternals = NULL;
        return false;
    }

    /* 加载所选内部效果器（shellUid 匹配，否则默认第一个） */
    int idx = 0;
    for (int i = 0; i < m_nInternals; i++)
        if (m_pInternals[i].uid == shellUid)
        {
            idx = i;
            break;
        }
    if (!LoadInternal(idx))
        return false;
    m_bShell = true;
    return true;
}

/*****************************************************************************/
/* LoadInternal : 从同一模块按 uid 重实例化内部效果器                        */
/*****************************************************************************/
bool Vst2Plugin::LoadInternal(int idx)
{
    if (idx < 0 || idx >= m_nInternals) return false;

    if (m_pEff)
    {
        DeinitEffect();
        delete m_pEff;
        m_pEff = NULL;
    }

    m_pEff = new CEffect;
    m_pEff->nUniqueId = m_pInternals[idx].uid;
    s_pLoading = this;
    bool ok = m_pEff->Load(m_szPath, AudioMasterCallback);
    s_pLoading = NULL;
    if (!ok)
    {
        delete m_pEff;
        m_pEff = NULL;
        return false;
    }
    m_nCurInternal = idx;
    m_bLoaded = true;
    RefreshName();
    return true;
}

void Vst2Plugin::RefreshName()
{
    m_szName[0] = '\0';
    if (m_pEff && m_pEff->pEffect)
        m_pEff->EffGetEffectName(m_szName);
    if (m_szName[0] == '\0')
        strncpy(m_szName, m_szPath, sizeof(m_szName) - 1);   /* 回退：模块名 */
}

/*****************************************************************************/
/* GetChannelName : 返回第 idx 通道的真实端口名（effGetInput/OutputProperties */
/*   的引脚 label；label 为空或插件不支持则返回 false → 上层回退默认命名）   */
/*****************************************************************************/
bool Vst2Plugin::GetChannelName(int idx, bool input, char *out, int cap) const
{
    if (!out || cap <= 0 || !m_pEff || !m_pEff->pEffect)
        return false;
    VstPinProperties props;
    memset(&props, 0, sizeof(props));
    long rc = input ? m_pEff->EffGetInputProperties(idx, &props)
                    : m_pEff->EffGetOutputProperties(idx, &props);
    if (rc != 1 || props.label[0] == '\0')
        return false;
    strncpy(out, props.label, (size_t)cap - 1);
    out[cap - 1] = '\0';
    return true;
}

const char *Vst2Plugin::GetName() const
{
    return m_szName;
}

/*****************************************************************************/
/* Unload : 卸载                                                             */
/*****************************************************************************/
void Vst2Plugin::Unload()
{
    if (m_pEff)
    {
        DeinitEffect();
        delete m_pEff;
        m_pEff = NULL;
    }
    m_bLoaded = false;
    m_bInited = false;
    m_nCurInternal = -1;
}

/*****************************************************************************/
/* InitEffect / DeinitEffect : 初始化顺序（移植自 LoadPlugin）                */
/*****************************************************************************/
bool Vst2Plugin::InitEffect()
{
    if (!m_pEff || !m_pEff->pEffect) return false;

    CEffect *e = m_pEff;
    e->EffOpen();
    e->EffSetSampleRate((float)m_dSampleRate);
    /* 某些插件只在首次 setBlockSize 时分配缓冲，先给个大值 */
    e->EffSetBlockSize(11025);
    e->bWantMidi = (e->EffCanDo("receiveVstMidiEvent") == 1);
    e->EffResume();
    e->EffSuspend();
    e->EffSetBlockSize(m_nBlockSize);
    e->EffResume();
    m_bInited = true;
    return true;
}

void Vst2Plugin::DeinitEffect()
{
    if (m_pEff)
    {
        m_pEff->EffEditClose();
        m_pEff->EffSuspend();
        m_pEff->EffClose();
    }
    m_bInited = false;
}

/*****************************************************************************/
/* Init / Shutdown : IPlugin                                                 */
/*****************************************************************************/
bool Vst2Plugin::Init(double sampleRate, int blockSize)
{
    m_dSampleRate = sampleRate;
    m_nBlockSize = blockSize;
    m_vstTimeInfo.sampleRate = sampleRate;
    return InitEffect();
}

void Vst2Plugin::Shutdown()
{
    DeinitEffect();
}

/*****************************************************************************/
/* ReconfigureAudio : 以实际采样率/块大小重配置（音频设备打开后调用）        */
/*   不动编辑器；仅重发采样率/块大小并 resume                                 */
/*****************************************************************************/
bool Vst2Plugin::ReconfigureAudio(double sampleRate, int blockSize)
{
    m_dSampleRate = sampleRate;
    m_nBlockSize = blockSize;
    m_vstTimeInfo.sampleRate = sampleRate;
    if (!m_pEff || !m_pEff->pEffect)
        return false;
    if (!m_bInited)
        return InitEffect();
    CEffect *e = m_pEff;
    e->EffSetSampleRate((float)sampleRate);
    e->EffSuspend();
    e->EffSetBlockSize(blockSize);
    e->EffResume();
    return true;
}

/*****************************************************************************/
/* Process : 处理一帧音频 + MIDI                                             */
/*****************************************************************************/
void Vst2Plugin::Process(float **in, float **out, int frames,
                         int inCh, int outCh, void *events)
{
    if (!m_pEff || !m_pEff->pEffect) return;

    m_nMidiOut = 0;         /* 本块 MIDI 输出事件计数清零（回调期间填充） */

    if (m_nMidiCount > 0)
    {
        m_pMidiEvents->numEvents = m_nMidiCount;
        for (int i = 0; i < m_nMidiCount; i++)
            m_pMidiEvents->events[i] = (VstEvent *)&m_pMidiSlots[i];
        m_pEff->EffProcessEvents(m_pMidiEvents);
        m_nMidiCount = 0;
    }

    /* 双精度路径暂走 float（M2 保持简单）；将来可按 effFlagsCanDoubleReplacing 展开 */
    m_pEff->EffProcessReplacing(in, out, frames);
}

/*****************************************************************************/
/* 通道 / MIDI 能力                                                           */
/*****************************************************************************/
int Vst2Plugin::GetInputChannels() const
{
    return (m_pEff && m_pEff->pEffect) ? m_pEff->pEffect->numInputs : 0;
}

int Vst2Plugin::GetOutputChannels() const
{
    return (m_pEff && m_pEff->pEffect) ? m_pEff->pEffect->numOutputs : 0;
}

bool Vst2Plugin::WantMidiInput() const
{
    return m_pEff ? m_pEff->bWantMidi : false;
}

bool Vst2Plugin::IsInstrument() const
{
    return m_pEff && m_pEff->pEffect &&
           (m_pEff->pEffect->flags & effFlagsIsSynth) != 0;
}

bool Vst2Plugin::WantMidiOutput() const
{
    if (!m_pEff || !m_pEff->pEffect) return false;
    return m_pEff->EffCanDo("sendVstMidiEvent") == 1;
}

void Vst2Plugin::SendMidiIn(const unsigned char *data, int len)
{
    if (!m_pEff || !m_pEff->bWantMidi) return;
    if (m_nMidiCount >= VST2_MAX_MIDI) return;

    VstMidiEvent &ev = m_pMidiSlots[m_nMidiCount];
    memset(&ev, 0, sizeof(ev));
    ev.type = kVstMidiType;
    ev.byteSize = sizeof(VstMidiEvent);
    int n = len > 3 ? 3 : len;
    for (int i = 0; i < n; i++)
        ev.midiData[i] = data[i];
    m_nMidiCount++;
}

/*****************************************************************************/
/* CollectMidiOut : 收集本块插件产生的 MIDI 输出事件                          */
/*****************************************************************************/
int Vst2Plugin::CollectMidiOut(PluginMidiEvent *out, int max)
{
    int n = (m_nMidiOut < max) ? m_nMidiOut : max;
    for (int i = 0; i < n; i++)
        out[i] = m_midiOut[i];
    m_nMidiOut = 0;         /* 消费后清零 */
    return n;
}

/*****************************************************************************/
/* 参数                                                                       */
/*****************************************************************************/
int Vst2Plugin::GetNumParams() const
{
    return (m_pEff && m_pEff->pEffect) ? m_pEff->pEffect->numParams : 0;
}

float Vst2Plugin::GetParam(int idx) const
{
    return m_pEff ? m_pEff->EffGetParameter(idx) : 0.f;
}

void Vst2Plugin::SetParam(int idx, float v)
{
    if (m_pEff) m_pEff->EffSetParameter(idx, v);
}

void Vst2Plugin::GetParamName(int idx, char *out, int cap) const
{
    if (out && cap > 0) out[0] = '\0';
    if (m_pEff && out && cap > 0)
    {
        char tmp[256];
        m_pEff->EffGetParamName(idx, tmp);
        strncpy(out, tmp, cap - 1);
        out[cap - 1] = '\0';
    }
}

void Vst2Plugin::GetParamDisplay(int idx, char *out, int cap) const
{
    if (out && cap > 0) out[0] = '\0';
    if (m_pEff && out && cap > 0)
    {
        char tmp[256];
        m_pEff->EffGetParamDisplay(idx, tmp);
        strncpy(out, tmp, cap - 1);
        out[cap - 1] = '\0';
    }
}

/*****************************************************************************/
/* 状态（chunk 直通，M6 用 CFxBank 包装为 .fxp）                              */
/*****************************************************************************/
bool Vst2Plugin::SaveState(void *&buf, int &size)
{
    buf = NULL;
    size = 0;
    if (!m_pEff || !m_pEff->pEffect) return false;
    void *ptr = NULL;
    long n = m_pEff->EffGetChunk(&ptr, true);
    if (n > 0 && ptr)
    {
        void *cp = malloc(n);
        if (!cp) return false;
        memcpy(cp, ptr, n);
        buf = cp;
        size = (int)n;
        return true;
    }
    return false;
}

bool Vst2Plugin::LoadState(const void *buf, int size)
{
    if (!m_pEff || !m_pEff->pEffect || !buf || size <= 0) return false;
    return m_pEff->EffSetChunk((void *)buf, size, true) != 0;
}

/*****************************************************************************/
/* 编辑器                                                                     */
/*****************************************************************************/
bool Vst2Plugin::HasEditor() const
{
    if (!m_pEff || !m_pEff->pEffect) return false;
    ERect *rc = NULL;
    return m_pEff->EffEditGetRect(&rc) > 0;
}

bool Vst2Plugin::OpenEditor(void *parentHwnd)
{
    if (!m_pEff) return false;
    return m_pEff->EffEditOpen(parentHwnd) > 0;
}

void Vst2Plugin::CloseEditor()
{
    if (m_pEff) m_pEff->EffEditClose();
}

void Vst2Plugin::Idle()
{
    if (m_pEff) m_pEff->EffEditIdle();
}

/*****************************************************************************/
/* 编辑器尺寸自适应                                                            */
/*****************************************************************************/
void Vst2Plugin::NotifyEditorSize(long w, long h)
{
    m_nEdW = (int)w;
    m_nEdH = (int)h;
    InterlockedExchange(&m_bEdSizeChanged, 1);
}

bool Vst2Plugin::GetEditorSize(int &w, int &h) const
{
    if (!m_pEff || !m_pEff->pEffect) return false;
    ERect *rc = NULL;
    if (m_pEff->EffEditGetRect(&rc) > 0 && rc)
    {
        w = rc->right - rc->left;
        h = rc->bottom - rc->top;
        m_nEdW = w;
        m_nEdH = h;
        InterlockedExchange(&m_bEdSizeChanged, 0);
        return true;
    }
    return false;
}

bool Vst2Plugin::EditorSizeChanged() const
{
    return m_bEdSizeChanged != 0;
}

/*****************************************************************************/
/* 内部效果器（shell）                                                        */
/*****************************************************************************/
int Vst2Plugin::GetInternalCount() const
{
    return m_nInternals;
}

bool Vst2Plugin::GetInternalInfo(int index, PluginInternalInfo *pInfo) const
{
    if (index < 0 || index >= m_nInternals || !pInfo) return false;
    *pInfo = m_pInternals[index];
    return true;
}

int Vst2Plugin::GetCurrentInternal() const
{
    return m_nCurInternal;
}

bool Vst2Plugin::SwitchInternal(int index)
{
    if (!m_bShell) return false;
    if (index == m_nCurInternal) return true;
    if (!LoadInternal(index)) return false;
    if (m_bInited)
        InitEffect();               /* 切换后按原采样率/块大小重新初始化 */
    return true;
}

/*****************************************************************************/
/* AudioMasterCallback : 静态宿主回调入口                                     */
/*****************************************************************************/
VstIntPtr VSTCALLBACK Vst2Plugin::AudioMasterCallback(AEffect *effect, VstInt32 opcode,
                                                      VstInt32 index, VstIntPtr value,
                                                      void *ptr, float opt)
{
    Vst2Plugin *p = s_pLoading;
    if (!p)
        return 0;
    return p->OnAudioMaster(opcode, index, value, ptr, opt);
}

VstIntPtr Vst2Plugin::OnAudioMaster(VstInt32 opcode, VstInt32 index, VstIntPtr value, void *ptr, float opt)
{
    switch (opcode)
    {
    case audioMasterVersion:
        return 2400;
    case audioMasterCurrentId:
        return m_pEff ? m_pEff->nUniqueId : 0;
    case audioMasterIdle:
        Idle();
        return 1;
    case audioMasterWantMidi:
        if (m_pEff) m_pEff->bWantMidi = true;
        return 1;
    case audioMasterGetTime:
        return (VstIntPtr)&m_vstTimeInfo;
    case audioMasterProcessEvents:
        /* 插件输出事件：收集 VstMidiEvent（实时线程内，同一块内消费） */
        if (ptr)
        {
            VstEvents *evts = (VstEvents *)ptr;
            for (long i = 0; i < evts->numEvents && m_nMidiOut < VST2_MAX_MIDI; i++)
            {
                VstEvent *e = evts->events[i];
                if (!e || e->type != kVstMidiType)
                    continue;
                VstMidiEvent *me = (VstMidiEvent *)e;
                unsigned char st = me->midiData[0];
                int len = ((st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0) ? 2 : 3;
                PluginMidiEvent &o = m_midiOut[m_nMidiOut++];
                o.d[0] = me->midiData[0];
                o.d[1] = me->midiData[1];
                o.d[2] = me->midiData[2];
                o.len = len;
            }
        }
        return 1;
    case audioMasterGetSampleRate:
        return (VstIntPtr)m_dSampleRate;
    case audioMasterGetBlockSize:
        return m_nBlockSize;
    case audioMasterGetInputLatency:
    case audioMasterGetOutputLatency:
        return 0;
    case audioMasterGetAutomationState:
        return kVstAutomationReadWrite;
    case audioMasterGetVendorString:
        if (ptr) strcpy((char *)ptr, "vsthost");
        return 1;
    case audioMasterGetProductString:
        if (ptr) strcpy((char *)ptr, "vsthost single");
        return 1;
    case audioMasterGetVendorVersion:
        return 1;
    case audioMasterCanDo:
    {
        const char *s = (const char *)ptr;
        if (!s) return 0;
        if (!strcmp(s, "receiveVstMidiEvent")) return 1;
        if (!strcmp(s, "sendVstMidiEvent")) return 1;
        if (!strcmp(s, "shellCategory")) return 1;
        if (!strcmp(s, "sizeWindow")) return 1;
        if (!strcmp(s, "supplyIdle")) return 1;
        return 0;
    }
    case audioMasterGetLanguage:
        return kVstLangEnglish;
    case audioMasterGetDirectory:
        return (VstIntPtr)(m_pEff ? m_pEff->OnGetDirectory() : 0);
    case audioMasterSizeWindow:
        if (m_pEff)
        {
            m_pEff->OnSizeEditorWindow(index, (long)value);
            NotifyEditorSize(index, (long)value);
        }
        return 1;
    case audioMasterBeginEdit:
    case audioMasterEndEdit:
        return 1;
    case audioMasterAutomate:
        return 1;
    case audioMasterGetCurrentProcessLevel:
        return 0;
    case audioMasterUpdateDisplay:
        return 0;
    case audioMasterGetNumAutomatableParameters:
        return m_pEff ? m_pEff->OnGetNumAutomatableParameters() : 0;
    case audioMasterGetParameterQuantization:
        return 0;
    case audioMasterPinConnected:
        return 1;
    case audioMasterNeedIdle:
        if (m_pEff) m_pEff->bNeedIdle = true;
        return 1;
    default:
        break;
    }
    return 0;
}
