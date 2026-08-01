// MidiInput.h : MIDI 输入设备封装（winmm，供 ASIO 实时回调消费）
//   midiIn 回调线程把短消息放入队列；ASIO 实时回调 PopEvent 取事件喂插件。
/******************************************************************************/
#pragma once

#include <windows.h>
#include <mmsystem.h>
#include <string>
#include <vector>

class CMidiInput
{
public:
    CMidiInput();
    ~CMidiInput();

    void Close();

    // 设备枚举
    static int         GetDeviceCount();
    static std::string GetDeviceName(int idx);   // ANSI 名

    // 打开指定 MIDI 输入设备（-1 = 关闭）
    bool Open(int deviceIndex);
    bool IsOpen() const { return m_hMidi != NULL; }

    // ASIO 实时回调：取出一条 MIDI 短消息（data[0..2]，len<=3）
    bool PopEvent(unsigned char *data, int &len);

private:
    static void CALLBACK MidiProc(HMIDIIN h, UINT uMsg, DWORD_PTR dwInstance,
                                  DWORD_PTR dwParam1, DWORD_PTR dwParam2);

    HMIDIIN m_hMidi;
    CRITICAL_SECTION m_cs;
    struct Ev { unsigned char d[3]; int n; };
    std::vector<Ev> m_queue;    // FIFO（先进先出，容量限制）
};
