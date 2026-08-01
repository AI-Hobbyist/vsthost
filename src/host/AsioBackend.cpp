// AsioBackend.cpp : ASIO 后端实现（M4）
/******************************************************************************/
#include "AsioBackend.h"

#include <windows.h>
#include <string.h>

#include "asiodrivers.h"     // AsioDrivers（枚举 + loadAsioDriver）

/* 全局驱动加载器（定义于 VST\asio\host\asiodrivers.cpp） */
extern bool loadAsioDriver(char *name);

/*===========================================================================*/
/* 格式转换工具（移植自 AsioHost.cpp / CSpecAsioHost）                        */
/*===========================================================================*/
namespace {

inline float Saturate(float input, float fMax)
{
    static const float fGrdDiv = 0.5f;
    float x1 = (float)fabs((double)(input + fMax));
    float x2 = (float)fabs((double)(input - fMax));
    return fGrdDiv * (x1 - x2);
}

void ReverseEndian2(void *buffer, long frames)
{
    char *a = (char *)buffer, c;
    while (--frames >= 0) { c = a[0]; a[0] = a[1]; a[1] = c; a += 2; }
}

void ReverseEndian3(void *buffer, long frames)
{
    char *a = (char *)buffer, c;
    while (--frames >= 0) { c = a[0]; a[0] = a[2]; a[2] = c; a += 3; }
}

void ReverseEndian4(void *buffer, long frames)
{
    char *a = (char *)buffer, c;
    while (--frames >= 0)
    {
        c = a[0]; a[0] = a[3]; a[3] = c;
        c = a[1]; a[1] = a[2]; a[2] = c;
        a += 4;
    }
}

void ReverseEndian8(void *buffer, long frames)
{
    char *a = (char *)buffer, c;
    while (--frames >= 0)
    {
        c = a[0]; a[0] = a[7]; a[7] = c;
        c = a[1]; a[1] = a[6]; a[6] = c;
        c = a[2]; a[2] = a[5]; a[5] = c;
        c = a[3]; a[3] = a[4]; a[4] = c;
        a += 8;
    }
}

void ToFloat16(void *source, float *target, long frames)
{
    short *src = (short *)source;
    while (--frames >= 0)
        *target++ = ((float)(*src++) + .5f) * (1.0f / 32767.503f);
}

void FromFloat16(float *source, void *target, long frames)
{
    short *dst = (short *)target;
    while (--frames >= 0)
        *dst++ = (short)((Saturate(*source++, 1.f) * 32767.505f) - .5f);
}

void ToFloat24(void *source, float *target, long frames)
{
    union { long lValue; char cValue[4]; } u;
    char *src = (char *)source, *dst;
    u.lValue = 0;
    while (--frames >= 0)
    {
#if ASIO_LITTLE_ENDIAN
        dst = &u.cValue[1];
#else
        dst = &u.cValue[0];
#endif
        *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
        *target++ = ((float)(u.lValue >> 8) + .5f) * (1.0f / 8388607.75f);
    }
}

void FromFloat24(float *source, void *target, long frames)
{
    union { long lValue; char cValue[4]; } u;
    char *src, *dst = (char *)target;
    while (--frames >= 0)
    {
        u.lValue = ((long)((Saturate(*source++, 1.0f) * 8388607.75f) - .5f)) << 8;
#if ASIO_LITTLE_ENDIAN
        src = &u.cValue[1];
#else
        src = &u.cValue[0];
#endif
        *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
    }
}

void ToFloat32(void *source, float *target, long relevantBits, long frames)
{
    signed char nShift = (signed char)(relevantBits - 24);
    long *src = (long *)source, inter;
    if (nShift < 0)
    {
        nShift = (signed char)-nShift;
        while (--frames >= 0)
        {
            inter = (*src++) << nShift;
            *target++ = ((float)inter + .5f) * (1.0f / 8388607.75f);
        }
    }
    else
    {
        while (--frames >= 0)
        {
            inter = (*src++) >> nShift;
            *target++ = ((float)inter + .5f) * (1.0f / 8388607.75f);
        }
    }
}

void FromFloat32(float *source, void *target, long relevantBits, long frames)
{
    signed char nShift = (signed char)(relevantBits - 24);
    long *dst = (long *)target;
    if (nShift < 0)
    {
        nShift = (signed char)-nShift;
        while (--frames >= 0)
            *dst++ = ((long)((Saturate(*source++, 1.f) * 8388607.75f) - .5f)) >> nShift;
    }
    else
    {
        while (--frames >= 0)
            *dst++ = ((long)((Saturate(*source++, 1.f) * 8388607.75f) - .5f)) << nShift;
    }
}

void ToFloat64(void *source, float *target, long frames)
{
    double *src = (double *)source;
    while (--frames >= 0)
        *target++ = (float)(*src++);
}

void FromFloat64(float *source, void *target, long frames)
{
    double *dst = (double *)target;
    while (--frames >= 0)
        *dst++ = (double)Saturate(*source++, 1.0f);
}

} // namespace

