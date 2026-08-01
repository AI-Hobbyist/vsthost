// MidiOutput.h : MIDI 输出设备封装（winmm，供 ASIO 等无自带 MIDI 的后端使用）
//   midiOutOpen 打开输出设备；实时回调 SendEvent 发送短消息（插件 MIDI 输出事件）
/******************************************************************************/
#pragma once

#include <windows.h>
#include <mmsystem.h>
#include <string>

class CMidiOutput
{
public:
    CMidiOutput();
    ~CMidiOutput();

    void Close();

    // 设备枚举
    static int         GetDeviceCount();
    static std::string GetDeviceName(int idx);   // ANSI 名

    // 打开指定 MIDI 输出设备（-1 = 关闭）
    bool Open(int deviceIndex);
    bool IsOpen() const { return m_hMidi != NULL; }

    // 实时线程：发送一条 MIDI 短消息（len <= 3，如 NoteOn/CC/弯音）
    void SendEvent(const unsigned char *d, int len);

private:
    HMIDIOUT m_hMidi;
};
