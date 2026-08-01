// AsioBackend.h : ASIO 音频后端（包装 ASIO SDK，计划书 §5.5）
/******************************************************************************/
#pragma once

#include "IAudioBackend.h"

#include <string>
#include <vector>

#include "asiosys.h"    // 定义 NATIVE_INT64 / IEEE754_64FLOAT（ASIOSampleRate=double）
#include "asio.h"        // ASIO C API + 类型（含 asio.h 的导出声明）

// 每路缓冲最大通道数（与 ASIO SDK 例子一致）
#define ASIOBACKEND_MAX_CH 32

// ASIO 后端：枚举/加载 ASIO 驱动、缓冲转换（Int16/24/32/Float32/64）、
// 实时回调直接驱动插件处理。
// 默认设备选择由上层（MainWnd）决定：优先 "FL Studio ASIO"，避免抢占
// 其他 DAW 正在使用的 ASIO 硬件驱动。
class CAsioBackend : public IAudioBackend
{
public:
    CAsioBackend();
    virtual ~CAsioBackend();

    bool   Open(const char *driverName, double wantRate, int wantBufSize,
                int wantIn, int wantOut, void *sysRef) override;
    void   Close() override;
    bool   Start() override;
    void   Stop() override;

    double GetSampleRate() const override { return m_sampleRate; }
    int    GetBufferSize() const override { return (int)m_bufSize; }
    int    GetInputChannels() const override { return (int)m_inBufs; }
    int    GetOutputChannels() const override { return (int)m_outBufs; }
    const char *GetDriverName() const override { return m_driverName.c_str(); }

    bool   IsOpen() const { return m_bOpen; }
    bool   IsRunning() const { return m_bRunning; }

    void SetProcessCallback(ProcessCB cb, void *ctx) override { m_cb = cb; m_ctx = ctx; }

    // 枚举系统已安装的 ASIO 驱动（ANSI 名）
    static void EnumerateDrivers(std::vector<std::string> &names);
    // 打开驱动自带控制面板（需已 Open）
    bool ControlPanel();
    // DSP 使用率（%）：最近一块处理耗时 / 块周期；未运行返回 0
    double GetDspUsage() const;
    // ASIO 通道名（isInput 方向内 0-based；未 Open 返回空）
    const char *GetChannelName(int index, bool input) const;

private:
    // ASIO 回调（静态转发到单例）
    static ASIOTime *BufferSwitchTimeInfo(ASIOTime *timeInfo, long index,
                                          ASIOBool processNow);
    static void      BufferSwitch(long index, ASIOBool processNow);
    static void      SampleRateChanged(ASIOSampleRate sRate);
    static long      AsioMessages(long selector, long value, void *message,
                                  double *opt);

    // 格式转换（ASIOST 类型 -> float / float -> ASIOST 类型）
    void ConvertFromASIO(int ch, void *src, float *dst, long frames);
    void ConvertToASIO(int ch, float *src, void *dst, long frames);

private:
    static CAsioBackend *s_pThis;
    static ASIOCallbacks s_callbacks;

    std::string m_driverName;
    double m_sampleRate;
    long   m_bufSize;
    long   m_inCh, m_outCh;        // 驱动报告的总通道数
    long   m_inBufs, m_outBufs;    // 实际创建的缓冲数（<= kMax）
    bool   m_bOpen;
    bool   m_bRunning;
    long   m_curIndex;
    volatile long m_dspElapsedUs;   // 最近一块处理耗时（微秒，实时线程写/UI 读）

    ASIOBufferInfo  m_bufInfos[ASIOBACKEND_MAX_CH * 2];
    ASIOChannelInfo m_chanInfos[ASIOBACKEND_MAX_CH * 2];

    // 转换中转缓冲（float）
    std::vector<std::vector<float> > m_inF;
    std::vector<std::vector<float> > m_outF;
    std::vector<float *>             m_inPtrs, m_outPtrs;

    ProcessCB m_cb;
    void     *m_ctx;
};
