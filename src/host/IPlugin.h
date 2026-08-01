// IPlugin.h : 统一插件抽象接口（计划书 §5.2）
/******************************************************************************/
#pragma once

// 插件内部效果器信息（VST2 shell 子插件 / VST3 多组件）
struct PluginInternalInfo
{
    unsigned long uid;          // VST2 子插件 uniqueID；VST3 组件 class id 前 4 字节
    char          name[256];    // 显示名
};

// 统一插件抽象：VST2 / VST3 都实现该接口，上层（引擎/UI/后端）只依赖它
class IPlugin
{
public:
    virtual ~IPlugin() = default;

    // 插件显示名（VST2 effGetEffectName / VST3 组件名），可为空
    virtual const char *GetName() const { return nullptr; }

    // 生命周期
    virtual bool   Init(double sampleRate, int blockSize) = 0; // 含 setup/总线查询
    virtual void   Shutdown() = 0;

    // 音频设备打开后，以实际采样率/块大小重配置插件（M4 ASIO/JACK）
    virtual bool   ReconfigureAudio(double sampleRate, int blockSize)
    { return Init(sampleRate, blockSize); }

    // 音频
    virtual void   Process(float **in, float **out, int frames,
                           int inCh, int outCh, void *events) = 0;
    virtual int    GetInputChannels()  const = 0;
    virtual int    GetOutputChannels() const = 0;
    virtual bool   WantMidiInput()  const = 0; // VST2 receiveVstMidiEvent / VST3 事件总线
    virtual bool   WantMidiOutput() const = 0; // 插件是否能输出事件

    // MIDI 输出事件（插件产生，Process 后收集；默认无输出）
    struct PluginMidiEvent { unsigned char d[3]; int len; };
    // 收集本块插件产生的 MIDI 输出事件（实时线程，Process 后调用）；
    // 返回写入 out 的事件数（<= max）；max=0 时仅清除内部计数
    virtual int    CollectMidiOut(PluginMidiEvent *out, int max) { return 0; }
    // 是否为乐器（VST2 effFlagsIsSynth / VST3 subCategories 含 Instrument）；
    // 乐器不显示输入电平表
    virtual bool   IsInstrument() const { return false; }

    // MIDI / 参数 / 程序
    virtual void   SendMidiIn(const unsigned char *data, int len) = 0;
    virtual int    GetNumParams() const = 0;
    virtual float  GetParam(int idx) const = 0;
    virtual void   SetParam(int idx, float v) = 0;
    virtual void   GetParamName(int idx, char *out, int cap) const = 0;
    virtual void   GetParamDisplay(int idx, char *out, int cap) const = 0;

    // 状态（.fxp / .vstpreset / 内部 chunk）
    virtual bool   SaveState(void *&buf, int &size) = 0;
    virtual bool   LoadState(const void *buf, int size) = 0;

    // 编辑器
    virtual bool   HasEditor() const = 0;
    virtual bool   OpenEditor(void *parentHwnd) = 0;
    virtual void   CloseEditor() = 0;
    virtual void   Idle() = 0;

    // 编辑器尺寸（随插件界面自适应）：返回 false 表示未知
    virtual bool   GetEditorSize(int &width, int &height) const { return false; }
    // 插件是否请求了新的编辑器尺寸（供宿主定时检查后重新自适应）
    virtual bool   EditorSizeChanged() const { return false; }

    // Shell（内部效果器）支持：返回 0 表示无内部效果器
    // pInfo 为 NULL 时仅返回个数；否则填充第 index 个
    virtual int    GetInternalCount() const = 0;
    virtual bool   GetInternalInfo(int index, PluginInternalInfo *pInfo) const = 0;
    virtual int    GetCurrentInternal() const = 0;      // 当前内部效果器 index，-1 无
    virtual bool   SwitchInternal(int index) = 0;       // 切换到指定内部效果器（同模块重实例化）
};
