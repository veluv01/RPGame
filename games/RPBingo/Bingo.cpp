#pragma GCC optimize("Os")   // cold code: size over speed
// The rules (Bingo.h describes them): dealing from the round's seed, the
// caller's clock, daubs and power-ups, the hall's winning call, the money.
#include <string.h>
#include "Bingo.h"

// RPGame.h's button masks (this file stays free of the hardware headers).
enum : uint8_t { BTN_A = 1, BTN_B = 2, BTN_UP = 4, BTN_DOWN = 8, BTN_LEFT = 16, BTN_RIGHT = 32 };

const uint32_t LINES[NLINES] = {
    0x000001Fu, 0x00003E0u, 0x0007C00u, 0x00F8000u, 0x1F00000u,       // rows
    0x0108421u, 0x0210842u, 0x0421084u, 0x0842108u, 0x1084210u,       // columns
    0x1041041u, 0x0111110u,                                           // diagonals
};
const uint8_t PRICES[3] = {5, 10, 25};
const uint8_t RIVALS[3] = {20, 40, 8};
static const uint8_t INTERVAL[3] = {120, 80, 180};
static const uint32_t CENTRE = 1u << 12;

char *callName(char *p, uint8_t n) {
    *p++ = "BINGO"[letterOf(n)];
    *p++ = '-';
    if (n >= 10) *p++ = (char)('0' + n / 10);
    *p++ = (char)('0' + n % 10);
    return p;
}

static uint8_t popcount(uint32_t m) {
    uint8_t n = 0;
    for (; m; m &= m - 1) n++;
    return n;
}

