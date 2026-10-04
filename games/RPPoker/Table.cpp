// The poker table (Table.h): the deck, the betting rules, side pots, the
// showdown and the flow of a hand, reported as events for the stage.
#pragma GCC optimize("Os")
#include <string.h>
#include "Table.h"
#include "Hand.h"
#include <rpgame/Input.h>                    // button masks only (no graphics: host tests)

// Pacing, in frames at the FUN pace (QUICK halves them).
enum : uint8_t {
    P_ACTION = 16, P_STREET = 18, P_DEALT = 10, P_REVEAL = 36, P_AWARD = 80, P_DRAW = 26, P_FOLDED = 6,
};
static const int32_t GOALS[3] = {10000, 50000, 0};
static const uint8_t COLOURS = 6;            // CPU avatar colours (CardArt.cpp)

static inline uint8_t nextSeat(uint8_t s) { return (uint8_t)((s + 1) & 3); }
static inline uint8_t popcount8(uint8_t m) { uint8_t n = 0; for (; m; m &= (uint8_t)(m - 1)) n++; return n; }

int32_t Table::goal() const { return GOALS[opt.goal]; }
int32_t Table::betSize() const { return (v().st[street].big ? 4 : 2) * unit(); }
void Table::newPurse() { purse = 500; }

// ---------------------------------------------------------------------------
// Deck, events, pacing
// ---------------------------------------------------------------------------
uint32_t Table::rand32() {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

void Table::stackDeck(const uint8_t *cards, uint8_t n) {
    if (n > sizeof stacked) n = sizeof stacked;
    memcpy(stacked, cards, n);
    nStacked = n;
}

void Table::shuffle() {
    for (uint8_t i = 0; i < 52; i++) deck[i] = i;
    for (uint8_t i = 51; i > 0; i--) {
        uint8_t j = (uint8_t)(rand32() % (i + 1u));
        uint8_t t = deck[i]; deck[i] = deck[j]; deck[j] = t;
    }
    // A stacked deck (tests, scripted demos) goes on top, in order.
    for (uint8_t k = 0; k < nStacked; k++)
        for (uint8_t i = k; i < 52; i++)
            if (deck[i] == stacked[k]) { deck[i] = deck[k]; deck[k] = stacked[k]; break; }
    nStacked = 0;
    deckPos = 0;
}

uint8_t Table::draw() { return deckPos < 52 ? deck[deckPos++] : 0; }

void Table::emit(Ev t, uint8_t a, uint8_t b, uint8_t c, int32_t amount) {
    if (qLen >= sizeof q / sizeof q[0]) return;
    Event &e = q[(qHead + qLen++) % (sizeof q / sizeof q[0])];
    e.type = t; e.a = a; e.b = b; e.c = c; e.amount = amount;
}

bool Table::popEvent(Event &e) {
    if (!qLen) return false;
    e = q[qHead];
    qHead = (uint8_t)((qHead + 1) % (sizeof q / sizeof q[0]));
    qLen--;
    return true;
}

void Table::pace(uint16_t frames) {
    if (opt.pace) frames /= 2;
    wait = frames;
}

void Table::go(Phase p, uint16_t frames) {
    phase = p;
    step = 0;
    pace(frames);
    bar = p == Phase::Human ? Bar::Bet : p == Phase::DrawHuman ? Bar::Draw :
          p == Phase::HandOver ? Bar::Next : p == Phase::Rebuy ? Bar::Rebuy : Bar::None;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------
uint8_t Table::nLive() const {
    uint8_t n = 0;
    for (uint8_t s = 0; s < SEATS; s++) n += live(s);
    return n;
}

uint8_t Table::nCanAct() const {
    uint8_t n = 0;
    for (uint8_t s = 0; s < SEATS; s++) n += seats[s].state == S_LIVE;
    return n;
}

int32_t Table::potTotal() const {
    int32_t t = pot;
    for (uint8_t s = 0; s < SEATS; s++) t += seats[s].bet;
    return t;
}

int32_t Table::toCall(uint8_t s) const {
    int32_t c = curBet - seats[s].bet;
    if (c > seats[s].stack) c = seats[s].stack;
    return c > 0 ? c : 0;
}

// The total a bet or raise must reach to be a full one (re-opening the
// betting): no limit and pot limit, the last raise again (at least the big
// blind); fixed limit, one bet more (stud's bring-in is completed to a bet).
int32_t Table::fullTo() const {
    if (v().limit == FIXED_LIMIT) return curBet < betSize() ? betSize() : curBet + betSize();
    return curBet + (lastRaise > bb() ? lastRaise : bb());
}

int32_t Table::minTo(uint8_t s) const {
    int32_t to = fullTo(), all = seats[s].bet + seats[s].stack;
    return to < all ? to : all;
}

int32_t Table::maxTo(uint8_t s) const {
    int32_t all = seats[s].bet + seats[s].stack, to = all;
    if (v().limit == POT_LIMIT) to = curBet + potTotal() + (curBet - seats[s].bet);
    else if (v().limit == FIXED_LIMIT) to = fullTo();
    return to < all ? to : all;
}

bool Table::canRaise(uint8_t s) const {
    const Seat &p = seats[s];
    if (p.state != S_LIVE || !p.mayRaise || p.stack <= curBet - p.bet) return false;
    if (v().limit == FIXED_LIMIT && raises >= 4) return false;
    for (uint8_t o = 0; o < SEATS; o++)
        if (o != s && seats[o].state == S_LIVE) return true;          // someone to raise against
    return false;
}

uint8_t Table::slots() const { return v().limit == FIXED_LIMIT ? 3 : 4; }

bool Table::slotEnabled(uint8_t slot) const {
    switch (bar) {
        case Bar::Bet:
            if (slot == B_FOLD) return toCall(YOU) > 0;
            if (slot == B_CALL) return true;
            return slot < slots() && canRaise(YOU);
        case Bar::Next: return slot <= N_LEAVE;
        case Bar::Rebuy: return slot <= N_LEAVE;
        default: return false;
    }
}

// Five Card Draw: up to three cards, or four keeping an ace.
static bool drawOk(const uint8_t *cards, uint8_t mask) {
    uint8_t n = popcount8(mask);
    if (n <= 3) return true;
    if (n > 4) return false;
    for (uint8_t i = 0; i < 5; i++)
        if (!((mask >> i) & 1)) return rankOf(cards[i]) == RA;
    return false;
}
uint8_t Table::drawLimit() const {
    for (uint8_t i = 0; i < 5; i++) if (rankOf(seats[YOU].cards[i]) == RA) return 4;
    return 3;
}

uint32_t Table::score(uint8_t s) const {
    const Seat &p = seats[s];
    if (game == OMAHA && nBoard >= 3) return hand::omaha(p.cards, board, nBoard);
    uint8_t all[12], n = p.n;
    memcpy(all, p.cards, n);
    if (game == HOLDEM) { memcpy(all + n, board, nBoard); n = (uint8_t)(n + nBoard); }
    return hand::eval(all, n);
}

uint32_t Table::showing(uint8_t s) const {
    const Seat &p = seats[s];
    uint8_t up[7], n = 0;
    for (uint8_t i = 0; i < p.n; i++) if ((p.up >> i) & 1) up[n++] = p.cards[i];
    return hand::eval(up, n);
}

uint8_t Table::bringInSeat() const {
    uint8_t best = 0xFF, at = 0;
    for (uint8_t s = 0; s < SEATS; s++)
        if (live(s) && seats[s].n >= 3 && seats[s].cards[2] < best) { best = seats[s].cards[2]; at = s; }
    return at;
}

// ---------------------------------------------------------------------------
// Sitting down, a new hand
// ---------------------------------------------------------------------------
void Table::seatCpu(uint8_t s) {
    Seat &p = seats[s];
    // A colour nobody at the table has, going round the six.
    uint8_t c = (uint8_t)(rand32() % COLOURS);
    for (uint8_t k = 0; k < COLOURS; k++, c = (uint8_t)((c + 1) % COLOURS)) {
        bool used = false;
        for (uint8_t o = 1; o < SEATS; o++) used |= o != s && seats[o].colour == c && seats[o].state != S_OUT;
        if (!used) break;
    }
    memset(&p, 0, sizeof p);
    p.colour = c;
    p.stack = (int32_t)(60 + rand32() % 141) * unit();         // 30 to 100 big blinds
    static const uint8_t JITTER[LEVELS] = {15, 10, 8};
    p.quirk = (int8_t)((int)(rand32() % (2u * JITTER[level] + 1)) - JITTER[level]);
    p.state = S_LIVE;
    emit(Ev::Join, s, c, 0, p.stack);
}

void Table::sitDown(uint8_t g, uint8_t lv, int32_t in, uint32_t s) {
    game = g; level = lv;
    seed(s);
    if (in > purse) in = purse;
    purse -= in;
    buyIn = in;
    qLen = 0;
    memset(seats, 0, sizeof seats);
    memset(&read, 0, sizeof read);
    for (uint8_t i = 1; i < SEATS; i++) seats[i].colour = 0xFF;
    seats[YOU].stack = in;
    seats[YOU].colour = 0xFF;
    for (uint8_t i = 1; i < SEATS; i++) seatCpu(i);
    button = (uint8_t)(rand32() & 3);
    if (!demo && wealth() > stats.bestPurse) stats.bestPurse = wealth();
    go(Phase::HandStart, 20);
}

void Table::leave() {
    if (phase == Phase::HandOver && step) {          // the hand's books close first
        step = 0;
        endHand();
        if (phase == Phase::Won || phase == Phase::Broke) return;
    }
    bool mid = phase != Phase::HandOver && phase != Phase::Rebuy && phase != Phase::Leave;
    if (mid && !demo) stats.g[game].net += seats[YOU].stack - handStart;   // chips in the pot are lost
    purse += seats[YOU].stack;
    seats[YOU].stack = 0;
    wantSave = !demo;
    go(Phase::Leave);
}

void Table::post(uint8_t s, uint8_t a, int32_t amount, bool asBet) {
    Seat &p = seats[s];
    int32_t pay = amount < p.stack ? amount : p.stack;
    p.stack -= pay;
    p.put += pay;
    if (asBet) p.bet += pay; else pot += pay;
    if (!p.stack) p.state = S_ALLIN;
    p.last = a;
    emit(Ev::Post, s, a, 0, pay);
}

void Table::startHand() {
    for (uint8_t s = 1; s < SEATS; s++) if (!seats[s].stack) seatCpu(s);
    for (uint8_t s = 0; s < SEATS; s++) {
        Seat &p = seats[s];
        p.bet = p.put = 0;
        p.n = p.up = 0;
        p.state = S_LIVE;
        p.last = A_NONE;
        p.acted = 0; p.mayRaise = 1;
        p.drew = 0xFF;
    }
    nBoard = 0; pot = 0; nPots = 0; shownDown = 0; street = 0;
    button = nextSeat(button);
    emit(Ev::Button, button);
    shuffle();
    emit(Ev::Shuffle);
    handStart = seats[YOU].stack;
    youWon = 0;
    if (!demo) stats.g[game].hands++;
    curBet = 0; lastRaise = bb(); raises = 0;
    if (v().stud) {
        for (uint8_t i = 1; i <= SEATS; i++) post((uint8_t)((button + i) & 3), A_ANTE, unit(), false);
    } else {
        post(nextSeat(button), A_SB, unit(), true);
        post((uint8_t)((button + 2) & 3), A_BB, bb(), true);
        curBet = bb();
        raises = v().limit == FIXED_LIMIT;           // the big blind is the first bet
    }
    startStreet();
}

void Table::startStreet() {
    const Street &st = v().st[street];
    if (street) {
        curBet = 0; lastRaise = bb(); raises = 0;
        for (uint8_t s = 0; s < SEATS; s++) { seats[s].acted = 0; seats[s].mayRaise = 1; }
    }
    memset(streetRaises, 0, sizeof streetRaises);
    aggressor = 0xFF;
    emit(Ev::Street, street);
    // A burn card before every street but the first.
    if (street && (st.deal || st.board)) emit(Ev::Deal, 5, 0, draw(), 0);
    for (uint8_t k = 0; k < st.deal; k++)
        for (uint8_t i = 1; i <= SEATS; i++) {
            uint8_t s = (uint8_t)((button + i) & 3);
            Seat &p = seats[s];
            if (!live(s) || p.n >= 7) continue;
            uint8_t c = draw(), up = (uint8_t)((st.up >> k) & 1);
            p.cards[p.n] = c;
            if (up) p.up |= (uint8_t)(1 << p.n);
            emit(Ev::Deal, s, p.n, c, up);
            p.n++;
        }
    for (uint8_t k = 0; k < st.board; k++) {
        board[nBoard] = draw();
        emit(Ev::Deal, 4, nBoard, board[nBoard], 1);
        nBoard++;
    }
    if (shownDown) revealAll();                      // all in: new cards face up too
    if (v().stud && street == 0) { go(Phase::BringIn, P_DEALT); return; }
    if (!bettingNeeded()) { revealAll(); go(Phase::RunOut, P_STREET * 2); return; }
    actor = firstToAct();
    nextActor((uint8_t)((actor + 3) & 3));
    go(Phase::Betting, P_DEALT);
}

// Is there anyone left to bet? Not if nobody can act, nor if one player can
// and has nothing to call.
bool Table::bettingNeeded() const {
    uint8_t n = 0, who = 0;
    for (uint8_t s = 0; s < SEATS; s++) if (seats[s].state == S_LIVE) { n++; who = s; }
    if (n == 0) return false;
    if (n == 1) return seats[who].bet < curBet;
    return true;
}

uint8_t Table::firstToAct() const {
    if (!v().stud) return street == 0 ? (uint8_t)((button + 3) & 3) : nextSeat(button);
    // Stud: the best hand showing, ties to the first from the button.
    uint8_t best = nextSeat(button);
    uint32_t bs = 0;
    for (uint8_t i = 1; i <= SEATS; i++) {
        uint8_t s = (uint8_t)((button + i) & 3);
        if (!live(s)) continue;
        uint32_t sc = showing(s);
        if (sc > bs) { bs = sc; best = s; }
    }
    return best;
}

// The next seat (after `from`) that still has to act this street.
void Table::nextActor(uint8_t from) {
    for (uint8_t i = 1; i <= SEATS; i++) {
        uint8_t s = (uint8_t)((from + i) & 3);
        const Seat &p = seats[s];
        if (p.state == S_LIVE && (!p.acted || p.bet < curBet)) { actor = s; return; }
    }
    actor = 0xFF;
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
void Table::act(uint8_t s, uint8_t a, int32_t to) {
    Seat &p = seats[s];
    if (a == A_FOLD) {
        p.state = S_FOLDED;
    } else if (a == A_CHECK || a == A_CALL || to <= curBet) {
        int32_t pay = toCall(s);
        p.bet += pay; p.stack -= pay; p.put += pay;
        a = pay ? (p.stack ? A_CALL : A_ALLIN) : A_CHECK;
    } else {
        int32_t all = p.bet + p.stack;
        if (to > all) to = all;
        int32_t pay = to - p.bet;
        bool full = to >= fullTo();
        a = pay == p.stack ? A_ALLIN : curBet == 0 ? A_BET :
            (v().stud && curBet < betSize()) ? A_COMPLETE : A_RAISE;
        p.bet = to; p.stack -= pay; p.put += pay;
        for (uint8_t o = 0; o < SEATS; o++) {
            Seat &q = seats[o];
            if (o == s || q.state != S_LIVE) continue;
            // A full raise re-opens the betting; a short all-in only asks the
            // others to call the extra (who acted already may not raise).
            if (full) { q.acted = 0; q.mayRaise = 1; }
            else if (q.acted) { q.acted = 0; q.mayRaise = 0; }
        }
        if (full) { lastRaise = to - curBet; raises++; }
        curBet = to;
        aggressor = s;
        streetRaises[s]++;
        if (s == YOU) read.raises++;
    }
    if (s == YOU) read.acts++;
    p.acted = 1;
    p.last = a;
    if (!p.stack && p.state == S_LIVE) p.state = S_ALLIN;
    emit(Ev::Action, s, a, 0, p.bet);
}

void Table::afterAction() {
    if (nLive() == 1) { go(Phase::EndStreet, P_ACTION); return; }
    nextActor(actor);
    go(actor == 0xFF ? Phase::EndStreet : Phase::Betting, P_ACTION);
}

void Table::makeView(uint8_t s) {
    ai::View &w = view;
    memset(&w, 0, sizeof w);
    const Seat &p = seats[s];
    w.game = game; w.street = street; w.seat = s; w.limit = v().limit;
    memcpy(w.own, p.cards, p.n); w.nOwn = p.n;
    memcpy(w.board, board, nBoard); w.nBoard = nBoard;
    uint8_t order = (uint8_t)((s - button - 1) & 3), last = 1;
    for (uint8_t o = 0; o < SEATS; o++) {
        if (o == s || !live(o)) continue;
        if (((o - button - 1) & 3) > order) last = 0;
        uint8_t k = w.nOpp++;
        if (v().stud)
            for (uint8_t i = 0; i < seats[o].n; i++)
                if (((seats[o].up >> i) & 1) && w.nOppUp[k] < 4) w.oppUp[k][w.nOppUp[k]++] = seats[o].cards[i];
        w.raisesAgainst = (uint8_t)(w.raisesAgainst + streetRaises[o]);
    }
    w.youRaised = s != YOU && streetRaises[YOU];
    w.lastToAct = last;
    w.preDraw = v().draw && street == 0;
    w.canRaise = canRaise(s);
    w.pot = potTotal();
    w.toCall = toCall(s);
    w.minTo = minTo(s);
    w.maxTo = maxTo(s);
    w.bet = p.bet;
    w.stack = p.stack;
    w.bb = v().limit == FIXED_LIMIT ? betSize() : bb();
}

void Table::cpuAct() {
    ai::Choice c = ai::decide(view, level, seats[actor].quirk, read);
    if (c.move == ai::M_FOLD) act(actor, toCall(actor) ? A_FOLD : A_CHECK, 0);
    else if (c.move == ai::M_CALL || !canRaise(actor)) act(actor, A_CALL, 0);
    else act(actor, A_RAISE, c.to);
    afterAction();
}

// ---------------------------------------------------------------------------
// End of a street, showdown, pots
// ---------------------------------------------------------------------------
void Table::endStreet() {
    // An uncalled bet goes back: the biggest bet's excess over the next.
    uint8_t top = 0;
    int32_t second = 0;
    for (uint8_t s = 1; s < SEATS; s++) if (seats[s].bet > seats[top].bet) top = s;
    for (uint8_t s = 0; s < SEATS; s++) if (s != top && seats[s].bet > second) second = seats[s].bet;
    Seat &t = seats[top];
    if (t.bet > second) {
        int32_t back = t.bet - second;
        t.bet -= back; t.put -= back; t.stack += back;
        if (t.state == S_ALLIN) t.state = S_LIVE;
        emit(Ev::Return, top, 0, 0, back);
    }
    int32_t swept = 0;
    for (uint8_t s = 0; s < SEATS; s++) { swept += seats[s].bet; seats[s].bet = 0; }
    pot += swept;
    curBet = 0;
    if (swept) emit(Ev::Collect, 0, 0, 0, pot);
}

void Table::revealAll() {
    for (uint8_t s = 0; s < SEATS; s++) {
        if (!live(s)) continue;
        seats[s].up = 0x7F;
        if (!((shownDown >> s) & 1)) emit(Ev::Reveal, s);
        shownDown |= (uint8_t)(1 << s);
    }
}

void Table::afterStreet() {
    if (nLive() == 1) { winUncontested(); return; }
    if (street + 1 >= v().streets) { go(Phase::Showdown, P_STREET); return; }
    if (v().draw && street == 0) {
        emit(Ev::Deal, 5, 0, draw(), 0);             // the burn before the draw
        go(Phase::Draw, P_STREET);
        return;
    }
    street++;
    startStreet();
    if (phase == Phase::Betting) pace(P_STREET);
}

void Table::winUncontested() {
    for (uint8_t s = 0; s < SEATS; s++) {
        if (!live(s)) continue;
        seats[s].stack += pot;
        emit(Ev::Win, s, 0, 0xFF, pot);
        if (s == YOU) youWon += pot;
    }
    pot = 0;
    go(Phase::HandOver, P_AWARD);
    step = 1;                                        // HandOver: end the hand first
    bar = Bar::None;
}

void Table::computePots() {
    // Slice everyone's money (folded players' too) at each live player's
    // total, lowest first: each slice is a pot for those who reached it.
    nPots = 0;
    int32_t prev = 0;
    for (;;) {
        int32_t lv = 0x7FFFFFFF;
        for (uint8_t s = 0; s < SEATS; s++)
            if (live(s) && seats[s].put > prev && seats[s].put < lv) lv = seats[s].put;
        if (lv == 0x7FFFFFFF) break;
        int32_t amount = 0;
        uint8_t elig = 0;
        for (uint8_t s = 0; s < SEATS; s++) {
            int32_t put = seats[s].put, a = put < lv ? put : lv;
            if (a > prev) amount += a - prev;
            if (live(s) && put >= lv) elig |= (uint8_t)(1 << s);
        }
        if (nPots < SEATS) pots[nPots++] = {amount, elig};
        else pots[nPots - 1].amount += amount;
        prev = lv;
    }
    // Folded money above every live player's total (only possible if the
    // returns missed it) goes to the last pot rather than vanish.
    int32_t extra = 0;
    for (uint8_t s = 0; s < SEATS; s++) if (seats[s].put > prev) extra += seats[s].put - prev;
    if (nPots) pots[nPots - 1].amount += extra;
}

void Table::showdown() {
    // Turn the hands up one at a time: the last to bet or raise first, else
    // the first from the button.
    uint8_t first = aggressor != 0xFF && live(aggressor) ? aggressor : nextSeat(button);
    for (uint8_t i = 0; i < SEATS; i++) {
        uint8_t s = (uint8_t)((first + i) & 3);
        if (!live(s) || ((shownDown >> s) & 1)) continue;
        seats[s].up = 0x7F;
        shownDown |= (uint8_t)(1 << s);
        emit(Ev::Reveal, s);
        pace(s == YOU ? P_REVEAL / 2 : P_REVEAL);
        return;
    }
    for (uint8_t s = 0; s < SEATS; s++) scores[s] = live(s) ? score(s) : 0;
    if (live(YOU) && !demo) {
        uint8_t c = (uint8_t)(hand::cat(scores[YOU]) + 1);
        if (c > stats.g[game].best) stats.g[game].best = c;
        if (streetRaises[YOU] && hand::cat(scores[YOU]) <= hand::PAIR) read.bluffs++;
    }
    computePots();
    pot = 0;
    go(Phase::Award, P_STREET);
    step = nPots;
}

void Table::award() {
    // Side pots first, the main pot last.
    if (!step) { go(Phase::HandOver, P_AWARD); step = 1; bar = Bar::None; return; }
    uint8_t k = --step;
    const Pot &pp = pots[k];
    uint32_t best = 0;
    for (uint8_t s = 0; s < SEATS; s++) if (((pp.eligible >> s) & 1) && scores[s] > best) best = scores[s];
    uint8_t winners = 0, n = 0;
    for (uint8_t s = 0; s < SEATS; s++)
        if (((pp.eligible >> s) & 1) && scores[s] == best) { winners |= (uint8_t)(1 << s); n++; }
    int32_t share = pp.amount / n, odd = pp.amount - share * n;
    for (uint8_t i = 1; i <= SEATS; i++) {
        uint8_t s = (uint8_t)((button + i) & 3);
        if (!((winners >> s) & 1)) continue;
        int32_t got = share + (odd > 0 ? 1 : 0);
        if (odd > 0) odd--;
        seats[s].stack += got;
        emit(Ev::Win, s, k, hand::cat(best), got);
        if (s == YOU) youWon += got;
    }
    pace(P_AWARD);
}

void Table::endHand() {
    emit(Ev::HandEnd);
    for (uint8_t s = 1; s < SEATS; s++)
        if (!seats[s].stack) { seats[s].state = S_OUT; emit(Ev::Bust, s); }
    if (demo) { go(Phase::HandStart, 150); return; }
    GameStats &g = stats.g[game];
    g.net += seats[YOU].stack - handStart;
    if (youWon) { g.won++; if (youWon > g.biggest) g.biggest = youWon; }
    if (wealth() > stats.bestPurse) stats.bestPurse = wealth();
    if (goal() && wealth() >= goal()) { stats.banks++; wantSave = true; go(Phase::Won); return; }
    if (++handsSinceSave >= 5) { handsSinceSave = 0; wantSave = true; }
    if (!seats[YOU].stack) {
        if (purse >= minBuyIn(level)) { sel = 0; go(Phase::Rebuy); return; }
        wantSave = true;
        if (purse < minBuyIn(ROOKIE)) { stats.broke++; go(Phase::Broke); }
        else go(Phase::Leave);                       // a smaller table, then
        return;
    }
    sel = N_NEXT;
    go(Phase::HandOver);
}

// ---------------------------------------------------------------------------
// Five Card Draw
// ---------------------------------------------------------------------------
void Table::replaceCards(uint8_t s, uint8_t mask) {
    Seat &p = seats[s];
    p.drew = popcount8(mask);
    p.last = A_DRAW;
    emit(Ev::Discard, s, mask, p.drew);
    for (uint8_t i = 0; i < 5; i++)
        if ((mask >> i) & 1) {
            p.cards[i] = draw();
            emit(Ev::Deal, s, i, p.cards[i], (uint8_t)((shownDown >> s) & 1));
        }
}

// ---------------------------------------------------------------------------
// Your input
// ---------------------------------------------------------------------------
void Table::fixSel() {
    if (slotEnabled(sel)) return;
    sel = B_CALL;
}

void Table::humanInput(uint8_t pressed, uint8_t repeat) {
    fixSel();
    int8_t d = (repeat & RIGHT_BUTTON) ? 1 : (repeat & LEFT_BUTTON) ? -1 : 0;
    if (d) {
        uint8_t s = sel;
        do { s = (uint8_t)((s + slots() + d) % slots()); } while (!slotEnabled(s));
        if (s != sel) { sel = s; emit(Ev::Cursor); }
    }
    int32_t lo = minTo(YOU), hi = maxTo(YOU);
    if (raiseTo < lo) raiseTo = lo;
    if (raiseTo > hi) raiseTo = hi;
    // UP/DOWN size the raise in chip steps, faster the longer it is held.
    int8_t r = (repeat & UP_BUTTON) ? 1 : (repeat & DOWN_BUTTON) ? -1 : 0;
    if (r && sel == B_RAISE && hi > lo) {
        accel = (pressed & (UP_BUTTON | DOWN_BUTTON)) ? 0 : (uint8_t)(accel < 40 ? accel + 1 : 40);
        int32_t stepAmt = bb() * (accel > 24 ? 10 : accel > 8 ? 5 : 1);
        int32_t to = r > 0 ? (raiseTo + stepAmt) / stepAmt * stepAmt : (raiseTo - 1) / stepAmt * stepAmt;
        if (to < lo) to = lo;
        if (to > hi) to = hi;
        if (to != raiseTo) { raiseTo = to; emit(Ev::Cursor); }
        else emit(Ev::Deny);
    } else if (r && sel != B_RAISE && canRaise(YOU)) {
        sel = B_RAISE;                                // UP/DOWN jump to the raise
        emit(Ev::Cursor);
    }
    if (!(pressed & A_BUTTON)) return;
    if (!slotEnabled(sel)) { emit(Ev::Deny); return; }
    switch (sel) {
        case B_FOLD:  act(YOU, A_FOLD, 0); break;
        case B_CALL:  act(YOU, A_CALL, 0); break;
        case B_RAISE: act(YOU, A_RAISE, v().limit == FIXED_LIMIT ? lo : raiseTo); break;
        case B_MAX:   act(YOU, A_RAISE, hi); break;
    }
    afterAction();
}

void Table::drawInput(uint8_t pressed, uint8_t repeat) {
    Seat &p = seats[YOU];
    int8_t d = (repeat & RIGHT_BUTTON) ? 1 : (repeat & LEFT_BUTTON) ? -1 : 0;
    if (d) { glove = (uint8_t)((glove + 6 + d) % 6); emit(Ev::Cursor); }
    uint8_t m = drawMask;
    if (glove < 5) {
        uint8_t bit = (uint8_t)(1 << glove);
        if (pressed & UP_BUTTON) m |= bit;
        if (pressed & DOWN_BUTTON) m &= (uint8_t)~bit;
        if (pressed & A_BUTTON) m ^= bit;
        if (m != drawMask) {
            if (drawOk(p.cards, m)) { drawMask = m; emit(Ev::Cursor); }
            else emit(Ev::Deny);
        }
    } else if (pressed & A_BUTTON) {
        replaceCards(YOU, drawMask);
        go(Phase::Draw, P_DRAW);
    }
}

void Table::barInput(uint8_t pressed) {
    if (pressed & (LEFT_BUTTON | RIGHT_BUTTON)) { sel ^= 1; emit(Ev::Cursor); }
    if (!(pressed & A_BUTTON)) return;
    if (sel == N_LEAVE) { leave(); return; }
    if (phase == Phase::Rebuy) {
        int32_t in = buyIn < purse ? buyIn : purse;
        purse -= in;
        seats[YOU].stack = in;
        emit(Ev::Rebuy, YOU, 0, 0, in);
    }
    go(Phase::HandStart, 6);
}

// ---------------------------------------------------------------------------
// The flow
// ---------------------------------------------------------------------------
void Table::update(uint8_t pressed, uint8_t repeat, bool fxBusy, bool mayThink) {
    // A CPU thinks while the table animates.
    if (phase == Phase::Think && mayThink && !aiDone)
        aiDone = ai::step(game == OMAHA ? 1 : 16);
    if (wait) { wait--; return; }
    if (fxBusy) return;
    switch (phase) {
        case Phase::Idle: case Phase::Leave: case Phase::Won: case Phase::Broke:
            break;
        case Phase::HandStart:
            startHand();
            break;
        case Phase::BringIn: {
            uint8_t s = bringInSeat();
            post(s, A_BRINGIN, unit(), true);
            curBet = seats[s].bet;
            seats[s].acted = 1;
            actor = s;
            nextActor(s);
            go(actor == 0xFF ? Phase::EndStreet : Phase::Betting, P_ACTION);
            break;
        }
        case Phase::Betting:
            if (actor == 0xFF) { go(Phase::EndStreet); break; }
            emit(Ev::Turn, actor);
            if (actor == YOU && !demo) {
                sel = B_CALL;
                raiseTo = minTo(YOU);
                accel = 0;
                go(Phase::Human);
            } else {
                makeView(actor);
                ai::begin(view, rand32(), ai::samplesFor(level, game));
                aiDone = false;
                uint8_t base = opt.pace ? 10 : 22;
                thinkT = (uint8_t)(live(YOU) || demo ? base + rand32() % base : P_FOLDED);
                go(Phase::Think);
            }
            break;
        case Phase::Think:
            if (thinkT) { thinkT--; break; }
            if (!aiDone) break;
            cpuAct();
            break;
        case Phase::Human:
            humanInput(pressed, repeat);
            break;
        case Phase::EndStreet:
            endStreet();
            afterStreet();
            break;
        case Phase::RunOut:
            endStreet();
            afterStreet();
            break;
        case Phase::Draw: {
            // Each player in turn from the button's left, all-in ones too.
            uint8_t s = 0xFF;
            for (uint8_t i = 1; i <= SEATS && s == 0xFF; i++) {
                uint8_t t = (uint8_t)((button + i) & 3);
                if (live(t) && seats[t].drew == 0xFF) s = t;
            }
            if (s == 0xFF) { street = 1; startStreet(); break; }
            emit(Ev::Turn, s);
            if (s == YOU && !demo) { glove = 0; drawMask = 0; go(Phase::DrawHuman); break; }
            actor = s;
            go(Phase::DrawThink, opt.pace ? 10 : 24);
            break;
        }
        case Phase::DrawThink:
            replaceCards(actor, ai::discards(seats[actor].cards, level));
            go(Phase::Draw, P_DRAW);
            break;
        case Phase::DrawHuman:
            drawInput(pressed, repeat);
            break;
        case Phase::Showdown:
            showdown();
            break;
        case Phase::Award:
            award();
            break;
        case Phase::HandOver:
        case Phase::Rebuy:
            if (step) { step = 0; endHand(); break; }
            if (demo) break;
            barInput(pressed);
            break;
    }
}
