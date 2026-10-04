#include "rpgame.h"

/* --------------------------------------------------------------------- */
/* Palette                                                                */
/* --------------------------------------------------------------------- */
const uint16_t PALETTE[16] = {
    0x10A4,  /*  0 background        ( 16, 20, 32) */
    0xE75E,  /*  1 file name         (230,235,245) */
    0x7C12,  /*  2 dim / size text   (120,130,150) */
    0x2319,  /*  3 highlight bar     ( 32, 96,200) */
    0xFFFF,  /*  4 highlighted text  (255,255,255) */
    0x2967,  /*  5 header/status bg  ( 40, 44, 60) */
    0xFE8B,  /*  6 header text       (255,210, 90) */
    0xFDEA,  /*  7 directory name    (255,190, 80) */
    0xFACA,  /*  8 error text        (255, 90, 80) */
    0x3A2B,  /*  9 scrollbar track   ( 60, 70, 90) */
    0, 0, 0, 0, 0, 0
};

uint8_t scratch[SCRATCH_BYTES] __attribute__((aligned(4)));

/* --------------------------------------------------------------------- */
/* Shared SPI arbitration                                                       */
/* --------------------------------------------------------------------- */
// RPGameSD saves/restores the complete SPI configuration itself.
void spiClaimForLcd() {}
void sdBegin() { gfx_wait(); }
void sdEnd() {}

/* --------------------------------------------------------------------- */
/* Buttons                                                                */
/* --------------------------------------------------------------------- */
Btn bUp     = { PIN_BTN_UP,     false, 0, 0 };
Btn bDown   = { PIN_BTN_DOWN,   false, 0, 0 };
Btn bLeft   = { PIN_BTN_LEFT,   false, 0, 0 };
Btn bRight  = { PIN_BTN_RIGHT,  false, 0, 0 };
Btn bA      = { PIN_BTN_A,      false, 0, 0 };
Btn bB      = { PIN_BTN_B,      false, 0, 0 };
Btn bSelect = { PIN_BTN_SELECT, false, 0, 0 };
Btn bStart  = { PIN_BTN_START,  false, 0, 0 };

static Btn *const allButtons[] = {
    &bUp, &bDown, &bLeft, &bRight, &bA, &bB, &bSelect, &bStart
};
#define N_BUTTONS (sizeof(allButtons) / sizeof(allButtons[0]))

bool btnPressed(Btn &b, bool repeat)
{
    const uint32_t t = millis();
    if ((int32_t)(t - b.settleAt) < 0) return false;    /* still bouncing */

    const bool now = (digitalRead(b.pin) == LOW);

    if (now != b.down) {
        b.down     = now;
        b.settleAt = t + DEBOUNCE_MS;
        if (now) { b.nextRepeat = t + REPEAT_DELAY; return true; }
        return false;
    }
    if (now && repeat && (int32_t)(t - b.nextRepeat) >= 0) {
        b.nextRepeat = t + REPEAT_RATE;
        return true;
    }
    return false;
}

bool btnAnyDown(void)
{
    for (uint8_t i = 0; i < N_BUTTONS; i++)
        if (digitalRead(allButtons[i]->pin) == LOW) return true;
    return false;
}

void btnResetAll(void)
{
    const uint32_t t = millis();
    for (uint8_t i = 0; i < N_BUTTONS; i++) {
        Btn *b = allButtons[i];
        b->down       = (digitalRead(b->pin) == LOW);
        b->settleAt   = t + DEBOUNCE_MS;
        b->nextRepeat = t + REPEAT_DELAY;
    }
}

/* --------------------------------------------------------------------- */
/* Small helpers                                                          */
/* --------------------------------------------------------------------- */
void fmtU32(uint32_t v, char *out)
{
    char tmp[11];
    uint8_t n = 0;
    do { tmp[n++] = (char)('0' + (v % 10)); v /= 10; } while (v);
    uint8_t i = 0;
    while (n) out[i++] = tmp[--n];
    out[i] = 0;
}

bool extEquals(const char *name, const char *ext)
{
    const char *d = strrchr(name, '.');
    if (!d) return false;
    d++;
    while (*d && *ext) {
        char a = *d++, b = *ext++;
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return false;
    }
    return *d == 0 && *ext == 0;
}

void splashMessage(const char *msg, uint8_t colour)
{
    gfx_clear(C_BG);
    gfx_text((GFX_W - (int)strlen(msg) * 6) / 2, 58, msg, colour);
    gfx_flush();
}
