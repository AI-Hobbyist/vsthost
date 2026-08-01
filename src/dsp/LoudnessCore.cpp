// LoudnessCore.cpp : 响度核心实现（ITU-R BS.1770-4 K 权重 + gating）
/******************************************************************************/
#include "LoudnessCore.h"

#include <cmath>
#include <algorithm>

/* K 权重滤波器系数（BS.1770-4 附录，48kHz；非 48k 采样率近似使用） */
namespace
{
// Stage 1: 高频预加重（high-shelf，2 阶 IIR）
const double HS_B0 = 1.53512485958697;
const double HS_B1 = -2.69169618940638;
const double HS_B2 = 1.19839281085285;
const double HS_A1 = -1.69065929318241;
const double HS_A2 = 0.73248077421585;

// Stage 2: RLB 加权（2 阶 IIR）
const double RLB_B0 = 1.0;
const double RLB_B1 = -2.0;
const double RLB_B2 = 1.0;
const double RLB_A1 = -1.99004745483398;
const double RLB_A2 = 0.99007225036621;

// 绝对门限（LUFS，BS.1770-4）
const double kAbsGate = -70.0;
// 历史最大块数（6 小时 @ 400ms = 54000 块，防无限增长）
const size_t kMaxHist = 54000;
} // namespace

LoudnessCore::LoudnessCore() {}
LoudnessCore::~LoudnessCore() {}

double LoudnessCore::LoudnessOf(double meanSquare)
{
    if (meanSquare <= 0.0)
        return -70.0;
    return -0.691 + 10.0 * log10(meanSquare);
}

void LoudnessCore::ResetLocked()
{
    m_chan.assign(m_nCh > 0 ? m_nCh : 1, Chan());
    m_blockMs.assign(m_nCh > 0 ? m_nCh : 1, 0.0);
    m_blockPeak = 0.0;
    m_blockFrames = 0;
    m_totalFrames = 0;
    m_msHist.clear();
    m_peakHist.clear();
    m_silenceSeconds = 0.0;
    m_silentDone = false;
}

void LoudnessCore::Reset()
{
    std::lock_guard<std::mutex> lk(m_mtx);
    ResetLocked();
}

void LoudnessCore::Setup(double sampleRate, int channels)
{
    std::lock_guard<std::mutex> lk(m_mtx);
    m_sr = (sampleRate > 0) ? sampleRate : 48000.0;
    m_nCh = (channels > 0) ? channels : 1;
    m_blockLen = (long long)(0.4 * m_sr);   // 400ms 块
    if (m_blockLen < 1) m_blockLen = 1;
    ResetLocked();
}

