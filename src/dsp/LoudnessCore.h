// LoudnessCore.h : ITU-R BS.1770-4 / EBU R128 / ATSC A/85 响度测量核心
//   K 权重滤波（48kHz 标准系数，其他采样率近似）+ 分块 gating：
//     Momentary  (M)  400ms 窗口
//     Short-term (S)  3s 滑动窗口（-70 LUFS 绝对门）
//     Integrated (I)  全程两层 gating（绝对 -70 + 相对 -10）
//     LRA             全程块 loudness 10%~95% 百分位差
//   ASIO 实时线程 Process() 写，UI 线程 GetResult() 读（内部互斥）
/******************************************************************************/
#pragma once

#include <vector>
#include <deque>
#include <mutex>

class LoudnessCore
{
public:
    LoudnessCore();
    ~LoudnessCore();

    // 重置全部状态（换插件 / 换采样率 / 手动重置）
    void Reset();

    // 初始化（采样率、通道数）
    void Setup(double sampleRate, int channels);

    // 实时线程：喂入一块音频（nCh 个通道，每通道 frames 帧）
    void Process(const float *const *bufs, int nCh, int frames);

    // Integrated 静音重置：连续静音（块响度 <= 静音阈值）超过该秒数自动重置
    // 累积（0 = 不重置）；阈值默认 -70 LUFS（BS.1770 绝对门）
    void SetSilenceReset(double seconds) { m_silenceReset = seconds; }
    double SilenceReset() const { return m_silenceReset; }
    void SetSilenceThreshold(double lufs) { m_silenceThresh = lufs; }
    double SilenceThreshold() const { return m_silenceThresh; }

    struct Result
    {
        double momentary = -70.0;   // LUFS（400ms）
        double shortTerm = -70.0;   // LUFS（3s）
        double integrated = -70.0;  // LUFS（全程）
        double lra = 0.0;           // LU
        double peak = 0.0;          // 全通道最大采样峰值（线性）
        bool   haveMomentary = false;
        bool   haveShortTerm = false;
        bool   haveIntegrated = false;
    };

    // UI 线程：读取当前结果
    Result GetResult() const;

    // 自动静音重置次数（供宿主检测后重开 CSV 日志文件）
    long AutoResetCount() const;

private:
    struct Chan
    {
        // K 权重：2 级 IIR（high-shelf + RLB），状态变量
        double hs_x1 = 0, hs_x2 = 0, hs_y1 = 0, hs_y2 = 0;
        double rlb_x1 = 0, rlb_x2 = 0, rlb_y1 = 0, rlb_y2 = 0;
    };

    void ResetLocked();
    static double LoudnessOf(double meanSquare);

    double m_sr = 48000.0;
    int    m_nCh = 0;
    long long m_blockLen = 0;       // 块长（0.4s 采样数）
    std::vector<Chan> m_chan;

    // 当前块累积（实时线程独占，不进锁）
    std::vector<double> m_blockMs;  // 每通道块内平方和
    double m_blockPeak = 0.0;
    long long m_blockFrames = 0;    // 当前块已累积帧
    long long m_totalFrames = 0;    // 累计处理帧数

    // 历史（m_mtx 保护）
    mutable std::mutex m_mtx;
    std::deque<double> m_msHist;    // 每块总 mean square（Σ 通道，对时间平均）
    std::deque<double> m_peakHist;  // 每块峰值
    double m_silenceSeconds = 0.0;  // 连续静音秒数
    double m_silenceReset = 0.0;    // 0 = 不重置
    double m_silenceThresh = -70.0; // 静音阈值（LUFS）
    bool   m_silentDone = false;    // 本次静音是否已重置过一次
    long   m_autoResets = 0;        // 自动重置次数（计数持续）
};
