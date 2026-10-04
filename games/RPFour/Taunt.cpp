// The dealer's lines, by kind, and picking one (see Taunt.h).
#pragma GCC optimize("Os", "no-ipa-sra")
#include "Taunt.h"

namespace taunt {

// A kind's lines, '|' between them (at most eight of a kind).
struct Pool { const char *lines; uint8_t face; };

static const Pool POOL[KINDS] = {
    /* START */       {"WELCOME TO\nTHE TABLE.|GOOD LUCK!|A PLEASURE\nTO PLAY YOU.|TAKE YOUR\nTIME. ENJOY!|"
                       "SHALL WE\nBEGIN?", SMILE},
    /* WIN_NOW */     {"PARDON ME.\nTHAT MAKES\nFOUR.|IF YOU'LL\nPERMIT ME...|FOUR FOR THE\nHOUSE, I'M\nAFRAID.|"
                       "MY APOLOGIES\nTHIS ONE IS\nMINE.", TALK},
    /* GIFT */        {"THAT DISC\nOPENED THE\nCELL ABOVE.|MIND WHAT A\nDISC LETS ME\nPLAY ON TOP.|"
                       "CHECK ABOVE\nYOUR DISC\nNEXT TIME.", RAISED},
    /* UNBLOCKED */   {"I HAD THREE\nIN A ROW\nTHERE.|WATCH FOR\nMY THREES.|THAT ONE\nNEEDED A\nBLOCK.|"
                       "COUNT MY\nDISCS EACH\nTURN.", RAISED},
    /* FORCED */      {"I SEE A WIN\nIN # MOVES.\nLOOK CLOSE.|CAREFUL: I\nCAN FORCE IT\nIN # MOVES.|"
                       "# MOVES TO\nMY FOUR. CAN\nYOU SEE IT?", RAISED},
    /* MUST_BLOCK */  {"GOOD THREAT!\nI MUST\nBLOCK.|NICELY SET\nUP. BLOCKED.|WELL TRIED!\nI SAW IT.|"
                       "A FINE\nATTACK.\nBLOCKED.|YOU KEEP ME\nBUSY!", SMILE},
    /* MISSED */      {"YOU HAD A\nFOUR THERE!\nLOOK AGAIN.|A WIN WAS\nOPEN. CHECK\nYOUR THREES.|"
                       "SO CLOSE!\nYOU COULD\nHAVE WON.", SURPRISED},
    /* BLOCKED */     {"WELL\nSPOTTED!|GOOD BLOCK.|SHARP EYES!|NICELY\nDEFENDED.|YOU SAW MY\nTHREE. GOOD!|"
                       "EXACTLY\nRIGHT.", SMILE},
    /* DOUBLE */      {"TWO THREATS\nAT ONCE.\nSUPERB!|I CAN ONLY\nBLOCK ONE.\nBRILLIANT!|A PERFECT\nTRAP. WELL\nDONE!", SURPRISED},
    /* LOSING */      {"YOU HAVE ME.\nWELL\nPLAYED!|I SEE NO WAY\nOUT. BRAVO!|YOU ARE\nWINNING.\nKEEP GOING!", SMILE},
    /* TRAP */        {"I HAVE TWO\nTHREATS NOW.\nI'M SORRY!|TWO WAYS TO\nFOUR. WATCH\nFOR THESE.|"
                       "THAT IS A\nFORK. A GOOD\nONE TO LEARN", TALK},
    /* THREAT */      {"CAREFUL: I\nHAVE THREE.|A BLOCK IS\nNEEDED NOW.|DO YOU SEE\nMY THREE?|"
                       "MIND THE\nGAP IN MY\nROW.", RAISED},
    /* OPEN_CENTRE */ {"THE CENTRE.\nA STRONG\nSTART!|GOOD CHOICE.\nTHE MIDDLE\nIS BEST.", SMILE},
    /* OPEN_EDGE */   {"A TIP: THE\nMIDDLE WINS\nMORE GAMES.|THE EDGE IS\nBRAVE. TRY\nTHE CENTRE.", TALK},
    /* AHEAD */       {"A TIP: LOOK\nFOR MY\nDIAGONALS.|TRY TO HOLD\nTHE CENTRE\nCOLUMNS.|BUILD TWO\nTHREATS AT\nONCE.|"
                       "CHECK EACH\nCELL BEFORE\nYOU DROP.", TALK},
    /* BEHIND */      {"YOU LEAD.\nKEEP IT UP!|STRONG PLAY\nSO FAR.|YOU HAVE\nTHE BETTER\nBOARD.|NICE SHAPE.\nI'M WORKING\nHARD HERE.", SMILE},
    /* BANTER */      {"THINK ONE\nMOVE AHEAD.|DIAGONALS\nARE EASY TO\nMISS.|A CLOSE\nGAME SO FAR.|"
                       "WHAT DOES\nEACH MOVE\nGIVE ME?|GOOD GAME\nSO FAR.|WELL PLAYED.\nYOUR MOVE\nNEXT.", TALK},
    /* IDLE */        {"NO RUSH AT\nALL.|TAKE ALL THE\nTIME YOU\nNEED.|A TIP: THE\nMIDDLE IS\nSTRONG.", SMILE},
    /* DRAWISH */     {"A CLOSE\nONE. WE ARE\nWELL MATCHED", SMILE},
    /* HE_LOST */     {"CONGRATU-\nLATIONS!\nWELL PLAYED.|A FINE WIN.\nMY\nCOMPLIMENTS!|SPLENDID!\nYOU EARNED\nTHAT ONE.|"
                       "WELL DONE\nINDEED! A\nPLEASURE.|BEAUTIFULLY\nPLAYED!", SMILE},
    /* HE_WON */      {"GOOD GAME!\nDO TRY\nAGAIN.|SO CLOSE.\nNEXT TIME\nIS YOURS!|WELL FOUGHT.\nANOTHER\nGAME?|"
                       "YOU PLAYED\nWELL. ONE\nMORE?|EVERY GAME\nMAKES YOU\nSHARPER.", SMILE},
    /* DRAWN */       {"A FAIR\nRESULT.\nGOOD GAME!|EVENLY\nMATCHED.\nWELL PLAYED", SMILE},
    /* P2_START */    {"GOOD LUCK\nTO YOU\nBOTH!|A FAIR GAME,\nPLEASE.\nENJOY!", SMILE},
    /* P2_MISSED */   {"A FOUR WAS\nOPEN THERE!|SO CLOSE!\nCHECK YOUR\nTHREES.", SURPRISED},
    /* P2_DOUBLE */   {"TWO THREATS\nAT ONCE.\nSUPERB!|A PERFECT\nTRAP!", SURPRISED},
    /* P2_BLOCK */    {"GOOD BLOCK.|WELL\nSPOTTED!|SHARP EYES!", SMILE},
    /* P2_THREAT */   {"THREE IN A\nROW. A BLOCK\nIS NEEDED.|CAREFUL\nNOW...|DO YOU SEE\nTHE THREE?", RAISED},
    /* P2_BANTER */   {"A CLOSE\nGAME SO FAR.|WELL PLAYED,\nBOTH.|DIAGONALS\nARE EASY TO\nMISS.", TALK},
    /* P2_WON */      {"CONGRATU-\nLATIONS!|WELL PLAYED,\nBOTH OF\nYOU!|A FINE WIN.\nGOOD GAME!", SMILE},
};

static uint8_t used[KINDS];

void reset() {
    for (auto &u : used) u = 0;
}

static uint8_t lines(const char *s) {
    uint8_t n = 1;
    for (; *s; s++) n = (uint8_t)(n + (*s == '|'));
    return n;
}

static void copy(const char *s, uint8_t i, char *out, uint8_t number) {
    while (i) i = (uint8_t)(i - (*s++ == '|'));
    uint8_t k = 0;
    for (; *s && *s != '|' && k < LINE_MAX - 1; s++) out[k++] = *s == '#' ? (char)('0' + number % 10) : *s;
    out[k] = 0;
}

uint8_t pick(uint8_t kind, c4::Rng &rng, char *out, uint8_t number) {
    const Pool &p = POOL[kind];
    uint8_t n = lines(p.lines), all = (uint8_t)((1u << n) - 1);
    if ((used[kind] & all) == all) used[kind] = 0;
    // The k-th line not yet used.
    uint8_t left = 0;
    for (uint8_t i = 0; i < n; i++) left = (uint8_t)(left + !(used[kind] & (1 << i)));
    uint8_t k = rng.below(left), i = 0;
    for (;; i++) {
        if (used[kind] & (1 << i)) continue;
        if (!k--) break;
    }
    used[kind] |= (uint8_t)(1 << i);
    copy(p.lines, i, out, number);
    return p.face;
}

bool line(uint8_t kind, uint8_t i, char *out) {
    if (kind >= KINDS || i >= lines(POOL[kind].lines)) return false;
    copy(POOL[kind].lines, i, out, 3);
    return true;
}

}  // namespace taunt
