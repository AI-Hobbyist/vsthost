// SingleHost.h : 单插件宿主核心（同名自动加载 / shell / 状态）
/******************************************************************************/
#pragma once

#include <string>
#include "IPlugin.h"

class CSingleHost
{
public:
    CSingleHost();
    ~CSingleHost();

    // 手动加载插件（含 WaveShell 枚举、默认内部效果器）
    bool LoadFromFile(const std::wstring &path);
    // 同名自动加载：exe 名 或 (Shell文件名)内部插件名 形式（计划书 §5.1）
    bool LoadFromExeName(const std::wstring &exePath);
    // 仅加载以便枚举内部效果器（命令行批量生成 exe 用，不初始化音频）
    bool LoadForEnum(const std::wstring &path) { return LoadPath(path); }
    // 关闭当前插件
    void Close();

    IPlugin *Get() { return m_pPlugin; }
    bool IsLoaded() const { return m_pPlugin != NULL; }
    const std::wstring &GetModulePath() const { return m_szModulePath; }
    // 状态预设名：<插件名>_<实例序号>（窗口标题同源，见 HostNaming）
    const std::wstring &GetStateBase() const { return m_szStateBase; }

    // 内部效果器（shell）
    int  GetInternalCount() const;
    bool GetInternalInfo(int idx, PluginInternalInfo *info) const;
    int  GetCurrentInternal() const;
    // 切换内部效果器；bSaveCurrent=false 用于启动直选（旧内部效果器未被使用，不落盘）
    bool SwitchInternal(int idx, bool bSaveCurrent = true);

    // .fxp 状态存取（文件 = exe 目录 + <插件名>_<实例序号>.fxp）
    void SaveStateFile() const;
    void LoadStateFile();

    // 内部效果器上次选择（<exe>.ini [shell] lastuid，跨会话恢复）
    void SaveLastUid() const;
    int  LoadLastUid() const;   // 返回内部效果器 index，-1 = 无记录/未命中

protected:
    bool LoadPath(const std::wstring &path);
    void Free();
    void UpdateStateBase();
    // 加载 shell 并切换到指定内部效果器（internalName 为空则用 last_uid/默认）
    bool LoadShellInternal(const std::wstring &shellPath, const std::wstring &internalName);
    // <exe> 同目录 <exe名>.ini（与宿主的 AsioConfigPath 同一文件）
    static std::wstring ConfigIniPath();

protected:
    IPlugin     *m_pPlugin;
    std::wstring m_szModulePath;
    std::wstring m_szStateBase;     // <插件名>_<实例序号>
};
