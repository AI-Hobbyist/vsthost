// Vst3Plugin.h : VST3 单插件封装（实现 IPlugin，基于 vst3sdk 3.8.0 hosting 层）
/******************************************************************************/
#pragma once

#include "IPlugin.h"

#include "public.sdk/source/vst/hosting/module.h"

#include <string>
#include <vector>
#include <memory>

namespace Steinberg {
    class IPlugView;
    namespace Vst {
        class PlugProvider;
        class IComponent;
        class IEditController;
        class IAudioProcessor;
        class EventList;
        class ParameterChanges;
        class HostProcessData;
    }
}

class Vst3Plugin : public IPlugin
{
public:
    Vst3Plugin();
    virtual ~Vst3Plugin();

    // 加载 .vst3 模块并收集组件（shellUid 为 0 时默认第一个）
    bool Load(const std::wstring &path, unsigned long shellUid = 0);
    void Unload();

    bool IsLoaded() const { return m_bLoaded; }
    bool IsShell()  const { return m_classInfos.size() > 1; }
    const std::wstring &GetPath() const { return m_szPath; }

    // IPlugin
    bool   Init(double sampleRate, int blockSize) override;
    bool   ReconfigureAudio(double sampleRate, int blockSize) override;
    void   Shutdown() override;
    const char *GetName() const override;
    void   Process(float **in, float **out, int frames,
                   int inCh, int outCh, void *events) override;
    int    GetInputChannels()  const override;
    int    GetOutputChannels() const override;
    bool   WantMidiInput()  const override;
    bool   WantMidiOutput() const override;
    bool   GetChannelName(int idx, bool input, char *out, int cap) const override;
    bool   IsInstrument() const override;
    void   SendMidiIn(const unsigned char *data, int len) override;
    int    CollectMidiOut(PluginMidiEvent *out, int max) override;
    int    GetNumParams() const override;
    float  GetParam(int idx) const override;
    void   SetParam(int idx, float v) override;
    void   GetParamName(int idx, char *out, int cap) const override;
    void   GetParamDisplay(int idx, char *out, int cap) const override;
    bool   SaveState(void *&buf, int &size) override;
    bool   LoadState(const void *buf, int size) override;
    bool   HasEditor() const override;
    bool   OpenEditor(void *parentHwnd) override;
    void   CloseEditor() override;
    void   Idle() override;
    bool   GetEditorSize(int &width, int &height) const override;
    bool   EditorSizeChanged() const override;

    // Shell（多组件模块）
    int    GetInternalCount() const override;
    bool   GetInternalInfo(int index, PluginInternalInfo *pInfo) const override;
    int    GetCurrentInternal() const override;
    bool   SwitchInternal(int index) override;

    // IPlugFrame::resizeView 回调（插件请求新编辑器尺寸）
    void   NotifyEditorSize(long w, long h);

protected:
    bool Instantiate(int index);      // 用 PlugProvider 实例化第 index 个组件
    void DestroyInstance();
    bool SetupAudio();                // setActive + setupProcessing + 总线查询
    void QueryBusses();
    void RefreshName();
    void SetEditorSize(int w, int h);

protected:
    std::wstring m_szPath;
    bool  m_bLoaded;
    bool  m_bInited;
    int   m_nCurClass;              // 当前组件 index（-1 无）
    char  m_szName[256];            // 当前组件显示名

    std::shared_ptr<VST3::Hosting::Module> m_module;
    std::vector<VST3::Hosting::ClassInfo>  m_classInfos;

    Steinberg::Vst::PlugProvider   *m_plug;       // 当前实例 provider
    Steinberg::Vst::IComponent     *m_component;  // addRef 持有
    Steinberg::Vst::IEditController *m_controller;
    Steinberg::Vst::IAudioProcessor *m_processor; // 从 component 查询，不单独持有

    // 总线能力
    int   m_inCh, m_outCh;          // 音频输入/输出通道总数
    int   m_nEventIn, m_nEventOut;  // 事件（MIDI）输入/输出总线数

    // 音频总线名（QueryBusses 收集，JACK 端口名用）
    struct AudioBusRec { std::string name; int channelCount; };
    std::vector<AudioBusRec> m_inBuses, m_outBuses;

    double m_dSampleRate;
    int    m_nBlockSize;

    // 处理数据
    Steinberg::Vst::EventList        *m_eventList;      // 输入事件
    Steinberg::Vst::EventList        *m_outEventList;   // 输出事件（插件产生）
    Steinberg::Vst::ParameterChanges *m_paramChanges;
    Steinberg::Vst::HostProcessData  *m_processData;
    std::vector<float>       m_paramValues;   // 参数值缓存（按 index）
    std::vector<unsigned int> m_paramIds;     // index -> VST3 ParamID

    // MIDI 输出暂存（Process 后由 CollectMidiOut 取走）
    PluginMidiEvent m_midiOut[256];
    int m_nMidiOut;

    // 编辑器
    Steinberg::IPlugView *m_view;
    void    *m_parentHwnd;
    void    *m_frame;           // Vst3PlugFrame（IPlugFrame 实现，.cpp 定义）
    int     m_edW, m_edH;
    bool    m_edSizeChanged;
};
