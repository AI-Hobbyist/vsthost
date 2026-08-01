// MidiOutput.cpp : MIDI 输出封装实现（winmm）
/******************************************************************************/
#include "MidiOutput.h"

CMidiOutput::CMidiOutput() : m_hMidi(NULL)
{
}

CMidiOutput::~CMidiOutput()
{
    Close();
}

void CMidiOutput::Close()
{
    if (m_hMidi)
    {
        midiOutClose(m_hMidi);
        m_hMidi = NULL;
    }
}

int CMidiOutput::GetDeviceCount()
{
    return (int)midiOutGetNumDevs();
}

std::string CMidiOutput::GetDeviceName(int idx)
{
    MIDIOUTCAPSW caps;
    if (midiOutGetDevCapsW((UINT)idx, &caps, sizeof(caps)) != MMSYSERR_NOERROR)
        return std::string();
    char buf[256];
    WideCharToMultiByte(CP_ACP, 0, caps.szPname, -1, buf, sizeof(buf), NULL, NULL);
    return std::string(buf);
}

bool CMidiOutput::Open(int deviceIndex)
{
    Close();
    if (deviceIndex < 0)
        return false;
    if (midiOutOpen(&m_hMidi, (UINT)deviceIndex, 0, 0, 0) != MMSYSERR_NOERROR)
    {
        m_hMidi = NULL;
        return false;
    }
    return true;
}

void CMidiOutput::SendEvent(const unsigned char *d, int len)
{
    if (!m_hMidi || len < 1)
        return;
    DWORD msg = d[0];
    if (len > 1) msg |= ((DWORD)d[1]) << 8;
    if (len > 2) msg |= ((DWORD)d[2]) << 16;
    midiOutShortMsg(m_hMidi, msg);
}
