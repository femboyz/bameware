#pragma once

//#include "SDK/SDK.h"
#include <Windows.h>
#include "SDK/fake_usercmd.h"   // brings in CUserCmd, IN_ATTACK, etc.


namespace FakeLag {
    // on/off switch
    extern bool    enable;
    // how many ticks to choke
    extern int     lagTicks;
    // key to toggle
    extern int     toggleKey;

    // called each CreateMove; flips enable on keypress and updates sendPacket
    // `sendPacket` is the ref out to the engine so it knows whether to transmit this cmd
    void CreateMove(CUserCmd* cmd, bool& sendPacket);
}
