// HostNaming.h : 宿主命名（窗口标题 / JACK 客户端名 / Shell exe 命名共用）
/******************************************************************************/
#pragma once

#include <string>

/* 当前 exe 文件名主干（不含扩展名） */
std::wstring GetExeBaseName();

/* 实例序号（1 起）：同名 exe 宿主进程中，创建时间更早的数量 + 1 */
int GetInstanceOrdinal();

/* 按基础名 + 递增序号生成实例名：<base>_<ordinal> */
std::wstring ComputeInstanceName(const std::wstring &base);

/* 宿主展示名 / JACK 客户端名：<exe主干>_<实例序号> */
std::wstring ComputeHostName();

/* ANSI -> 宽字符（插件内部效果器名转 wstring，CP_ACP） */
std::wstring AnsiToWide(const std::string &a);

/* 过滤文件名非法字符 \/:*?"<>| -> '_' */
std::wstring SanitizeFileName(const std::wstring &name);

/* 生成 Shell 直选 exe 名："(Shell文件名主干)内部效果器名.exe"（已过滤非法字符） */
std::wstring MakeShellExeName(const std::wstring &shellBase, const std::wstring &internalName);
