#pragma once

namespace depump
{

// Describes one periodic sidechain-pump gain curve. Times are the
// original compressor's behavior being modeled/undone.
struct PumpProfile
{
    float rateHz = 2.0f;      // pump cycles per second
    float depthDb = 6.0f;     // gain reduction at the dip bottom, positive dB
    float attackMs = 10.0f;   // one-pole speed of the fall into the dip
    float holdMs = 60.0f;     // how long gain is held down after the attack
                              // (the trigger signal's energy duration) —
                              // distinct from attack speed
    float releaseMs = 150.0f; // recovery time constant back toward unity
    float phase01 = 0.0f;     // dip-start offset within the cycle, 0..1
};

} // namespace depump
