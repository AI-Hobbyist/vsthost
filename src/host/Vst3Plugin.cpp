// Vst3Plugin.cpp : VST3 单插件封装实现
/******************************************************************************/
#include "Vst3Plugin.h"

#include <windows.h>

#include <cstring>
#include <cstdlib>
#include <cstdio>

/* vst3sdk 3.8.0 hosting 层 */
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include "public.sdk/source/common/memorystream.h"

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/base/funknownimpl.h"

/* BusNameToUtf8 : BusInfo.name（String128 = TChar[128]，TChar 为 char16_t，UTF-16）转 UTF-8。
   Windows 下 wchar_t 同为 16 位 UTF-16，先逐元素复制到 wchar_t 缓冲再转换 */
static std::string BusNameToUtf8(const Steinberg::Vst::String128 &name)
{
    wchar_t wname[128];
    for (int i = 0; i < 128; i++)
    {
        wname[i] = (wchar_t)name[i];
        if (name[i] == 0)
            break;
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, wname, -1, NULL, 0, NULL, NULL);
    if (n > 1)
    {
        std::string s((size_t)n - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wname, -1, &s[0], n, NULL, NULL);
        return s;
    }
    return std::string();
}

/*===========================================================================*/
/* IPlugFrame 实现：插件经 resizeView 请求新编辑器尺寸                        */
/*===========================================================================*/
class Vst3PlugFrame : public Steinberg::IPlugFrame
{
public:
    Vst3PlugFrame(Vst3Plugin *owner) : m_owner(owner) {}
    virtual ~Vst3PlugFrame() noexcept {}

    Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView *view,
                                             Steinberg::ViewRect *newSize) override
    {
        if (m_owner && newSize)
            m_owner->NotifyEditorSize(newSize->getWidth(), newSize->getHeight());
        return Steinberg::kResultTrue;
    }

    DECLARE_FUNKNOWN_METHODS
private:
    Vst3Plugin *m_owner;
};
IMPLEMENT_FUNKNOWN_METHODS(Vst3PlugFrame, Steinberg::IPlugFrame,
                           Steinberg::IPlugFrame::iid)

/*===========================================================================*/
/* Vst3Plugin                                                                  */
/*===========================================================================*/
Vst3Plugin::Vst3Plugin()
{
    m_szPath.clear();
    m_bLoaded = false;
    m_bInited = false;
    m_nCurClass = -1;
    m_szName[0] = '\0';
    m_plug = NULL;
    m_component = NULL;
    m_controller = NULL;
    m_processor = NULL;
    m_inCh = m_outCh = 0;
    m_nEventIn = m_nEventOut = 0;
    m_dSampleRate = 44100.0;
    m_nBlockSize = 512;
    m_eventList = new Steinberg::Vst::EventList(1024);
    m_outEventList = new Steinberg::Vst::EventList(256);
    m_paramChanges = new Steinberg::Vst::ParameterChanges(1024);
    m_processData = new Steinberg::Vst::HostProcessData;
    m_nMidiOut = 0;
    m_view = NULL;
    m_parentHwnd = NULL;
    m_frame = NULL;
    m_edW = m_edH = 0;
    m_edSizeChanged = false;
}

Vst3Plugin::~Vst3Plugin()
{
    Unload();
    if (m_frame) { delete (Vst3PlugFrame *)m_frame; m_frame = NULL; }
    delete m_eventList;
    delete m_outEventList;
    delete m_paramChanges;
    delete m_processData;
}

/*****************************************************************************/
/* Load : 加载 .vst3 模块并收集 AudioEffect 组件（shell）                     */
/*****************************************************************************/
bool Vst3Plugin::Load(const std::wstring &path, unsigned long /*shellUid*/)
{
    Unload();
    m_szPath = path;

    std::string pathUtf8;
    {
        int n = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, NULL, 0, NULL, NULL);
        if (n > 1)
        {
            pathUtf8.resize(n - 1);
            WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &pathUtf8[0], n, NULL, NULL);
        }
    }
    if (pathUtf8.empty())
        return false;

    std::string error;
    m_module = VST3::Hosting::Module::create(pathUtf8, error);
    if (!m_module)
        return false;

    m_classInfos.clear();
    auto factory = m_module->getFactory();
    for (auto &ci : factory.classInfos())
    {
        if (ci.category() != kVstAudioEffectClass)
            continue;
        m_classInfos.push_back(ci);
    }
    if (m_classInfos.empty())
    {
        m_module.reset();
        return false;
    }

    m_bLoaded = true;
    return true;
}

