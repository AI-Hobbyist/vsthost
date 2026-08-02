// Vst2Plugin.h : VST2 单插件封装（实现 IPlugin）
/******************************************************************************/
#pragma once

#include "IPlugin.h"
#include "vst2/CEffect.h"

#define VST2_MAX_INTERNALS   256
#define VST2_MAX_MIDI        256

class Vst2Plugin : public IPlugin
{
public:
    Vst2Plugin();
    virtual ~Vst2Plugin();

    // 加载/卸载；shellUid=0 表示 shell 默认（第一个）子插件
    bool Load(const char *path, unsigned long shellUid = 0);
    void Unload();

    bool IsLoaded() const { return m_bLoaded; }
    bool IsShell()  const { return m_bShell; }
    const char *GetPath() const { return m_szPath; }

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

    // 插件 audioMasterSizeWindow 回调入口
    virtual void NotifyEditorSize(long w, long h);
    int    GetInternalCount() const override;
    bool   GetInternalInfo(int index, PluginInternalInfo *pInfo) const override;
    int    GetCurrentInternal() const override;
    bool   SwitchInternal(int index) override;

    // 宿主回调入口
    VstIntPtr OnAudioMaster(VstInt32 opcode, VstInt32 index, VstIntPtr value, void *ptr, float opt);

protected:
    bool InitEffect();          // 完成 sample rate / block size / resume 设置
    void DeinitEffect();
    bool LoadInternal(int idx); // 从同一模块按 uid 重实例化内部效果器
    void RefreshName();         // 查询 effGetEffectName 更新插件名

    static Vst2Plugin *s_pLoading;
    static VstIntPtr VSTCALLBACK AudioMasterCallback(AEffect *effect, VstInt32 opcode,
                                                     VstInt32 index, VstIntPtr value,
                                                     void *ptr, float opt);

protected:
    CEffect   *m_pEff;
    char       m_szPath[1024];
    char       m_szName[256];   // 插件显示名（effGetEffectName）
    bool       m_bShell;
    bool       m_bLoaded;
    bool       m_bInited;
    double     m_dSampleRate;
    int        m_nBlockSize;
    int        m_nCurInternal;

    // 内部效果器（shell 子插件）列表
    PluginInternalInfo *m_pInternals;
    int m_nInternals;

    // 编辑器尺寸自适应
    mutable volatile LONG m_bEdSizeChanged;
    mutable int m_nEdW, m_nEdH;

    // MIDI 输入暂存
    VstEvents    *m_pMidiEvents;
    VstMidiEvent *m_pMidiSlots;
    int m_nMidiCount;

    // MIDI 输出暂存（插件 audioMasterProcessEvents 收集，Process 后取走）
    PluginMidiEvent m_midiOut[VST2_MAX_MIDI];
    int m_nMidiOut;

    VstTimeInfo m_vstTimeInfo;

    // effGetSpeakerArrangement 缓存（逐通道名 fallback：扬声器自定义名 / 标准声道名）
    VstSpeakerArrangement m_speakerIn, m_speakerOut;
};
