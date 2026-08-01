// FxBank.cpp : .fxb/.fxp 文件读写（移植自 CVSTHost.cpp）
/******************************************************************************/
#include "FxBank.h"

#include <stdio.h>
#include <string.h>
#include <stddef.h>

const int MyVersion = 1;                /* 已知最高 VST FX 版本             */

/* 设置字节序交换标志 */
static char szChnk[] = "CcnK";
static long lChnk = 'CcnK';
bool CFxBase::NeedsBSwap = !!memcmp(szChnk, &lChnk, 4);

void CFxBase::SwapBytes(long &l)
{
    unsigned char *b = (unsigned char *)&l;
    long intermediate = ((long)b[0] << 24) | ((long)b[1] << 16) |
                        ((long)b[2] << 8) | (long)b[3];
    l = intermediate;
}

void CFxBase::SwapBytes(float &f)
{
    long *pl = (long *)&f;
    SwapBytes(*pl);
}

void CFxBase::SwapBytes(VstInt32 &vi)
{
    unsigned char *b = (unsigned char *)&vi;
    VstInt32 intermediate = ((VstInt32)b[0] << 24) | ((VstInt32)b[1] << 16) |
                            ((VstInt32)b[2] << 8) | (VstInt32)b[3];
    vi = intermediate;
}

CFxBank::CFxBank(char *pszFile)
{
    Init();
    if (pszFile)
        LoadBank(pszFile);
}

CFxBank::CFxBank(int nPrograms, int nParams)
{
    Init();
    SetSize(nPrograms, nParams);
}

CFxBank::CFxBank(int nChunkSize)
{
    Init();
    SetSize(nChunkSize);
}

void CFxBank::Init()
{
    bBank = NULL;
    Unload();
}

CFxBank::~CFxBank()
{
    Unload();
}

CFxBank &CFxBank::DoCopy(const CFxBank &org)
{
    unsigned char *nBank = NULL;
    if (org.nBankLen)
    {
        nBank = new unsigned char[org.nBankLen];
        if (!nBank)
            throw (int)1;
        memcpy(nBank, org.bBank, org.nBankLen);
    }
    Unload();
    bBank = nBank;
    bChunk = org.bChunk;
    nBankLen = org.nBankLen;
    strcpy(szFileName, org.szFileName);
    return *this;
}

bool CFxBank::SetSize(int nPrograms, int nParams)
{
    int nTotLen = sizeof(SFxBankBase);
    int nProgLen = sizeof(SFxProgramBase) + nParams * sizeof(float);
    nTotLen += nPrograms * nProgLen;
    unsigned char *nBank = new unsigned char[nTotLen];
    if (!nBank)
        return false;

    Unload();
    bBank = nBank;
    nBankLen = nTotLen;
    bChunk = false;

    memset(nBank, 0, nTotLen);
    SFxBank *pBank = (SFxBank *)bBank;
    pBank->chunkMagic = cMagic;
    pBank->byteSize = 0;
    pBank->fxMagic = bankMagic;
    pBank->version = MyVersion;
    pBank->numPrograms = nPrograms;

    unsigned char *bProg = (unsigned char *)pBank->programs;
    for (int i = 0; i < nPrograms; i++)
    {
        SFxProgram *pProg = (SFxProgram *)(bProg + i * nProgLen);
        pProg->chunkMagic = cMagic;
        pProg->byteSize = 0;
        pProg->fxMagic = fMagic;
        pProg->version = 1;
        pProg->numParams = nParams;
        for (int j = 0; j < nParams; j++)
            pProg->params[j] = 0.f;
    }
    return true;
}

bool CFxBank::SetSize(int nChunkSize)
{
    int nTotLen = (int)offsetof(SFxBankChunk, chunk) + nChunkSize;
    unsigned char *nBank = new unsigned char[nTotLen];
    if (!nBank)
        return false;

    Unload();
    bBank = nBank;
    nBankLen = nTotLen;
    bChunk = true;

    memset(nBank, 0, nTotLen);
    SFxBankChunk *pBank = (SFxBankChunk *)bBank;
    pBank->chunkMagic = cMagic;
    pBank->fxMagic = chunkBankMagic;
    pBank->version = MyVersion;
    pBank->numPrograms = 1;
    pBank->size = nChunkSize;
    return true;
}