/*****************************************************************************/
/* Instantiate : 用 PlugProvider 实例化第 index 个组件                        */
/*****************************************************************************/
bool Vst3Plugin::Instantiate(int index)
{
    DestroyInstance();
    if (!m_module || index < 0 || index >= (int)m_classInfos.size())
        return false;

    /* 宿主上下文（IHostApplication） */
    static Steinberg::Vst::HostApplication s_hostApp;
    Steinberg::Vst::PluginContextFactory::instance().setPluginContext(&s_hostApp);

    m_plug = new Steinberg::Vst::PlugProvider(m_module->getFactory(),
                                              m_classInfos[index], false);
    if (!m_plug->initialize())
    {
        delete m_plug;
        m_plug = NULL;
        return false;
    }

    m_component = m_plug->getComponent();      /* addRef */
    m_controller = m_plug->getController();    /* addRef，可为 NULL */
    if (!m_component)
    {
        DestroyInstance();
        return false;
    }

    Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor> proc(m_component);
    if (proc)
    {
        m_processor = proc;
        m_processor->addRef();
    }

    m_nCurClass = index;
    RefreshName();
    return true;
}

/*****************************************************************************/
/* SetupAudio : setActive + setupProcessing + 总线查询 + 缓冲容器             */
/*****************************************************************************/
bool Vst3Plugin::SetupAudio()
{
    if (!m_component || !m_processor)
        return false;

    /* 激活所有总线（音频 + 事件，输入 + 输出）：
       严格 VST3 插件（如 Waves）要求宿主显式 activateBus 才会处理
       对应总线，否则 process() 返回成功但输出缓冲不被填充（输出全 0） */
    const Steinberg::Vst::MediaTypes media[2] =
        { Steinberg::Vst::kAudio, Steinberg::Vst::kEvent };
    const Steinberg::Vst::BusDirections dirs[2] =
        { Steinberg::Vst::kInput, Steinberg::Vst::kOutput };
    for (int m = 0; m < 2; m++)
        for (int d = 0; d < 2; d++)
        {
            int n = m_component->getBusCount(media[m], dirs[d]);
            for (int i = 0; i < n; i++)
                m_component->activateBus(media[m], dirs[d], i, true);
        }

    if (m_component->setActive(true) != Steinberg::kResultOk)
        return false;

    Steinberg::Vst::ProcessSetup setup;
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = m_dSampleRate;
    setup.maxSamplesPerBlock = m_nBlockSize;
    if (m_processor->setupProcessing(setup) != Steinberg::kResultOk)
        return false;

    QueryBusses();

    m_processData->prepare(*m_component, 0, Steinberg::Vst::kSample32);

    /* 参数缓存（index -> ParamID + 值，defaultNormalizedValue 初始化） */
    m_paramValues.clear();
    m_paramIds.clear();
    if (m_controller)
    {
        int n = m_controller->getParameterCount();
        for (int i = 0; i < n; i++)
        {
            Steinberg::Vst::ParameterInfo info;
            if (m_controller->getParameterInfo(i, info) == Steinberg::kResultOk)
            {
                m_paramIds.push_back((unsigned int)info.id);
                m_paramValues.push_back((float)info.defaultNormalizedValue);
            }
        }
    }

    m_bInited = true;
    return true;
}

