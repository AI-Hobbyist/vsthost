// FxBank.h : .fxb/.fxp 文件结构（移植自 CVSTHost.h）
/******************************************************************************/
#pragma once

#include <windows.h>

#include "aeffectx.h"                   // VST2 类型

#if !defined(VST_2_3_EXTENSIONS)
struct VstSpeakerArrangement;
struct VstPatchChunkInfo;
#endif

/* 结构体：与 VST SDK 的 vstfxstore.h 语义一致（参考 CVSTHost 自带实现） */
#define cMagic            'CcnK'
#define fMagic            'FxCk'
#define bankMagic         'FxBk'
#define chunkGlobalMagic  'FxCh'
#define chunkPresetMagic  'FPCh'
#define chunkBankMagic    'FBCh'

struct SFxHeader
{
    VstInt32 chunkMagic;                /* 'CcnK' */
    VstInt32 byteSize;
};

struct SFxBase : public SFxHeader
{
    VstInt32 fxMagic;                   /* 'FxCk' / 'FPCh' / 'FxBk' / 'FBCh' */
    VstInt32 version;
    VstInt32 fxID;
    VstInt32 fxVersion;
};

struct SFxProgramBase : public SFxBase
{
    VstInt32 numParams;
    char prgName[28];
};

struct SFxProgram : public SFxProgramBase
{
    float params[1];
};

struct SFxProgramChunk : public SFxProgramBase
{
    VstInt32 size;
    char chunk[1];
};

struct SFxBankBase : public SFxBase
{
    VstInt32 numPrograms;
    VstInt32 currentProgram;
    char future[124];
};

struct SFxBank : public SFxBankBase
{
    SFxProgram programs[1];
};

struct SFxBankChunk : public SFxBankBase
{
    VstInt32 size;
    char chunk[1];
};

/*****************************************************************************/
/* CFxBase / CFxBank : 移植自 CVSTHost                                        */
/*****************************************************************************/
class CFxBase
{
public:
    CFxBase() {}
protected:
    static bool NeedsBSwap;
    static void SwapBytes(float &f);
    static void SwapBytes(long &l);
    static void SwapBytes(VstInt32 &vi);
};

class CFxBank : public CFxBase
{
public:
    CFxBank(char *pszFile = 0);
    CFxBank(int nPrograms, int nParams);
    CFxBank(int nChunkSize);
    CFxBank(CFxBank const &org) { DoCopy(org); }
    virtual ~CFxBank();
    CFxBank & operator=(CFxBank const &org) { return DoCopy(org); }

    bool SetSize(int nPrograms, int nParams);
    bool SetSize(int nChunkSize);
    bool LoadBank(char *pszFile);
    bool SaveBank(char *pszFile);
    void Unload();
    bool IsLoaded() { return !!bBank; }
    bool IsChunk() { return bChunk; }

    long GetVersion()     { if (!bBank) return 0; return ((SFxBase *)bBank)->version; }
    long GetFxID()        { if (!bBank) return 0; return ((SFxBase *)bBank)->fxID; }
    void SetFxID(long id) { if (bBank) ((SFxBase *)bBank)->fxID = id; if (!bChunk) for (int i = GetNumPrograms() - 1; i >= 0; i--) GetProgram(i)->fxID = id; }
    long GetFxVersion()   { if (!bBank) return 0; return ((SFxBase *)bBank)->fxVersion; }
    void SetFxVersion(long v) { if (bBank) ((SFxBase *)bBank)->fxVersion = v; if (!bChunk) for (int i = GetNumPrograms() - 1; i >= 0; i--) GetProgram(i)->fxVersion = v; }
    long GetNumPrograms() { if (!bBank) return 0; return ((SFxBankBase *)bBank)->numPrograms; }
    long GetNumParams()   { if (bChunk) return 0; return GetProgram(0)->numParams; }
    long GetChunkSize()   { if (!bChunk) return 0; return ((SFxBankChunk *)bBank)->size; }
    void *GetChunk()      { if (!bChunk) return 0; return ((SFxBankChunk *)bBank)->chunk; }
    bool SetChunk(void *chunk) { if (!bChunk) return false; memcpy(((SFxBankChunk *)bBank)->chunk, chunk, ((SFxBankChunk *)bBank)->size); return true; }

    SFxProgram *GetProgram(int nProgNum);

    char *GetProgramName(int nProgram)
    {
        SFxProgram *p = GetProgram(nProgram);
        if (!p) return NULL;
        return p->prgName;
    }
    float GetProgParm(int nProgram, int nParm)
    {
        SFxProgram *p = GetProgram(nProgram);
        if (!p || nParm > p->numParams) return 0;
        return p->params[nParm];
    }
    bool SetProgParm(int nProgram, int nParm, float val = 0.0f)
    {
        SFxProgram *p = GetProgram(nProgram);
        if (!p || nParm > p->numParams) return false;
        if (val < 0.0f) val = 0.0f;
        if (val > 1.0f) val = 1.0f;
        p->params[nParm] = val;
        return true;
    }

protected:
    char szFileName[256];
    unsigned char *bBank;
    int  nBankLen;
    bool bChunk;

    void Init();
    CFxBank &DoCopy(CFxBank const &org);
};
