#pragma GCC optimize("Os", "no-ipa-sra", "no-inline-functions-called-once", "no-jump-tables", "no-guess-branch-probability")   // cold code: size over speed
// The wheel's motion (Spin.h).
#include "Spin.h"

namespace spin {

void Spin::start(int32_t posQ8, uint8_t stop, uint8_t power, int8_t jitter, bool quick) {
    int32_t cur = (posQ8 >> 8) % RIM;
    if (cur < 0) cur += RIM;
    from = cur << 8;
    int32_t target = stop * PEG + PEG / 2 + jitter;
    int32_t way = (target - cur) % RIM;
    if (way < 0) way += RIM;
    dist = (1 + power / 86) * RIM + way;                // one to three turns, then the way there
    uint16_t d = (uint16_t)(150 + power * 90 / 255);
    dur = quick ? (uint16_t)(d * 2 / 3) : d;
    t = 0;
}

bool Spin::step() {
    if (t < dur) t++;
    return t < dur;
}

int32_t Spin::pos() const {
    // left = dist * (1 - t/dur)^3, with the fraction in Q15.
    int32_t u = dur ? (int32_t)(((uint32_t)(dur - t) << 15) / dur) : 0;
    int32_t u3 = (((u * u) >> 15) * u) >> 15;
    int32_t left = (dist * u3) >> 7;                    // Q8
    int32_t p = (from + (dist << 8) - left) % (RIM << 8);
    return p;
}

}  // namespace spin