/*****************************************************************************/
/* QueryBusses : 汇总音频通道数与事件（MIDI）总线数                           */
/*****************************************************************************/
void Vst3Plugin::QueryBusses()
{
    m_inCh = m_outCh = 0;
    m_nEventIn = m_nEventOut = 0;
    m_inBuses.clear();
    m_outBuses.clear();
    if (!m_component)
        return;

    int n = m_component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kInput);
    for (int i = 0; i < n; i++)
    {
        Steinberg::Vst::BusInfo info;
        if (m_component->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, i, info) == Steinberg::kResultOk)
        {
            m_inCh += info.channelCount;
            AudioBusRec b;
            b.name = BusNameToUtf8(info.name);        /* 真实总线名（可能为空） */
            b.channelCount = info.channelCount;
            m_inBuses.push_back(b);
        }
    }
    n = m_component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput);
    for (int i = 0; i < n; i++)
    {
        Steinberg::Vst::BusInfo info;
        if (m_component->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, i, info) == Steinberg::kResultOk)
        {
            m_outCh += info.channelCount;
            AudioBusRec b;
            b.name = BusNameToUtf8(info.name);
            b.channelCount = info.channelCount;
            m_outBuses.push_back(b);
        }
    }
    m_nEventIn = m_component->getBusCount(Steinberg::Vst::kEvent, Steinberg::Vst::kInput);
    m_nEventOut = m_component->getBusCount(Steinberg::Vst::kEvent, Steinberg::Vst::kOutput);
}

/*****************************************************************************/
/* GetChannelName : 返回第 idx 通道的真实端口名（所属音频总线的 name；多通道 */
/*   总线加 _N 后缀保证 JACK 端口名唯一）；总线名为空或找不到则返回 false    */
/*****************************************************************************/
bool Vst3Plugin::GetChannelName(int idx, bool input, char *out, int cap) const
{
    if (!out || cap <= 0)
        return false;
    const std::vector<AudioBusRec> &buses = input ? m_inBuses : m_outBuses;
    int off = 0;
    for (size_t b = 0; b < buses.size(); b++)
    {
        int cnt = buses[b].channelCount;
        if (idx >= off && idx < off + cnt)
        {
            const std::string &n = buses[b].name;
            if (n.empty())
                return false;
            std::string full = (cnt > 1) ? n + "_" + std::to_string(idx - off + 1) : n;
            strncpy(out, full.c_str(), (size_t)cap - 1);
            out[cap - 1] = '\0';
            return true;
        }
        off += cnt;
    }
    return false;
}

/*****************************************************************************/
/* DestroyInstance : 释放当前组件实例                                         */
/*****************************************************************************/
void Vst3Plugin::DestroyInstance()
{
    CloseEditor();

    if (m_processor) { m_processor->release(); m_processor = NULL; }
    if (m_controller) { m_controller->release(); m_controller = NULL; }
    if (m_component) { m_component->release(); m_component = NULL; }
    if (m_plug) { delete m_plug; m_plug = NULL; }

    m_nCurClass = -1;
    m_bInited = false;
}

/*****************************************************************************/
/* RefreshName : 更新当前组件显示名                                            */
/*****************************************************************************/
void Vst3Plugin::RefreshName()
{
    m_szName[0] = '\0';
    if (m_nCurClass >= 0 && m_nCurClass < (int)m_classInfos.size())
    {
        const std::string &name = m_classInfos[m_nCurClass].name();
        strncpy(m_szName, name.c_str(), sizeof(m_szName) - 1);
        m_szName[sizeof(m_szName) - 1] = '\0';
    }
}

/*****************************************************************************/
/* Unload : 卸载模块                                                          */
/*****************************************************************************/
void Vst3Plugin::Unload()
{
    DestroyInstance();
    m_classInfos.clear();
    m_module.reset();
    m_bLoaded = false;
    m_szPath.clear();
}

/*****************************************************************************/
/* IPlugin : 生命周期                                                          */
/*****************************************************************************/
bool Vst3Plugin::Init(double sampleRate, int blockSize)
{
    m_dSampleRate = sampleRate;
    m_nBlockSize = blockSize;

    if (m_nCurClass < 0)
    {
        if (!Instantiate(0))
            return false;
    }
    return SetupAudio();
}

