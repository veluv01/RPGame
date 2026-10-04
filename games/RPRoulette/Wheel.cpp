#pragma GCC optimize("Os")   // cold code: size over speed
// The wheels (Wheel.h). The pocket orders are tools/wheel.py's, generated
// into src/assets/WheelMap.h with the rotor's map.
#include "Wheel.h"
#include "src/assets/WheelMap.h"

namespace wheel {

uint8_t numberAt(uint8_t index, bool us) { return us ? US_ORDER[index] : EU_ORDER[index]; }

uint8_t indexOf(uint8_t number, bool us) {
    uint8_t i = 0;
    while (i < pockets(us) - 1 && numberAt(i, us) != number) i++;
    return i;
}

// Red: the odd numbers in 1..10 and 19..28, the even ones in 11..18 and
// 29..36 - i.e. odd, flipped in the second half of each 18.
Colour colour(uint8_t n) {
    if (n == 0 || n >= N00) return GREEN;
    return ((n & 1) != ((n - 1) % 18 >= 10)) ? RED_NUM : BLACK_NUM;
}

char *name(char *p, uint8_t n) {
    if (n >= N00) *p++ = '0', n = 0;
    else if (n >= 10) *p++ = (char)('0' + n / 10), n %= 10;
    *p++ = (char)('0' + n);
    *p = 0;
    return p;
}

}  // namespace wheel
