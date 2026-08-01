// loudness_std.h : 响度标准定义（含平台参考标准与真峰值上限）
//   name        下拉显示名
//   refLufs     响度参考线（LUFS，EBU 显示为 0 LU）
//   unit        0=LUFS 1=LU(EBU R128) 2=LKFS(ATSC)
//   tpLimit     真峰值上限（dBTP，超限红区起点）
/******************************************************************************/
#pragma once

struct LoudnessStdDef
{
    const wchar_t *name;
    double refLufs;
    int    unit;
    double tpLimit;     // dBTP
};

inline const LoudnessStdDef g_loudnessStds[] =
{
    { L"ITU-R BS.1770-4 (LUFS)",      -24.0, 0, -1.0 },
    { L"EBU R128 (LU)",               -23.0, 1, -1.0 },
    { L"ATSC A/85 (LKFS)",            -24.0, 2, -2.0 },
    { L"Bilibili 哔哩哔哩 (-16)",     -16.0, 0, -1.0 },
    { L"YouTube (-14)",               -14.0, 0, -1.0 },
    { L"抖音 / TikTok (-14)",         -14.0, 0, -1.0 },
    { L"Apple Music (-16)",           -16.0, 0, -1.0 },
    { L"Spotify (-14)",               -14.0, 0, -1.0 },
    { L"QQ / 网易云音乐 (-14)",       -14.0, 0, -1.0 },
    { L"电影院线 Cinema (-27)",       -27.0, 0, -0.1 },
};

inline const int g_loudnessStdCount =
    (int)(sizeof(g_loudnessStds) / sizeof(g_loudnessStds[0]));