void Vst3Plugin::Shutdown()
{
    if (m_component)
        m_component->setActive(false);
    DestroyInstance();
}

/*****************************************************************************/
/* ReconfigureAudio : 以实际采样率/块大小重配置（音频设备打开后调用）        */
/*   标准 VST3 流程：deactivate -> setupProcessing -> activate                */
/*****************************************************************************/
bool Vst3Plugin::ReconfigureAudio(double sampleRate, int blockSize)
{
    if (!m_component || !m_processor)
        return false;
    if (!m_bInited)
        return Init(sampleRate, blockSize);
    m_dSampleRate = sampleRate;
    m_nBlockSize = blockSize;
    m_component->setActive(false);
    return SetupAudio();
}

const char *Vst3Plugin::GetName() const
{
    return m_szName;
}

/*****************************************************************************/
/* IPlugin : 音频 / MIDI                                                       */
/*****************************************************************************/
void Vst3Plugin::Process(float **in, float **out, int frames,
                         int inCh, int outCh, void * /*events*/)
{
    if (!m_processor || !m_bInited || !m_processData)
        return;

    m_processData->numSamples = frames;

    int inOff = 0;
    int nIn = m_processData->numInputs;
    for (int b = 0; b < nIn && inOff < inCh; b++)
    {
        int ch = m_processData->inputs[b].numChannels;
        for (int c = 0; c < ch && inOff + c < inCh; c++)
            m_processData->setChannelBuffer(Steinberg::Vst::kInput, b, c,
                                            in ? in[inOff + c] : NULL);
        inOff += ch;
    }

    int outOff = 0;
    int nOut = m_processData->numOutputs;
    for (int b = 0; b < nOut && outOff < outCh; b++)
    {
        int ch = m_processData->outputs[b].numChannels;
        for (int c = 0; c < ch && outOff + c < outCh; c++)
            m_processData->setChannelBuffer(Steinberg::Vst::kOutput, b, c,
                                            out ? out[outOff + c] : NULL);
        outOff += ch;
    }

    m_processData->inputEvents = m_eventList;
    m_processData->inputParameterChanges = m_paramChanges;
    m_processData->outputEvents = m_outEventList;   /* 插件输出事件（MIDI） */

    m_processor->process(*m_processData);

    /* 收集插件输出的 MIDI 事件 → 字节（实时线程，下一块前被宿主取走）
       注：SDK 3.8.0 的 Event 仅 NoteOn/NoteOff/Data/PolyPressure；
       CC/弯音走 ParameterChanges（无通道信息），此处只收集音符类事件 */
    m_nMidiOut = 0;
    {
        int nOut = (int)m_outEventList->getEventCount();
        for (int i = 0; i < nOut && m_nMidiOut < 256; i++)
        {
            Steinberg::Vst::Event ev;
            if (m_outEventList->getEvent(i, ev) != Steinberg::kResultOk)
                continue;
            unsigned char ch = (unsigned char)(ev.noteOn.channel & 0x0F);
            PluginMidiEvent &m = m_midiOut[m_nMidiOut];
            switch (ev.type)
            {
            case Steinberg::Vst::Event::kNoteOnEvent:
                m.d[0] = (unsigned char)(0x90 | ch);
                m.d[1] = (unsigned char)ev.noteOn.pitch;
                m.d[2] = (unsigned char)(ev.noteOn.velocity * 127.f);
                m.len = 3; break;
            case Steinberg::Vst::Event::kNoteOffEvent:
                m.d[0] = (unsigned char)(0x80 | ch);
                m.d[1] = (unsigned char)ev.noteOff.pitch;
                m.d[2] = (unsigned char)(ev.noteOff.velocity * 127.f);
                m.len = 3; break;
            case Steinberg::Vst::Event::kPolyPressureEvent:
                m.d[0] = (unsigned char)(0xA0 | ch);
                m.d[1] = (unsigned char)ev.polyPressure.pitch;
                m.d[2] = (unsigned char)(ev.polyPressure.pressure * 127.f);
                m.len = 3; break;
            default:
                continue;   /* 其他事件类型跳过 */
            }
            m_nMidiOut++;
        }
    }
    m_outEventList->clear();

    m_eventList->clear();
    m_paramChanges->clearQueue();
}