/*===========================================================================*/
/* 静态成员                                                                   */
/*===========================================================================*/
CAsioBackend *CAsioBackend::s_pThis = NULL;

ASIOCallbacks CAsioBackend::s_callbacks =
{
    CAsioBackend::BufferSwitch,
    CAsioBackend::SampleRateChanged,
    CAsioBackend::AsioMessages,
    CAsioBackend::BufferSwitchTimeInfo
};

/*****************************************************************************/
/* CAsioBackend                                                              */
/*****************************************************************************/
CAsioBackend::CAsioBackend()
    : m_sampleRate(0), m_bufSize(0), m_inCh(0), m_outCh(0),
      m_inBufs(0), m_outBufs(0), m_bOpen(false),
      m_bRunning(false), m_curIndex(0), m_dspElapsedUs(0),
      m_cb(NULL), m_ctx(NULL)
{
}

CAsioBackend::~CAsioBackend()
{
    Close();
}

/*****************************************************************************/
/* EnumerateDrivers : 枚举系统已安装的 ASIO 驱动                              */
/*****************************************************************************/
void CAsioBackend::EnumerateDrivers(std::vector<std::string> &names)
{
    names.clear();
    AsioDrivers drivers;            /* 构造时枚举注册表 */
    char namebuf[64][64];
    char *nameptrs[64];
    for (int i = 0; i < 64; i++)
        nameptrs[i] = namebuf[i];
    long n = drivers.getDriverNames(nameptrs, 64);
    for (long i = 0; i < n; i++)
        if (nameptrs[i][0])
            names.push_back(nameptrs[i]);
}