void LoudnessCore::Process(const float *const *bufs, int nCh, int frames)
{
    if (!bufs || nCh <= 0 || frames <= 0)
        return;
    if ((int)m_chan.size() != nCh)
        Setup(m_sr, nCh);

    /* 每通道 K 权重滤波 + 块内平方累积（无锁，实时线程独占） */
    for (int c = 0; c < nCh; c++)
    {
        const float *in = bufs[c];
        if (!in)
            continue;
        Chan &k = m_chan[c];
        double ms = 0.0;
        for (int i = 0; i < frames; i++)
        {
            double x = in[i];

            /* Stage 1: high-shelf */
            double y1 = HS_B0 * x + HS_B1 * k.hs_x1 + HS_B2 * k.hs_x2
                        - HS_A1 * k.hs_y1 - HS_A2 * k.hs_y2;
            k.hs_x2 = k.hs_x1; k.hs_x1 = x;
            k.hs_y2 = k.hs_y1; k.hs_y1 = y1;

            /* Stage 2: RLB */
            double y2 = RLB_B0 * y1 + RLB_B1 * k.rlb_x1 + RLB_B2 * k.rlb_x2
                        - RLB_A1 * k.rlb_y1 - RLB_A2 * k.rlb_y2;
            k.rlb_x2 = k.rlb_x1; k.rlb_x1 = y1;
            k.rlb_y2 = k.rlb_y1; k.rlb_y1 = y2;

            ms += y2 * y2;

            double a = (x < 0.0) ? -x : x;
            if (a > m_blockPeak) m_blockPeak = a;
        }
        m_blockMs[c] += ms;
    }

    m_blockFrames += frames;
    m_totalFrames += frames;

    if (m_blockFrames < m_blockLen)
        return;

    /* 块结束：总 mean square = Σ 通道(块内平方和)/帧数 */
    double msSum = 0.0;
    for (int c = 0; c < nCh; c++)
        msSum += m_blockMs[c];
    msSum /= (double)m_blockFrames;

    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_msHist.push_back(msSum);
        m_peakHist.push_back(m_blockPeak);
        if (m_msHist.size() > kMaxHist)
        {
            m_msHist.pop_front();
            m_peakHist.pop_front();
        }

        /* 静音检测 + Integrated 自动重置：
           数字静音（ms=0）或块响度 <= 静音阈值 都算静音；
           每次进入“彻底静音”只在超过阈值时长时重置一次，
           持续静音不反复重置，直到重新有信号 */
        double loud = LoudnessOf(msSum);
        if (msSum <= 0.0 || loud <= m_silenceThresh)
        {
            if (!m_silentDone && m_silenceReset > 0.0)
            {
                m_silenceSeconds += (double)m_blockFrames / m_sr;
                if (m_silenceSeconds >= m_silenceReset)
                {
                    m_msHist.clear();
                    m_peakHist.clear();
                    m_silenceSeconds = 0.0;
                    m_silentDone = true;
                    m_autoResets++;
                }
            }
        }
        else
        {
            m_silenceSeconds = 0.0;
            m_silentDone = false;   /* 重新有信号后才允许下一次重置 */
        }
    }

    /* 重置块累积 */
    for (size_t c = 0; c < m_blockMs.size(); c++)
        m_blockMs[c] = 0.0;
    m_blockPeak = 0.0;
    m_blockFrames = 0;
}

LoudnessCore::Result LoudnessCore::GetResult() const
{
    Result r;
    std::lock_guard<std::mutex> lk(m_mtx);
    if (m_msHist.empty())
        return r;
    /* 峰值 */
    for (double p : m_peakHist)
        if (p > r.peak) r.peak = p;

    /* Momentary：最近一块（400ms），-70 绝对门 */
    r.momentary = LoudnessOf(m_msHist.back());
    r.haveMomentary = (r.momentary > kAbsGate);

    /* Short-term：最近 8 块（3.2s），-70 绝对门（线性域平均） */
    {
        double sum = 0.0; int n = 0;
        int start = (int)m_msHist.size() - 8;
        if (start < 0) start = 0;
        for (int i = start; i < (int)m_msHist.size(); i++)
        {
            if (LoudnessOf(m_msHist[i]) > kAbsGate)
            {
                sum += m_msHist[i];
                n++;
            }
        }
        if (n > 0)
        {
            r.shortTerm = LoudnessOf(sum / n);
            r.haveShortTerm = true;
        }
    }

    /* Integrated：两层 gating */
    {
        double sum = 0.0; int n = 0;
        for (double ms : m_msHist)
            if (LoudnessOf(ms) > kAbsGate)
            {
                sum += ms;
                n++;
            }
        if (n > 0)
        {
            double lktg = LoudnessOf(sum / n);
            double sum2 = 0.0; int n2 = 0;
            for (double ms : m_msHist)
                if (LoudnessOf(ms) > lktg - 10.0)
                {
                    sum2 += ms;
                    n2++;
                }
            if (n2 > 0)
            {
                r.integrated = LoudnessOf(sum2 / n2);
                r.haveIntegrated = true;
            }
        }
    }

    /* LRA：通过绝对门的块 loudness 10%~95% 百分位差 */
    {
        std::vector<double> lds;
        lds.reserve(m_msHist.size());
        for (double ms : m_msHist)
        {
            double L = LoudnessOf(ms);
            if (L > kAbsGate)
                lds.push_back(L);
        }
        if (lds.size() >= 4)
        {
            std::sort(lds.begin(), lds.end());
            size_t i10 = (size_t)(0.10 * (lds.size() - 1));
            size_t i95 = (size_t)(0.95 * (lds.size() - 1));
            r.lra = lds[i95] - lds[i10];
            if (r.lra < 0.0) r.lra = 0.0;
        }
    }
    return r;
}

long LoudnessCore::AutoResetCount() const
{
    std::lock_guard<std::mutex> lk(m_mtx);
    return m_autoResets;
}
