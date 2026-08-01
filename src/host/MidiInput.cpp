// MidiInput.cpp : MIDI 输入封装实现（winmm）
/******************************************************************************/
#include "MidiInput.h"

CMidiInput::CMidiInput() : m_hMidi(NULL)
{
    InitializeCriticalSection(&m_cs);
}

CMidiInput::~CMidiInput()
{
    Close();
    DeleteCriticalSection(&m_cs);
}

void CMidiInput::Close()
{
    if (m_hMidi)
    {
        midiInStop(m_hMidi);
        midiInClose(m_hMidi);
        m_hMidi = NULL;
    }
    EnterCriticalSection(&m_cs);
    m_queue.clear();
    LeaveCriticalSection(&m_cs);
}

int CMidiInput::GetDeviceCount()
{
    return (int)midiInGetNumDevs();
}

std::string CMidiInput::GetDeviceName(int idx)
{
    MIDIINCAPSW caps;
    if (midiInGetDevCapsW((UINT)idx, &caps, sizeof(caps)) != MMSYSERR_NOERROR)
        return std::string();
    char buf[256];
    WideCharToMultiByte(CP_ACP, 0, caps.szPname, -1, buf, sizeof(buf), NULL, NULL);
    return std::string(buf);
}

bool CMidiInput::Open(int deviceIndex)
{
    Close();
    if (deviceIndex < 0)
        return false;
    if (midiInOpen(&m_hMidi, (UINT)deviceIndex, (DWORD_PTR)MidiProc,
                   (DWORD_PTR)this, CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
    {
        m_hMidi = NULL;
        return false;
    }
    midiInStart(m_hMidi);
    return true;
}

bool CMidiInput::PopEvent(unsigned char *data, int &len)
{
    Ev ev;
    EnterCriticalSection(&m_cs);
    if (m_queue.empty())
    {
        LeaveCriticalSection(&m_cs);
        return false;
    }
    ev = m_queue.front();
    m_queue.erase(m_queue.begin());
    LeaveCriticalSection(&m_cs);

    for (int i = 0; i < 3; i++)
        data[i] = ev.d[i];
    len = ev.n;
    return true;
}

void CALLBACK CMidiInput::MidiProc(HMIDIIN /*h*/, UINT uMsg, DWORD_PTR dwInstance,
                                   DWORD_PTR dwParam1, DWORD_PTR /*dwParam2*/)
{
    CMidiInput *self = (CMidiInput *)dwInstance;
    if (!self)
        return;
    if (uMsg == MIM_DATA)
    {
        DWORD msg = (DWORD)dwParam1;   /* 0x00KKVVNN：NN=状态，VV=数据1，KK=数据2 */
        Ev ev;
        ev.d[0] = (unsigned char)(msg & 0xFF);
        ev.d[1] = (unsigned char)((msg >> 8) & 0xFF);
        ev.d[2] = (unsigned char)((msg >> 16) & 0xFF);
        ev.n = 3;
        EnterCriticalSection(&self->m_cs);
        if (self->m_queue.size() < 256)
            self->m_queue.push_back(ev);
        LeaveCriticalSection(&self->m_cs);
    }
}