/*****************************************************************************/
/* Open : 加载驱动 + 初始化 + 建缓冲                                          */
/*****************************************************************************/
bool CAsioBackend::Open(const char *driverName, double wantRate, int wantBufSize,
                        int wantIn, int wantOut, void *sysRef)
{
    (void)wantIn; (void)wantOut;
    Close();

    if (!driverName || !driverName[0])
        return false;

    /* 加载驱动 DLL 并取得 IASIO 接口（theAsioDriver，由 asio.cpp 提供） */
    if (!loadAsioDriver((char *)driverName))
        return false;
    m_driverName = driverName;

    /* 初始化驱动 */
    ASIODriverInfo info;
    memset(&info, 0, sizeof(info));
    info.asioVersion = 2;
    info.sysRef = sysRef;
    if (ASIOInit(&info) != ASE_OK)
    {
        ASIOExit();
        m_driverName.clear();
        return false;
    }

    /* 通道数 */
    if (ASIOGetChannels(&m_inCh, &m_outCh) != ASE_OK)
    {
        ASIOExit();
        m_driverName.clear();
        return false;
    }
    m_inBufs  = (m_inCh  > ASIOBACKEND_MAX_CH) ? ASIOBACKEND_MAX_CH : m_inCh;
    m_outBufs = (m_outCh > ASIOBACKEND_MAX_CH) ? ASIOBACKEND_MAX_CH : m_outCh;

    /* 块大小：期望值限制到驱动范围 */
    long minS = 0, maxS = 0, prefS = 0, gran = 0;
    if (ASIOGetBufferSize(&minS, &maxS, &prefS, &gran) != ASE_OK)
    {
        ASIOExit();
        m_driverName.clear();
        return false;
    }
    long nBuf = (wantBufSize > 0) ? wantBufSize : prefS;
    if (nBuf < minS) nBuf = minS;
    if (nBuf > maxS) nBuf = maxS;
    if (gran > 1)
        nBuf = (nBuf / gran) * gran;
    if (nBuf <= 0)
        nBuf = prefS;
    m_bufSize = nBuf;

    /* 采样率（驱动不报告时设为 44100） */
    double rate = 0;
    if (ASIOGetSampleRate(&rate) != ASE_OK || rate <= 0.0 || rate > 96000.0)
    {
        ASIOSetSampleRate(44100.0);
        ASIOGetSampleRate(&rate);
    }
    if (rate <= 0.0 || rate > 96000.0)
        rate = 44100.0;
    /* 若想要不同采样率且驱动支持，切换之 */
    if (wantRate > 0.0 && wantRate != rate)
    {
        if (ASIOCanSampleRate(wantRate) == ASE_OK)
        {
            ASIOSetSampleRate(wantRate);
            ASIOGetSampleRate(&rate);
        }
    }
    m_sampleRate = rate;

    /* 建缓冲：输入在前，输出在后 */
    ASIOBufferInfo *bi = m_bufInfos;
    for (long i = 0; i < m_inBufs; i++, bi++)
    {
        bi->isInput = ASIOTrue;
        bi->channelNum = i;
        bi->buffers[0] = bi->buffers[1] = 0;
    }
    for (long i = 0; i < m_outBufs; i++, bi++)
    {
        bi->isInput = ASIOFalse;
        bi->channelNum = i;
        bi->buffers[0] = bi->buffers[1] = 0;
    }
    if (ASIOCreateBuffers(m_bufInfos, m_inBufs + m_outBufs, nBuf,
                          &s_callbacks) != ASE_OK)
    {
        ASIOExit();
        m_driverName.clear();
        return false;
    }

    /* 通道格式信息（转换用） */
    for (long i = 0; i < m_inBufs + m_outBufs; i++)
    {
        m_chanInfos[i].channel = m_bufInfos[i].channelNum;
        m_chanInfos[i].isInput = m_bufInfos[i].isInput;
        if (ASIOGetChannelInfo(&m_chanInfos[i]) != ASE_OK)
        {
            memset(&m_chanInfos[i], 0, sizeof(m_chanInfos[i]));
            m_chanInfos[i].type = ASIOSTFloat32LSB;   /* 未知格式按 float 处理 */
        }
    }

    /* 转换中转缓冲 */
    m_inF.resize(m_inBufs);
    for (long i = 0; i < m_inBufs; i++)
        m_inF[i].assign(nBuf, 0.f);
    m_outF.resize(m_outBufs);
    for (long i = 0; i < m_outBufs; i++)
        m_outF[i].assign(nBuf, 0.f);
    m_inPtrs.resize(m_inBufs);
    for (long i = 0; i < m_inBufs; i++)
        m_inPtrs[i] = m_inF[i].data();
    m_outPtrs.resize(m_outBufs);
    for (long i = 0; i < m_outBufs; i++)
        m_outPtrs[i] = m_outF[i].data();

    m_bOpen = true;
    m_bRunning = false;
    s_pThis = this;
    return true;
}

/*****************************************************************************/
/* Close : 停止 + 释放                                                       */
/*****************************************************************************/
void CAsioBackend::Close()
{
    if (!m_bOpen)
        return;
    Stop();
    if (s_pThis == this)
        s_pThis = NULL;
    ASIODisposeBuffers();       /* 释放缓冲 */
    ASIOExit();                 /* 卸载驱动（asioDrivers->removeCurrentDriver） */
    m_bOpen = false;
    m_bRunning = false;
    m_driverName.clear();
    m_inF.clear();
    m_outF.clear();
    m_inPtrs.clear();
    m_outPtrs.clear();
}

/*****************************************************************************/
/* Start / Stop                                                              */
/*****************************************************************************/
bool CAsioBackend::Start()
{
    if (!m_bOpen)
        return false;
    s_pThis = this;
    if (ASIOStart() != ASE_OK)
        return false;
    m_bRunning = true;
    return true;
}

void CAsioBackend::Stop()
{
    if (!m_bOpen)
        return;
    if (ASIOStop() == ASE_OK)
    {
        s_pThis = NULL;
        m_bRunning = false;
    }
}

/*****************************************************************************/
/* ControlPanel : 驱动控制面板                                                */
/*****************************************************************************/
bool CAsioBackend::ControlPanel()
{
    if (!m_bOpen)
        return false;
    return (ASIOControlPanel() == ASE_OK);
}

/*****************************************************************************/
/* GetDspUsage : DSP 使用率（%）                                              */
/*****************************************************************************/
double CAsioBackend::GetDspUsage() const
{
    if (!m_bRunning || m_bufSize <= 0 || m_sampleRate <= 0.0)
        return 0.0;
    double periodUs = (double)m_bufSize / m_sampleRate * 1e6;
    if (periodUs <= 0.0)
        return 0.0;
    return (double)m_dspElapsedUs * 100.0 / periodUs;
}

