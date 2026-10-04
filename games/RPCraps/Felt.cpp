// The craps layout printed on the felt (Felt.h), drawn from Zones' data:
// each spot's outline, lettering and dice; and the cursor's frame.
#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
#include <RPGame.h>
#include "Felt.h"
#include "Layout.h"
#include "Zones.h"
#include "Chips.h"
#include "Craps.h"
#include "src/assets/Assets.h"

namespace felt {

using namespace lay;

static void centred35(int cx, int y, const char *s, uint8_t c) { text35(cx - text35Width(s) / 2, y, s, c); }
static void centred57(int cx, int y, const char *s, uint8_t c) { gfx_text(cx - gfx_textWidth(s) / 2, y, s, c); }

// Two dice side by side, the way the centre of a real layout pictures its bets.
static void pair(int x, int y, uint8_t a, uint8_t b) {
    art::dieFace(x, y, 5, a);
    art::dieFace(x + 6, y, 5, b);
}

static void spot(const Craps &g, const Zone &z, bool beginner) {
    int x = z.x, y = z.y, w = z.w, h = z.h, cx = x + w / 2;
    // Printed outline (neighbours share their edges).
    if (z.bet < CODDS4) gfx_rect(x, y, w, h, FELT_LT);
    bool closed = !g.onTable(z.bet);
    switch (z.bet) {
        case PLACE4: case PLACE5: case PLACE6: case PLACE8: case PLACE9: case PLACE10: {
            static const char *const NUM[6] = {"4", "5", "6", "8", "9", "10"};
            centred57(cx, y + 2, NUM[z.bet - PLACE4], closed ? FELT_DK : GOLD);
            if (closed) dither(x + 1, y + 1, w - 2, h - 2, FELT_DK, 0);
            break;
        }
        case COME:
            centred57(x + 36, y + 1, "COME", WHITE);
            break;
        case FIELD:
            // One row: (2) 3 4 9 10 11 FIELD (12), the 2 and 12 ringed in gold
            // (they pay more); chips sit under the white numbers.
            sprite4(FIELD_PRINT, beginner ? x + 20 : x + 1, beginner ? y + 1 : y + 1);
            break;
        case DONT:
            text35(x + 3, y + (beginner ? 3 : 2), "DONT PASS BAR", WHITE);
            if (beginner) pair(x + 57, y + 3, 6, 6);
            break;
        case DONT_ODDS: text35(x + 2, y + 2, "LAY", FELT_LT); break;
        case PASS: gfx_text(x + 3, y + (beginner ? 3 : 2), "PASS LINE", WHITE); break;
        case PASS_ODDS: text35(x + 2, y + 2, "ODDS", FELT_LT); break;
        case ANY7:
            fillRound(x + 2, y + 2, w - 4, h - 4, 2, RED);
            text35(x + 5, y + 3, "ANY 7", WHITE);
            break;
        case HARD4: case HARD6: case HARD8: case HARD10: {
            uint8_t v = (uint8_t)(2 + (z.bet - HARD4));
            pair(x + 3, y + 2, v, v);
            break;
        }
        case YO:
            pair(x + 3, y + 3, 5, 6);
            text35(x + 17, y + 3, "YO", GOLD);
            break;
        case ANYCRAPS:
            text35(x + 2, y + 2, "ANY CRAPS", GOLD);
            break;
        default: break;
    }
}

void draw(const Craps &g) {
    bool beginner = g.opt.table == TABLE_BEGINNER;
    gfx_fillRect(0, FELT_Y, 128, TRIM_Y - FELT_Y, FELT);
    uint8_t n = zones::count(g.opt.table);
    for (uint8_t i = 0; i < n; i++) {
        const Zone &z = zones::at(g.opt.table, i);
        if (z.bet < Z_CHIP0) spot(g, z, beginner);
    }
    if (!beginner) gfx_vline(90, FELT_Y, TRIM_Y - FELT_Y, GOLD);   // the centre's frame
    gfx_hline(0, TRIM_Y, 128, GOLD);
}

void hover(uint8_t table, uint8_t i, uint8_t colour) {
    const Zone &z = zones::at(table, i);
    if (z.bet >= Z_CHIP0) return;                        // the bar draws its own
    roundRect(z.x + 1, z.y + 1, z.w - 2, z.h - 2, 2, colour);
}

}  // namespace felt
