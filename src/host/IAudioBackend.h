// IAudioBackend.h : 音频后端抽象接口（计划书 §5.5）
/******************************************************************************/
#pragma once

// 统一音频后端：ASIO / JACK 都实现该接口，上层只依赖它
class IAudioBackend
{
public:
    virtual ~IAudioBackend() = default;

    // 打开设备并创建缓冲（不启动）。
    //   driverName : 后端设备名（ASIO 驱动名，ANSI）
    //   wantRate   : 期望采样率（驱动可能返回实际值，以 GetSampleRate 为准）
    //   wantBufSize: 期望块大小（<=0 用驱动首选；以 GetBufferSize 为准）
    //   wantIn/Out : 插件所需通道数（用于提示/映射，实际以驱动为准）
    //   sysRef     : 宿主窗口句柄（部分驱动需要）
    virtual bool Open(const char *driverName, double wantRate, int wantBufSize,
                      int wantIn, int wantOut, void *sysRef) = 0;
    virtual void Close() = 0;          // 停止并释放设备
    virtual bool Start() = 0;          // 启动音频流
    virtual void Stop() = 0;           // 停止音频流（不释放）

    virtual double GetSampleRate() const = 0;   // 实际采样率
    virtual int    GetBufferSize() const = 0;   // 实际块大小
    virtual int    GetInputChannels() const = 0; // 驱动实际输入通道数
    virtual int    GetOutputChannels() const = 0; // 驱动实际输出通道数
    virtual const char *GetDriverName() const = 0;

    // 实时处理回调（音频线程直接调用，禁止阻塞/分配/日志）
    //   in/out 为 float**，inCh/outCh 为驱动实际通道数，frames 为块大小
    typedef void (*ProcessCB)(void *ctx, float **in, float **out,
                              int frames, int inCh, int outCh);
    virtual void SetProcessCallback(ProcessCB cb, void *ctx) = 0;
};