/*****************************************************************************/
/* GetChannelName : ASIO 通道名（isInput 方向内 0-based）                     */
/*****************************************************************************/
const char *CAsioBackend::GetChannelName(int index, bool input) const
{
    int i = input ? index : (int)m_inBufs + index;
    if (i < 0 || i >= (int)(m_inBufs + m_outBufs))
        return "";
    return m_chanInfos[i].name;
}

/*===========================================================================*/
/* ASIO 回调                                                                  */
/*===========================================================================*/
ASIOTime *CAsioBackend::BufferSwitchTimeInfo(ASIOTime *timeInfo, long index,
                                             ASIOBool processNow)
{
    CAsioBackend *b = s_pThis;
    if (!b || !b->m_cb)
        return timeInfo;
    (void)processNow;

    LARGE_INTEGER qpf, t0, t1;
    QueryPerformanceFrequency(&qpf);
    QueryPerformanceCounter(&t0);

    b->m_curIndex = index;
    /* 注意：部分驱动（如 FlexASIO）在回调里 processNow 传 ASIOFalse，
       但仍期望宿主在回调线程内处理音频；为兼容一律同步处理。
       （旧 CSpecAsioHost 在 False 时也经工作线程处理） */

    /* 输入 ASIO 格式 -> float */
    for (long i = 0; i < b->m_inBufs; i++)
        b->ConvertFromASIO(i, b->m_bufInfos[i].buffers[index],
                           b->m_inF[i].data(), b->m_bufSize);

    /* 输出缓冲清零（未映射/未写通道保持静音） */
    for (long i = 0; i < b->m_outBufs; i++)
        memset(b->m_outF[i].data(), 0, b->m_bufSize * sizeof(float));

    /* 交给宿主（通道映射/插件处理在回调里做） */
    b->m_cb(b->m_ctx, b->m_inPtrs.data(), b->m_outPtrs.data(),
            (int)b->m_bufSize, (int)b->m_inBufs, (int)b->m_outBufs);

    /* float -> 输出 ASIO 格式 */
    for (long i = 0; i < b->m_outBufs; i++)
        b->ConvertToASIO(i, b->m_outF[i].data(),
                         b->m_bufInfos[b->m_inBufs + i].buffers[index],
                         b->m_bufSize);

    /* 每块处理完通知驱动输出就绪：
       FlexASIO 等驱动依赖宿主每次 OutputReady 握手才能继续下一块流转
       （第二轮 bufferSwitch 后会 wait 宿主 OutputReady，否则回调线程死锁、
         输入/输出全部停摆）。不支持 OutputReady 的驱动会静默忽略此调用。
       注意不能依赖 createBuffers 前的探测结果——此时部分驱动（如 FlexASIO
       preparedState 未建立）会返回错误，导致 m_postOutput=false 而从不握手。 */
    ASIOOutputReady();

    /* DSP 使用率 = 本块处理耗时 / 块周期 */
    QueryPerformanceCounter(&t1);
    if (qpf.QuadPart > 0)
        b->m_dspElapsedUs = (long)((double)(t1.QuadPart - t0.QuadPart)
                                   * 1e6 / (double)qpf.QuadPart);
    return timeInfo;
}

void CAsioBackend::BufferSwitch(long index, ASIOBool processNow)
{
    ASIOTime ti;
    memset(&ti, 0, sizeof(ti));
    if (ASIOGetSamplePosition(&ti.timeInfo.samplePosition,
                              &ti.timeInfo.systemTime) == ASE_OK)
        ti.timeInfo.flags = kSystemTimeValid | kSamplePositionValid;
    BufferSwitchTimeInfo(&ti, index, processNow);
}

void CAsioBackend::SampleRateChanged(ASIOSampleRate sRate)
{
    if (s_pThis)
        s_pThis->m_sampleRate = (double)sRate;
}

long CAsioBackend::AsioMessages(long selector, long value, void *message,
                                double *opt)
{
    (void)value; (void)message; (void)opt;
    switch (selector)
    {
    case kAsioSelectorSupported:
        return 1L;
    case kAsioEngineVersion:
        return 2L;
    case kAsioSupportsTimeInfo:
    case kAsioSupportsTimeCode:
        return 1L;
    default:
        return 0L;
    }
}