/*****************************************************************************/
/* CollectMidiOut : 收集本块插件产生的 MIDI 输出事件                          */
/*****************************************************************************/
int Vst3Plugin::CollectMidiOut(PluginMidiEvent *out, int max)
{
    int n = (m_nMidiOut < max) ? m_nMidiOut : max;
    for (int i = 0; i < n; i++)
        out[i] = m_midiOut[i];
    m_nMidiOut = 0;         /* 消费后清零 */
    return n;
}

int Vst3Plugin::GetInputChannels() const  { return m_inCh; }
int Vst3Plugin::GetOutputChannels() const { return m_outCh; }
bool Vst3Plugin::WantMidiInput()  const   { return m_nEventIn > 0; }
bool Vst3Plugin::WantMidiOutput() const   { return m_nEventOut > 0; }

bool Vst3Plugin::IsInstrument() const
{
    if (m_nCurClass < 0 || m_nCurClass >= (int)m_classInfos.size())
        return false;
    const auto &sc = m_classInfos[m_nCurClass].subCategories();
    for (size_t i = 0; i < sc.size(); i++)
        if (_stricmp(sc[i].c_str(), "Instrument") == 0)
            return true;
    return false;
}

void Vst3Plugin::SendMidiIn(const unsigned char *data, int len)
{
    if (len < 2)
        return;

    unsigned char status = data[0] & 0xF0;
    unsigned char channel = data[0] & 0x0F;

    switch (status)
    {
    case 0x80: /* Note Off -> Event */
        if (m_eventList)
        {
            Steinberg::Vst::Event e = {};
            e.busIndex = 0;
            e.sampleOffset = 0;
            e.ppqPosition = 0;
            e.flags = 0;
            e.type = Steinberg::Vst::Event::kNoteOffEvent;
            e.noteOff.channel = channel;
            e.noteOff.pitch = data[1];
            e.noteOff.velocity = (len > 2) ? data[2] / 127.f : 0.f;
            e.noteOff.noteId = -1;
            m_eventList->addEvent(e);
        }
        break;
    case 0x90: /* Note On（velocity 0 视为 Note Off） -> Event */
        if (m_eventList)
        {
            Steinberg::Vst::Event e = {};
            e.busIndex = 0;
            e.sampleOffset = 0;
            e.ppqPosition = 0;
            e.flags = 0;
            if (len > 2 && data[2] == 0)
            {
                e.type = Steinberg::Vst::Event::kNoteOffEvent;
                e.noteOff.channel = channel;
                e.noteOff.pitch = data[1];
                e.noteOff.velocity = 0.f;
                e.noteOff.noteId = -1;
            }
            else
            {
                e.type = Steinberg::Vst::Event::kNoteOnEvent;
                e.noteOn.channel = channel;
                e.noteOn.pitch = data[1];
                e.noteOn.velocity = (len > 2) ? data[2] / 127.f : 1.f;
                e.noteOn.noteId = -1;
            }
            m_eventList->addEvent(e);
        }
        break;
    case 0xA0: /* Poly Pressure -> Event */
        if (m_eventList)
        {
            Steinberg::Vst::Event e = {};
            e.busIndex = 0;
            e.sampleOffset = 0;
            e.ppqPosition = 0;
            e.flags = 0;
            e.type = Steinberg::Vst::Event::kPolyPressureEvent;
            e.polyPressure.channel = channel;
            e.polyPressure.pitch = data[1];
            e.polyPressure.pressure = (len > 2) ? data[2] / 127.f : 0.f;
            m_eventList->addEvent(e);
        }
        break;
    case 0xB0: /* Control Change -> ParameterChanges（ParamID = CC 号） */
    {
        Steinberg::Vst::ParamID pid = data[1];
        float val = (len > 2) ? data[2] / 127.f : 0.f;
        if (m_paramChanges)
        {
            Steinberg::int32 index = 0;
            if (Steinberg::Vst::IParamValueQueue *q = m_paramChanges->addParameterData(pid, index))
                q->addPoint(0, val, index);
        }
        break;
    }
    case 0xC0: /* Program Change -> ParameterChanges（kCtrlProgramChange=130） */
        if (m_paramChanges)
        {
            Steinberg::int32 index = 0;
            if (Steinberg::Vst::IParamValueQueue *q =
                    m_paramChanges->addParameterData(Steinberg::Vst::kCtrlProgramChange, index))
                q->addPoint(0, data[1] / 127.f, index);
        }
        break;
    case 0xD0: /* Channel Pressure -> ParameterChanges（kAfterTouch=128） */
        if (m_paramChanges)
        {
            Steinberg::int32 index = 0;
            if (Steinberg::Vst::IParamValueQueue *q =
                    m_paramChanges->addParameterData(Steinberg::Vst::kAfterTouch, index))
                q->addPoint(0, data[1] / 127.f, index);
        }
        break;
    case 0xE0: /* Pitch Bend -> ParameterChanges（kPitchBend=129） */
        if (m_paramChanges && len > 2)
        {
            float val = ((data[1] & 0x7F) | ((data[2] & 0x7F) << 7)) / (float)0x3FFF;
            Steinberg::int32 index = 0;
            if (Steinberg::Vst::IParamValueQueue *q =
                    m_paramChanges->addParameterData(Steinberg::Vst::kPitchBend, index))
                q->addPoint(0, val, index);
        }
        break;
    default:
        break;
    }
}

