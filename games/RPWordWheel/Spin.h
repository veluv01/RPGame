// The wheel's motion. The rules have already drawn the stop; this works out
// a spin that ends exactly there: a number of whole turns set by the power,
// plus the way to the stop, shed along a cubic ease-out so the last pegs
// take their time. Pure integer maths, host-tested.
//
// Positions are rim pixels under the pointer, Q8, wrapping at RIM. A stop is
// a peg slot 14 px wide (pegs at its edges); a wedge is three slots.
#pragma once
#include <stdint.h>

namespace spin {

constexpr int32_t PEG = 14, WEDGE = 3 * PEG, RIM = 72 * PEG;      // px

struct Spin {
    int32_t  from;              // Q8
    int32_t  dist;              // px
    uint16_t t, dur;            // frames

    // jitter: where in the slot it rests, -4..4 px off the middle.
    void    start(int32_t posQ8, uint8_t stop, uint8_t power, int8_t jitter, bool quick);
    bool    step();             // advance a frame; false once it has stopped
    bool    moving() const { return t < dur; }
    int32_t pos() const;        // Q8, 0 <= pos < RIM << 8
    void    finish() { t = dur; }
};

}  // namespace spin
