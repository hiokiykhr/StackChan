#pragma once

#include <cstdint>

namespace hermes_buddy {

enum class HermesChanMood : uint8_t {
    Sleep,
    Idle,
    Busy,
    Attention,
    Celebrate,
    Heart,
    Error,
};

void draw_hermes_chan(int cx, int cy, HermesChanMood mood, uint32_t tick, bool compact = false);

}  // namespace hermes_buddy