/*****************************************************************************/
/* IPlugin : 参数                                                              */
/*****************************************************************************/
int Vst3Plugin::GetNumParams() const
{
    return (int)m_paramValues.size();
}

float Vst3Plugin::GetParam(int idx) const
{
    return (idx >= 0 && idx < (int)m_paramValues.size()) ? m_paramValues[idx] : 0.f;
}

void Vst3Plugin::SetParam(int idx, float v)
{
    if (idx < 0 || idx >= (int)m_paramValues.size())
        return;
    m_paramValues[idx] = v;

    Steinberg::Vst::ParamID pid = (Steinberg::Vst::ParamID)m_paramIds[idx];
    if (m_controller)
        m_controller->setParamNormalized(pid, v);
    if (m_paramChanges)
    {
        Steinberg::int32 index = 0;
        Steinberg::Vst::IParamValueQueue *q =
            m_paramChanges->addParameterData(pid, index);
        if (q)
            q->addPoint(0, v, index);
    }
}

void Vst3Plugin::GetParamName(int idx, char *out, int cap) const
{
    if (out && cap > 0)
        out[0] = '\0';
    if (!m_controller || idx < 0 || !out || cap <= 0)
        return;
    Steinberg::Vst::ParameterInfo info;
    if (m_controller->getParameterInfo(idx, info) != Steinberg::kResultOk)
        return;
    std::string s = Steinberg::Vst::StringConvert::convert(info.title);
    strncpy(out, s.c_str(), cap - 1);
    out[cap - 1] = '\0';
}

void Vst3Plugin::GetParamDisplay(int idx, char *out, int cap) const
{
    if (out && cap > 0)
        out[0] = '\0';
    if (!m_controller || idx < 0 || !out || cap <= 0 || idx >= (int)m_paramIds.size())
        return;
    Steinberg::Vst::String128 str = {};
    if (m_controller->getParamStringByValue((Steinberg::Vst::ParamID)m_paramIds[idx],
                                            GetParam(idx), str) != Steinberg::kResultOk)
        return;
    std::string s = Steinberg::Vst::StringConvert::convert(str);
    strncpy(out, s.c_str(), cap - 1);
    out[cap - 1] = '\0';
}