bool CFxBank::LoadBank(char *pszFile)
{
    FILE *fp = fopen(pszFile, "rb");
    if (!fp)
        return false;
    bool brc = true;
    unsigned char *nBank = NULL;
    try
    {
        fseek(fp, 0, SEEK_END);
        size_t tLen = (size_t)ftell(fp);
        rewind(fp);

        nBank = new unsigned char[tLen];
        if (!nBank)
            throw (int)1;
        if (fread(nBank, 1, tLen, fp) != tLen)
            throw (int)1;

        SFxBankBase *pBank = (SFxBankBase *)nBank;
        if (NeedsBSwap)
        {
            SwapBytes(pBank->chunkMagic);
            SwapBytes(pBank->byteSize);
            SwapBytes(pBank->fxMagic);
            SwapBytes(pBank->version);
            SwapBytes(pBank->fxID);
            SwapBytes(pBank->fxVersion);
            SwapBytes(pBank->numPrograms);
        }
        if ((pBank->chunkMagic != cMagic) ||
            (pBank->version > MyVersion) ||
            ((pBank->fxMagic != bankMagic) && (pBank->fxMagic != chunkBankMagic)))
            throw (int)1;

        if (pBank->fxMagic == bankMagic)
        {
            SFxProgram *pProg = ((SFxBank *)pBank)->programs;
            int nProg = 0;
            while (nProg < pBank->numPrograms)
            {
                if (NeedsBSwap)
                {
                    SwapBytes(pProg->chunkMagic);
                    SwapBytes(pProg->byteSize);
                    SwapBytes(pProg->fxMagic);
                    SwapBytes(pProg->version);
                    SwapBytes(pProg->fxID);
                    SwapBytes(pProg->fxVersion);
                    SwapBytes(pProg->numParams);
                }
                if ((pProg->chunkMagic != cMagic) || (pProg->fxMagic != fMagic))
                    throw (int)1;
                if (NeedsBSwap)
                    for (int j = 0; j < pProg->numParams; j++)
                        SwapBytes(pProg->params[j]);

                unsigned char *pNext = (unsigned char *)pProg;
                pNext += sizeof(SFxProgramBase);
                pNext += (sizeof(float) * pProg->numParams);
                if (pNext > nBank + tLen)
                    throw (int)1;

                pProg = (SFxProgram *)pNext;
                nProg++;
            }
        }
        else if (pBank->fxMagic == chunkBankMagic)
        {
            if (NeedsBSwap)
            {
                SFxBankChunk *pChunk = (SFxBankChunk *)pBank;
                SwapBytes(pChunk->size);
                if (pChunk->size + ((size_t)((SFxBankChunk *)0)->chunk) > tLen)
                    throw (int)1;
            }
        }

        Unload();
        bBank = nBank;
        nBankLen = (int)tLen;
        bChunk = (pBank->fxMagic == chunkBankMagic);
    }
    catch (...)
    {
        brc = false;
        if (nBank)
            delete[] nBank;
    }
    fclose(fp);
    return brc;
}

bool CFxBank::SaveBank(char *pszFile)
{
    if (!IsLoaded())
        return false;

    unsigned char *nBank = new unsigned char[nBankLen];
    if (!nBank)
        return false;
    memcpy(nBank, bBank, nBankLen);

    SFxBankBase *pBank = (SFxBankBase *)nBank;
    int numPrograms = pBank->numPrograms;
    if (NeedsBSwap)
    {
        SwapBytes(pBank->chunkMagic);
        SwapBytes(pBank->byteSize);
        SwapBytes(pBank->fxMagic);
        SwapBytes(pBank->version);
        SwapBytes(pBank->fxID);
        SwapBytes(pBank->fxVersion);
        SwapBytes(pBank->numPrograms);
    }
    if (bChunk)
    {
        if (NeedsBSwap)
            SwapBytes(((SFxBankChunk *)pBank)->size);
    }
    else
    {
        SFxProgram *pProg = ((SFxBank *)pBank)->programs;
        int numParams = pProg->numParams;
        int nProg = 0;
        while (nProg < numPrograms)
        {
            if (NeedsBSwap)
            {
                SwapBytes(pProg->chunkMagic);
                SwapBytes(pProg->byteSize);
                SwapBytes(pProg->fxMagic);
                SwapBytes(pProg->version);
                SwapBytes(pProg->fxID);
                SwapBytes(pProg->fxVersion);
                SwapBytes(pProg->numParams);
                for (int j = 0; j < numParams; j++)
                    SwapBytes(pProg->params[j]);
            }
            unsigned char *pNext = (unsigned char *)pProg;
            pNext += sizeof(SFxProgramBase);
            pNext += (sizeof(float) * numParams);
            if (pNext > nBank + nBankLen)
                break;
            pProg = (SFxProgram *)pNext;
            nProg++;
        }
    }

    bool brc = true;
    FILE *fp = NULL;
    try
    {
        fp = fopen(pszFile, "wb");
        if (!fp)
            throw (int)1;
        if (fwrite(nBank, 1, nBankLen, fp) != (size_t)nBankLen)
            throw (int)1;
    }
    catch (...)
    {
        brc = false;
    }
    if (fp)
        fclose(fp);
    delete[] nBank;
    return brc;
}

void CFxBank::Unload()
{
    if (bBank)
        delete[] bBank;
    *szFileName = '\0';
    bBank = NULL;
    nBankLen = 0;
    bChunk = false;
}

SFxProgram *CFxBank::GetProgram(int nProgNum)
{
    if ((!IsLoaded()) || (bChunk))
        return NULL;

    SFxBank *pBank = (SFxBank *)bBank;
    SFxProgram *pProg = pBank->programs;
    int nProgLen = sizeof(SFxProgramBase) + pProg->numParams * sizeof(float);
    unsigned char *pThatProg = ((unsigned char *)pProg) + (nProgNum * nProgLen);
    pProg = (SFxProgram *)pThatProg;
    return pProg;
}
