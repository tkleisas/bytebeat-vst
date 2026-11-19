// DrumKits.h - factory pad layouts shared by Bytebeat Drums MPC and VST3.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 bokontep

#ifndef BYTEBEAT_DRUM_KITS_H
#define BYTEBEAT_DRUM_KITS_H

namespace bbdrums
{

constexpr int kPads = 16;

struct PadSpec
{
    int formula;
    float level;
    float pan;
    float tune;
    float decay;
    int choke;
    int mode;       // 0 one-shot, 1 hold
    float a, b, c, d;
};

inline PadSpec spec(int formula, float level, float pan, float tune, float decay,
                    int choke, int mode, float a = 50.0f, float b = 50.0f,
                    float c = 50.0f, float d = 50.0f)
{
    return { formula, level, pan, tune, decay, choke, mode, a, b, c, d };
}

struct KitSpec
{
    const char* name;
    const PadSpec* pads;
    float masterVolume;
    float tone;
    float drive;
};

namespace detail
{

inline const PadSpec init[kPads] = {
    spec(0,  85, -20,   0, 250, 0, 0),
    spec(3,  75,  20,   0, 180, 1, 0),
    spec(1,  80,   0,   0, 300, 0, 0),
    spec(8,  65,  30,   0, 120, 2, 0),
    spec(5,  80, -30,   0, 320, 1, 0),
    spec(6,  75,  40,   0, 220, 2, 0),
    spec(7,  70, -40,   0, 260, 0, 0),
    spec(10, 65,   0,   0, 140, 2, 0),
    spec(11, 70, -25,   0, 400, 0, 0),
    spec(13, 75,   0, -12, 450, 0, 0),
    spec(14, 70,  25,   0, 350, 0, 0),
    spec(15, 60,   0,   0, 400, 0, 0),
    spec(21, 65, -15,   0, 300, 1, 0),
    spec(22, 65,  15,   0, 300, 1, 0),
    spec(23, 60,   0,   0, 200, 2, 0),
    spec(9,  60,   0,   0, 250, 2, 0),
};

inline const PadSpec bytebeat[kPads] = {
    spec(0,  85, -10,   0, 180, 0, 0, 35, 70),
    spec(4,  75,  15,   0, 220, 1, 0, 60, 40),
    spec(2,  80,   0,   0, 250, 0, 0),
    spec(3,  70,  25,   0, 150, 2, 0),
    spec(1,  75, -25,   0, 300, 0, 0),
    spec(5,  70,  10,   0, 260, 1, 0),
    spec(6,  65,  35,   0, 190, 2, 0),
    spec(7,  65, -35,   0, 240, 0, 0),
    spec(10, 60,   0,   0, 160, 2, 0),
    spec(8,  65,  20,   0, 140, 2, 0),
    spec(9,  60, -20,   0, 220, 1, 0),
    spec(20, 60,  30,   0, 280, 0, 0),
    spec(19, 60, -30,   0, 320, 0, 0),
    spec(12, 60,   0,   0, 300, 0, 0),
    spec(16, 55,  15,   0, 400, 0, 1),
    spec(17, 55, -15,   0, 400, 0, 1),
};

inline const PadSpec metal[kPads] = {
    spec(6,  85,   0,   0, 140, 0, 0),
    spec(9,  75, -20,   0, 110, 1, 0),
    spec(8,  75,  20,   0, 160, 2, 0),
    spec(10, 70,   0,   0, 120, 2, 0),
    spec(5,  75, -30,   0, 180, 1, 0),
    spec(3,  70,  30,   0, 150, 1, 0),
    spec(2,  65,   0,   0, 200, 0, 0),
    spec(7,  65,  40,   0, 220, 0, 0),
    spec(23, 65, -40,   0, 180, 2, 0),
    spec(9,  60,   0,   0, 100, 2, 0),
    spec(22, 60,  20,   0, 200, 1, 0),
    spec(21, 60, -20,   0, 220, 1, 0),
    spec(11, 55,   0,   0, 300, 0, 1),
    spec(14, 55,  20,   0, 280, 0, 0),
    spec(18, 50,   0,   0, 350, 0, 1),
    spec(15, 50,   0,   0, 320, 0, 0),
};

inline const PadSpec acid[kPads] = {
    spec(4,  85,   0,   0, 220, 0, 0, 20, 80),
    spec(14, 75, -20,   0, 260, 0, 0),
    spec(13, 75,  20, -12, 300, 0, 0),
    spec(19, 70,   0,   0, 280, 0, 0),
    spec(22, 70, -30,   0, 240, 0, 0, 60, 30),
    spec(21, 65,  30,   0, 260, 0, 1),
    spec(11, 65,   0,   0, 400, 0, 1),
    spec(12, 60,  15,   0, 350, 0, 1),
    spec(3,  70, -15,   0, 180, 1, 0),
    spec(8,  65,   0,   0, 160, 2, 0),
    spec(6,  60,  20,   0, 200, 2, 0),
    spec(23, 60, -20,   0, 180, 2, 0),
    spec(1,  60,   0,   0, 300, 0, 0),
    spec(5,  55,  25,   0, 320, 0, 0),
    spec(7,  55, -25,   0, 300, 0, 0),
    spec(9,  55,   0,   0, 240, 0, 0),
};

inline const PadSpec ambient[kPads] = {
    spec(11, 70, -30,   0, 1800, 0, 1),
    spec(14, 65,  30,   0, 1400, 0, 1),
    spec(12, 65, -15,   0, 2000, 0, 1),
    spec(13, 70,  15, -12, 1600, 0, 1),
    spec(15, 60, -35,   0, 2200, 0, 1),
    spec(16, 60,  35,   0, 2000, 0, 1),
    spec(18, 55,   0,   0, 2400, 0, 1),
    spec(17, 55,  20,   0, 2200, 0, 1),
    spec(5,  60, -25,   0, 1200, 0, 1),
    spec(7,  55,  25,   0, 1000, 0, 1),
    spec(1,  60,   0,   0, 800, 0, 0),
    spec(0,  55,   0,   0, 900, 0, 0),
    spec(11, 55, -20,   0, 1500, 0, 1),
    spec(14, 50,  20,   0, 1200, 0, 1),
    spec(13, 55,   0, -12, 1800, 0, 1),
    spec(19, 50,   0,   0, 1000, 0, 0),
};

inline const PadSpec glitch[kPads] = {
    spec(10, 80,   0,   0, 100, 0, 0),
    spec(8,  75, -20,   0, 80, 1, 0),
    spec(2,  75,  20,   0, 120, 0, 0),
    spec(7,  70,   0,   0, 100, 2, 0),
    spec(23, 70, -30,   0, 90, 2, 0),
    spec(9,  65,  30,   0, 110, 1, 0),
    spec(3,  65,   0,   0, 130, 0, 0),
    spec(6,  60,  20,   0, 140, 2, 0),
    spec(20, 60, -20,   0, 160, 0, 0),
    spec(21, 55,   0,   0, 120, 1, 0),
    spec(22, 55,  25,   0, 110, 1, 0),
    spec(1,  55, -25,   0, 150, 0, 0),
    spec(5,  50,   0,   0, 180, 0, 0),
    spec(4,  50,  20,   0, 160, 0, 0),
    spec(11, 50, -20,   0, 200, 0, 1),
    spec(12, 45,   0,   0, 180, 0, 1),
};

} // namespace detail

inline const KitSpec* factoryKits(int& count)
{
    static const KitSpec kits[] = {
        { "Init",        detail::init,     -12.0f, 100.0f,  0.0f },
        { "Bytebeat Kit", detail::bytebeat, -12.0f,  95.0f, 15.0f },
        { "Metal Kit",   detail::metal,    -14.0f,  75.0f, 30.0f },
        { "Acid Kit",    detail::acid,     -12.0f,  85.0f, 25.0f },
        { "Ambient Kit", detail::ambient,  -14.0f,  80.0f,  0.0f },
        { "Glitch Kit",  detail::glitch,   -14.0f,  90.0f, 15.0f },
    };
    count = (int)(sizeof(kits) / sizeof(kits[0]));
    return kits;
}

} // namespace bbdrums

#endif // BYTEBEAT_DRUM_KITS_H