uint32_t Bingo::rand32() {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

void Bingo::emit(Ev t, uint8_t a, uint8_t b, uint8_t c, int32_t amount) {
    if (qLen >= 16) { qDropped++; return; }
    Event &e = q[(qHead + qLen++) & 15];
    e.type = t; e.a = a; e.b = b; e.c = c; e.amount = amount;
}

bool Bingo::popEvent(Event &e) {
    if (!qLen) return false;
    e = q[qHead];
    qHead = (qHead + 1) & 15; qLen--;
    return true;
}

uint8_t Bingo::priceNow() const {
    uint8_t i = opt.stakes < 3 ? opt.stakes : 0;
    while (i && purse < PRICES[i]) i--;
    return PRICES[i];
}

uint8_t Bingo::maxCards() const {
    int32_t n = purse / priceNow();
    return (uint8_t)(n > MAX_CARDS ? MAX_CARDS : n);
}

int32_t Bingo::potFor(uint8_t n) const {
    return (int32_t)priceNow() * (n + RIVALS[opt.hall < 3 ? opt.hall : 0]) * 9 / 10;
}

uint16_t Bingo::interval() const { return INTERVAL[opt.speed < 3 ? opt.speed : 0]; }

// Five numbers from each column's fifteen, the centre free.
void Bingo::dealCard(uint8_t *card) {
    for (uint8_t c = 0; c < 5; c++) {
        uint8_t pool[15];
        for (uint8_t i = 0; i < 15; i++) pool[i] = (uint8_t)(c * 15 + 1 + i);
        for (uint8_t r = 0; r < 5; r++) {
            uint8_t j = (uint8_t)(r + rand32() % (15u - r));
            uint8_t v = pool[j]; pool[j] = pool[r]; pool[r] = v;
            card[r * 5 + c] = v;
        }
    }
    card[12] = 0;
}

// The call on which a card first has a line, given every number's place in
// the draw (pos[0] = 0: the free centre).
static uint8_t firstLine(const uint8_t *card, const uint8_t *pos) {
    uint8_t best = 75;
    for (uint8_t l = 0; l < NLINES; l++) {
        uint8_t at = l < 5 ? (uint8_t)(l * 5) : l < 10 ? (uint8_t)(l - 5) : l == 10 ? 0 : 4;
        uint8_t step = l < 5 ? 1 : l < 10 ? 5 : l == 10 ? 6 : 4;
        uint8_t last = 0;
        for (uint8_t i = 0; i < 5; i++, at = (uint8_t)(at + step))
            if (pos[card[at]] > last) last = pos[card[at]];
        if (last < best) best = last;
    }
    return best;
}

void Bingo::setupRound(uint8_t n, uint8_t cardPrice, uint8_t nRivals) {
    roundSeed = rng;
    for (uint8_t i = 0; i < 75; i++) balls[i] = (uint8_t)(i + 1);
    for (uint8_t i = 74; i > 0; i--) {
        uint8_t j = (uint8_t)(rand32() % (i + 1u));
        uint8_t v = balls[i]; balls[i] = balls[j]; balls[j] = v;
    }
    nCards = n; price = cardPrice; rivals = nRivals;
    for (uint8_t k = 0; k < n; k++) {
        dealCard(cards[k]);
        daub[k] = CENTRE; pend[k] = 0;
    }
    uint8_t pos[76];
    pos[0] = 0;
    for (uint8_t i = 0; i < 75; i++) pos[balls[i]] = (uint8_t)(i + 1);
    hallBall = 75; hallTable = 2;
    for (uint8_t r = 0; r < nRivals; r++) {
        uint8_t card[25];
        dealCard(card);
        uint8_t at = firstLine(card, pos);
        if (at < hallBall) { hallBall = at; hallTable = (uint8_t)(r + 2); }
    }
    memset(calledBits, 0, sizeof calledBits);
    nCalled = 0; focus = 0; meter = 0; power = P_NONE; doubleCard = 0xFF; streak = 0;
    nForced = 0;
    pot = (int32_t)cardPrice * (n + nRivals) * 9 / 10;
}

void Bingo::newGame() {
    purse = START_PURSE;
    if (jackpot < JACKPOT_SEED) jackpot = JACKPOT_SEED;
    nCards = 0; buyN = 3; roundOpen = false;
    qLen = 0;
    phase = Phase::Welcome;
}

void Bingo::resume() {
    qLen = 0;
    if (jackpot < JACKPOT_SEED) jackpot = JACKPOT_SEED;
    if (roundOpen) {
        roundOpen = false;
        phase = Phase::Calling;
        timer = 150; callAge = 0xFFFF;
        emit(Ev::Resume);
    } else {
        phase = Phase::Welcome;
    }
}

void Bingo::quitNow() {
    roundOpen = phase == Phase::Calling;
    phase = Phase::Quit;
}

void Bingo::saveRound(RoundSave &s) const {
    s.seed = roundSeed;
    memcpy(s.daub, daub, sizeof s.daub);
    s.nCards = nCards; s.nCalled = nCalled; s.focus = focus; s.meter = meter; s.power = power;
    s.doubleCard = doubleCard; s.price = price; s.rivals = rivals;
}

void Bingo::loadRound(const RoundSave &s) {
    if (!s.nCards || s.nCards > MAX_CARDS || s.nCalled > 75) return;
    rng = s.seed ? s.seed : 0x9E3779B9u;
    setupRound(s.nCards, s.price, s.rivals);
    nCalled = s.nCalled;
    for (uint8_t i = 0; i < nCalled; i++) calledBits[(balls[i] - 1) >> 5] |= 1u << ((balls[i] - 1) & 31);
    for (uint8_t k = 0; k < nCards; k++) {
        daub[k] = (s.daub[k] & 0x1FFFFFFu) | CENTRE;
        for (uint8_t i = 0; i < 25; i++)
            if (cards[k][i] && called(cards[k][i]) && !((daub[k] >> i) & 1)) pend[k] |= 1u << i;
    }
    focus = s.focus < nCards ? s.focus : 0;
    meter = s.meter; power = s.power <= POWER_KINDS ? s.power : 0;
    doubleCard = s.doubleCard;
    roundOpen = true;
    phase = Phase::Quit;
}

void Bingo::openBuy() {
    if (purse < PRICES[0]) {
        phase = Phase::GameLost;
        stats.gamesBroke++;
        emit(Ev::GameOver);
        return;
    }
    phase = Phase::Buy;
    uint8_t m = maxCards();
    if (!buyN) buyN = 1;
    if (buyN > m) buyN = m;
    emit(Ev::BuyOpen, buyN);
}

void Bingo::start(uint8_t n) {
    uint8_t p = priceNow(), m = maxCards();
    if (n > m) n = m;
    if (!n) return;
    buyN = n;
    int32_t cost = (int32_t)p * n;
    purse -= cost;
    jackpot += cost >= 20 ? cost / 20 : 1;
    stats.rounds++; stats.cards += n;
    setupRound(n, p, RIVALS[opt.hall < 3 ? opt.hall : 0]);
    phase = Phase::Calling;
    timer = 100; callAge = 0xFFFF;
    emit(Ev::Start, n, 0, 0, cost);
}

void Bingo::force(uint8_t number) {
    for (uint8_t i = (uint8_t)(nCalled + nForced); i < 75; i++) {
        if (balls[i] != number) continue;
        balls[i] = balls[nCalled + nForced];
        balls[nCalled + nForced] = number;
        nForced++;
        return;
    }
}

void Bingo::callBall() {
    uint8_t n = balls[nCalled++];
    if (nForced) nForced--;
    calledBits[(n - 1) >> 5] |= 1u << ((n - 1) & 31);
    uint8_t hits = 0;
    for (uint8_t k = 0; k < nCards; k++)
        for (uint8_t i = 0; i < 25; i++)
            if (cards[k][i] == n && !((daub[k] >> i) & 1)) { pend[k] |= 1u << i; hits++; }
    callAge = 0;
    timer = interval();
    emit(Ev::Call, n, hits);
}

void Bingo::moveFocus(int8_t d) {
    if (nCards < 2) return;
    focus = (uint8_t)((focus + nCards + d) % nCards);
    emit(Ev::Focus, focus, (uint8_t)(d > 0));
}

void Bingo::checkWin(uint8_t card) {
    for (uint8_t l = 0; l < NLINES; l++) {
        if ((daub[card] & LINES[l]) != LINES[l]) continue;
        uint8_t flags = 0;
        int32_t pay = pot;
        if (doubleCard == card) { pay *= 2; flags |= WIN_DOUBLE; }
        if (nCalled <= JACKPOT_CALLS) {
            pay += jackpot; jackpot = JACKPOT_SEED;
            flags |= WIN_JACKPOT;
            stats.jackpots++;
        }
        if (rare == 1 || (!rare && rand32() % RARE_ONE_IN == 0)) flags |= WIN_RARE;
        purse += pay;
        stats.wins++;
        if (pay > stats.biggestWin) stats.biggestWin = pay;
        if (!stats.fastest || nCalled < stats.fastest) stats.fastest = nCalled;
        winCard = card; winLine = l;
        phase = Phase::Won; wait = 40;
        emit(Ev::Bingo, card, l, flags, pay);
        return;
    }
}

void Bingo::daubFocus() {
    uint32_t m = pend[focus];
    if (!m) { streak = 0; emit(Ev::Miss, 0); return; }
    daub[focus] |= m; pend[focus] = 0;
    bool fast = false;
    if (callAge <= FAST_TICKS && nCalled)
        for (uint8_t i = 0; i < 25; i++)
            if (((m >> i) & 1) && cards[focus][i] == balls[nCalled - 1]) fast = true;
    uint8_t cells = popcount(m);
    if (fast) {
        streak++;
        if (streak > stats.bestStreak) stats.bestStreak = streak;
    } else streak = 0;
    emit(Ev::Daub, focus, cells, fast, (int32_t)m);
    if (!power) {
        meter = (uint8_t)(meter + cells + (fast ? 2 : 0));
        if (meter >= POWER_FULL) {
            meter = 0;
            power = (uint8_t)(1 + rand32() % POWER_KINDS);
            emit(Ev::Power, power);
        }
    }
    checkWin(focus);
}

void Bingo::usePower() {
    if (!power) { emit(Ev::Miss, 1); return; }
    uint8_t p = power;
    power = P_NONE;
    uint32_t cell = 0;
    if (p == P_WILD) {
        // The open cell on the line closest to done.
        uint8_t fewest = 6;
        for (uint8_t l = 0; l < NLINES; l++) {
            uint32_t open = LINES[l] & ~daub[focus];
            uint8_t n = popcount(open);
            if (n && n < fewest) { fewest = n; cell = open & (0u - open); }
        }
        daub[focus] |= cell; pend[focus] &= ~cell;
    } else if (p == P_FREEZE) {
        timer = (uint16_t)(timer + 2 * interval());
    } else {
        doubleCard = focus;
    }
    emit(Ev::PowerUse, p, focus, 0, (int32_t)cell);
    if (p == P_WILD) checkWin(focus);
}

void Bingo::rivalWins() {
    bool had = false;
    for (uint8_t k = 0; k < nCards; k++)
        for (uint8_t l = 0; l < NLINES; l++)
            if (((daub[k] | pend[k]) & LINES[l]) == LINES[l]) had = true;
    streak = 0;
    phase = Phase::Lost; wait = 40;
    emit(Ev::Rival, hallTable, had);
}

void Bingo::daubAll() {
    uint8_t f = focus;
    for (uint8_t k = 0; k < nCards && phase == Phase::Calling; k++)
        if (pend[k]) { focus = k; daubFocus(); }
    if (phase == Phase::Calling) focus = f;
}

void Bingo::update(uint8_t pressed, uint8_t repeat, bool fxBusy) {
    switch (phase) {
        case Phase::Welcome:
            openBuy();
            break;
        case Phase::Buy: {
            uint8_t m = maxCards();
            if (repeat & (BTN_LEFT | BTN_DOWN)) {
                if (buyN > 1) emit(Ev::BuyPick, --buyN);
            } else if (repeat & (BTN_RIGHT | BTN_UP)) {
                if (buyN < m) emit(Ev::BuyPick, ++buyN);
                else if (m < MAX_CARDS) emit(Ev::Deny);
            }
            if (pressed & BTN_A) start(buyN);
            break;
        }
        case Phase::Calling:
            if (callAge < 0xFFFF) callAge++;
            if (repeat & BTN_LEFT) moveFocus(-1);
            if (repeat & BTN_RIGHT) moveFocus(1);
            if (pressed & BTN_A) daubFocus();
            if (phase == Phase::Calling && (pressed & BTN_B)) usePower();
            if (phase != Phase::Calling) break;
            if (timer) timer--;
            if (!timer) {
                // A rival's line is good from its call; the player has until
                // the next one to get a daub in first.
                if (nCalled >= hallBall) rivalWins();
                else callBall();
            }
            break;
        case Phase::Won: case Phase::Lost:
            if (fxBusy) break;
            if (wait) { wait--; break; }
            if (purse > stats.bestPurse) stats.bestPurse = purse;
            openBuy();
            break;
        default:
            break;
    }
}