/*===========================================================================*/
/* 格式转换                                                                   */
/*===========================================================================*/
void CAsioBackend::ConvertFromASIO(int ch, void *src, float *dst, long frames)
{
    switch (m_chanInfos[ch].type)
    {
    case ASIOSTInt16MSB:    ReverseEndian2(src, frames); /* fallthrough */
    case ASIOSTInt16LSB:    ToFloat16(src, dst, frames); break;
    case ASIOSTInt24MSB:    ReverseEndian3(src, frames); /* fallthrough */
    case ASIOSTInt24LSB:    ToFloat24(src, dst, frames); break;
    case ASIOSTFloat32MSB:  ReverseEndian4(src, frames); /* fallthrough */
    case ASIOSTFloat32LSB:  memcpy(dst, src, frames * sizeof(float)); break;
    case ASIOSTFloat64MSB:  ReverseEndian8(src, frames); /* fallthrough */
    case ASIOSTFloat64LSB:  ToFloat64(src, dst, frames); break;
    case ASIOSTInt32MSB:    ReverseEndian4(src, frames); /* fallthrough */
    case ASIOSTInt32LSB:    ToFloat32(src, dst, 32, frames); break;
    case ASIOSTInt32MSB16:  ReverseEndian4(src, frames); /* fallthrough */
    case ASIOSTInt32LSB16:  ToFloat32(src, dst, 16, frames); break;
    case ASIOSTInt32MSB18:  ReverseEndian4(src, frames); /* fallthrough */
    case ASIOSTInt32LSB18:  ToFloat32(src, dst, 18, frames); break;
    case ASIOSTInt32MSB20:  ReverseEndian4(src, frames); /* fallthrough */
    case ASIOSTInt32LSB20:  ToFloat32(src, dst, 20, frames); break;
    case ASIOSTInt32MSB24:  ReverseEndian4(src, frames); /* fallthrough */
    case ASIOSTInt32LSB24:  ToFloat32(src, dst, 24, frames); break;
    default:
        memset(dst, 0, frames * sizeof(float));   /* 未知格式：静音 */
        break;
    }
}

void CAsioBackend::ConvertToASIO(int ch, float *src, void *dst, long frames)
{
    switch (m_chanInfos[ch].type)
    {
    case ASIOSTInt16MSB:
        FromFloat16(src, dst, frames);
        ReverseEndian2(dst, frames);
        break;
    case ASIOSTInt16LSB:
        FromFloat16(src, dst, frames);
        break;
    case ASIOSTInt24MSB:
        FromFloat24(src, dst, frames);
        ReverseEndian3(dst, frames);
        break;
    case ASIOSTInt24LSB:
        FromFloat24(src, dst, frames);
        break;
    case ASIOSTFloat32MSB:
        memcpy(dst, src, frames * sizeof(float));
        ReverseEndian4(dst, frames);
        break;
    case ASIOSTFloat32LSB:
        memcpy(dst, src, frames * sizeof(float));
        break;
    case ASIOSTFloat64MSB:
        FromFloat64(src, dst, frames);
        ReverseEndian8(dst, frames);
        break;
    case ASIOSTFloat64LSB:
        FromFloat64(src, dst, frames);
        break;
    case ASIOSTInt32MSB:
        FromFloat32(src, dst, 32, frames);
        ReverseEndian4(dst, frames);
        break;
    case ASIOSTInt32LSB:
        FromFloat32(src, dst, 32, frames);
        break;
    case ASIOSTInt32MSB16:
        FromFloat32(src, dst, 16, frames);
        ReverseEndian4(dst, frames);
        break;
    case ASIOSTInt32LSB16:
        FromFloat32(src, dst, 16, frames);
        break;
    case ASIOSTInt32MSB18:
        FromFloat32(src, dst, 18, frames);
        ReverseEndian4(dst, frames);
        break;
    case ASIOSTInt32LSB18:
        FromFloat32(src, dst, 18, frames);
        break;
    case ASIOSTInt32MSB20:
        FromFloat32(src, dst, 20, frames);
        ReverseEndian4(dst, frames);
        break;
    case ASIOSTInt32LSB20:
        FromFloat32(src, dst, 20, frames);
        break;
    case ASIOSTInt32MSB24:
        FromFloat32(src, dst, 24, frames);
        ReverseEndian4(dst, frames);
        break;
    case ASIOSTInt32LSB24:
        FromFloat32(src, dst, 24, frames);
        break;
    default:
        break;   /* 未知格式：不写 */
    }
}
