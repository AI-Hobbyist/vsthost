// CEffect.cpp : VST2 插件封装实现（移植自 CVSTHost.cpp，去除多插件宿主依赖）
/******************************************************************************/
#include "CEffect.h"

#include <stdio.h>
#include <string.h>

/*****************************************************************************/
/* CEffect : 构造                                                            */
/*****************************************************************************/
CEffect::CEffect()
{
    pEffect = NULL;
    sName = NULL;
    bEditOpen = false;
    bNeedIdle = false;
    bInEditIdle = false;
    bWantMidi = false;
    bInSetProgram = false;
    nUniqueId = 0;
    hModule = NULL;
    sDir = NULL;
    m_pAudioMaster = 0;
}

/*****************************************************************************/
/* ~CEffect : 析构                                                           */
/*****************************************************************************/
CEffect::~CEffect()
{
    Unload();
}

/*****************************************************************************/
/* Load : 加载插件模块                                                        */
/*****************************************************************************/
bool CEffect::Load(const char *name, audioMasterCallback cb)
{
    m_pAudioMaster = cb;

    Unload();                           /* 确保未加载其他模块                */

    AEffect *(*pMain)(audioMasterCallback) = 0;

    try
    {
        hModule = ::LoadLibraryA(name); /* 加载 DLL                          */
    }
    catch (...)
    {
        hModule = NULL;
    }

    if (hModule)
    {
        pMain = (AEffect * (*)(audioMasterCallback))
                ::GetProcAddress(hModule, "VSTPluginMain");
        if (!pMain)
            pMain = (AEffect * (*)(audioMasterCallback))
                    ::GetProcAddress(hModule, "main");
    }

    if (pMain)
    {
        try
        {
            pEffect = pMain(cb);
        }
        catch (...)
        {
            pEffect = NULL;
        }
    }

    /* 检查返回结构是否正确 */
    if (pEffect && (pEffect->magic != kEffectMagic))
        pEffect = NULL;

    if (pEffect)
    {
        sName = new char[strlen(name) + 1];
        if (sName)
            strcpy(sName, name);

        const char *p = strrchr(name, '\\');
        if (p)
        {
            sDir = new char[p - name + 1];
            if (sDir)
            {
                memcpy(sDir, name, p - name);
                sDir[p - name] = '\0';
            }
        }
    }

    return !!pEffect;
}

/*****************************************************************************/
/* Unload : 卸载插件模块                                                      */
/*****************************************************************************/
bool CEffect::Unload()
{
    EffClose();                         /* 确保已关闭                        */
    pEffect = NULL;

    if (hModule)
    {
        ::FreeLibrary(hModule);
        hModule = NULL;
    }
    if (sDir)
    {
        delete[] sDir;
        sDir = NULL;
    }
    if (sName)
    {
        delete[] sName;
        sName = NULL;
    }
    m_pAudioMaster = 0;
    return true;
}

/*****************************************************************************/
/* LoadBank : 加载 .fxb 文件（若适用于本效果器）                              */
/*****************************************************************************/
bool CEffect::LoadBank(char *name)
{
    try
    {
        CFxBank fx(name);
        if (!fx.IsLoaded())
            throw (int)1;
    }
    catch (...)
    {
        return false;
    }
    return true;
}

/*****************************************************************************/
/* SaveBank : 保存 .fxb 文件                                                  */
/*****************************************************************************/
bool CEffect::SaveBank(char *name)
{
    return false;                       /* 暂未实现                          */
}

/*****************************************************************************/
/* OnGetDirectory : 返回插件所在目录                                          */
/*****************************************************************************/
void *CEffect::OnGetDirectory()
{
    return sDir;
}

/*****************************************************************************/
/* EffDispatch : 调用插件 dispatcher                                          */
/*****************************************************************************/
long CEffect::EffDispatch(long opCode, long index, VstIntPtr value, void *ptr, float opt)
{
    if (!pEffect)
        return 0;
    return (long)pEffect->dispatcher(pEffect, opCode, index, value, ptr, opt);
}

/*****************************************************************************/
/* EffProcess : 调用插件 process()                                            */
/*****************************************************************************/
void CEffect::EffProcess(float **inputs, float **outputs, long sampleframes)
{
    if (!pEffect)
        return;
    pEffect->process(pEffect, inputs, outputs, sampleframes);
}

/*****************************************************************************/
/* EffProcessReplacing : 调用插件 processReplacing()                          */
/*****************************************************************************/
void CEffect::EffProcessReplacing(float **inputs, float **outputs, long sampleframes)
{
    if ((!pEffect) || (!(pEffect->flags & effFlagsCanReplacing)))
        return;
    pEffect->processReplacing(pEffect, inputs, outputs, sampleframes);
}

/*****************************************************************************/
/* EffProcessDoubleReplacing : 调用插件 processDoubleReplacing()              */
/*****************************************************************************/
void CEffect::EffProcessDoubleReplacing(double **inputs, double **outputs, long sampleFrames)
{
    if ((!pEffect) || (!(pEffect->flags & effFlagsCanDoubleReplacing)))
        return;
#if defined(VST_2_4_EXTENSIONS)
    pEffect->processDoubleReplacing(pEffect, inputs, outputs, sampleFrames);
#endif
}

/*****************************************************************************/
/* EffSetParameter / EffGetParameter : 参数读写                               */
/*****************************************************************************/
void CEffect::EffSetParameter(long index, float parameter)
{
    if (!pEffect)
        return;
    pEffect->setParameter(pEffect, index, parameter);
}

float CEffect::EffGetParameter(long index)
{
    if (!pEffect)
        return 0.f;
    return pEffect->getParameter(pEffect, index);
}
