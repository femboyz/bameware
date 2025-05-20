#pragma once
#define IN_ATTACK (1 << 0)

struct CUserCmd {
    int  command_number;
    int  tick_count;
    int  buttons;   // we only care about IN_ATTACK here
    // you can leave out everything else…
};