#include "fakelag.h"
#include <Windows.h>


using namespace FakeLag;

// storage for state
bool FakeLag::enable = false;
int  FakeLag::lagTicks = 16;
int  FakeLag::toggleKey = VK_END;   // you can change this default

static int tickCounter = 0;

void FakeLag::CreateMove(CUserCmd* cmd, bool& sendPacket)
{
    // 1) toggle on key-down
    static bool keyWasDown = false;
    if (GetAsyncKeyState(toggleKey) & 0x8000)
    {
        if (!keyWasDown) enable = !enable;
        keyWasDown = true;
    }
    else { keyWasDown = false; }

    // 2) if disabled, always send
    if (!enable)
    {
        sendPacket = true;
        tickCounter = 0;
        return;
    }

    // 3) if shooting, always send immediately
    if (cmd->buttons & IN_ATTACK)
    {
        sendPacket = true;
        tickCounter = 0;
        return;
    }

    // 4) choke logic
    if (tickCounter >= lagTicks)
    {
        sendPacket = true;
        tickCounter = 0;
    }
    else
    {
        sendPacket = false;
        tickCounter++;
    }
}