/*****************************************************************************/
/* IPlugin : 状态（IComponent::getState/setState 经 MemoryStream）             */
/*****************************************************************************/
bool Vst3Plugin::SaveState(void *&buf, int &size)
{
    buf = NULL;
    size = 0;
    if (!m_component)
        return false;

    Steinberg::MemoryStream stream;
    if (m_component->getState(&stream) != Steinberg::kResultOk)
        return false;
    if (stream.getSize() <= 0)
        return false;

    buf = malloc((size_t)stream.getSize());
    if (!buf)
        return false;
    memcpy(buf, stream.getData(), (size_t)stream.getSize());
    size = (int)stream.getSize();
    return true;
}

bool Vst3Plugin::LoadState(const void *buf, int size)
{
    if (!m_component || !buf || size <= 0)
        return false;
    Steinberg::MemoryStream stream((void *)buf, size);
    return m_component->setState(&stream) == Steinberg::kResultOk;
}

/*****************************************************************************/
/* IPlugin : 编辑器                                                            */
/*****************************************************************************/
bool Vst3Plugin::HasEditor() const
{
    return m_controller != NULL;
}

bool Vst3Plugin::OpenEditor(void *parentHwnd)
{
    if (!m_controller)
        return false;
    CloseEditor();

    m_view = m_controller->createView(Steinberg::Vst::ViewType::kEditor);
    if (!m_view)
        return false;

    if (!m_frame)
        m_frame = new Vst3PlugFrame(this);
    m_view->setFrame((Steinberg::IPlugFrame *)m_frame);

    if (m_view->isPlatformTypeSupported(Steinberg::kPlatformTypeHWND) != Steinberg::kResultTrue)
    {
        CloseEditor();
        return false;
    }
    if (m_view->attached(parentHwnd, Steinberg::kPlatformTypeHWND) != Steinberg::kResultTrue)
    {
        CloseEditor();
        return false;
    }

    m_parentHwnd = parentHwnd;

    Steinberg::ViewRect r;
    if (m_view->getSize(&r) == Steinberg::kResultTrue)
    {
        m_edW = r.getWidth();
        m_edH = r.getHeight();
        m_edSizeChanged = false;
    }
    return true;
}

void Vst3Plugin::CloseEditor()
{
    if (m_view)
    {
        m_view->setFrame(NULL);
        m_view->removed();
        m_view->release();
        m_view = NULL;
    }
    m_parentHwnd = NULL;
}

void Vst3Plugin::Idle()
{
    /* VST3 无 idle；编辑器消息由插件自身处理 */
}

bool Vst3Plugin::GetEditorSize(int &width, int &height) const
{
    if (m_edW > 0 && m_edH > 0)
    {
        width = m_edW;
        height = m_edH;
        return true;
    }
    return false;
}

bool Vst3Plugin::EditorSizeChanged() const
{
    return m_edSizeChanged;
}

void Vst3Plugin::NotifyEditorSize(long w, long h)
{
    m_edW = (int)w;
    m_edH = (int)h;
    m_edSizeChanged = true;
}

/*****************************************************************************/
/* IPlugin : Shell（多组件模块）                                               */
/*****************************************************************************/
int Vst3Plugin::GetInternalCount() const
{
    return (int)m_classInfos.size();
}

bool Vst3Plugin::GetInternalInfo(int index, PluginInternalInfo *pInfo) const
{
    if (!pInfo || index < 0 || index >= (int)m_classInfos.size())
        return false;
    const auto &ci = m_classInfos[index];

    pInfo->uid = 0;
    const auto &id = ci.ID();           /* 16 字节 TUID，取前 4 字节作 uid */
    if (id.size() >= 4)
        memcpy(&pInfo->uid, id.data(), 4);

    strncpy(pInfo->name, ci.name().c_str(), sizeof(pInfo->name) - 1);
    pInfo->name[sizeof(pInfo->name) - 1] = '\0';
    return true;
}

int Vst3Plugin::GetCurrentInternal() const
{
    return m_nCurClass;
}

bool Vst3Plugin::SwitchInternal(int index)
{
    if (index < 0 || index >= (int)m_classInfos.size() || index == m_nCurClass)
        return false;

    CloseEditor();

    if (!Instantiate(index))
        return false;
    if (!SetupAudio())
    {
        DestroyInstance();
        return false;
    }
    return true;
}
