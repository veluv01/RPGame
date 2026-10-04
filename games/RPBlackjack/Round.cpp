#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in the RPGame library and RPGfx)
// Derived from Press-Play-On-Tape/Blackjack (Apache-2.0),
// PlayGameState_Play.cpp / _Utils.cpp / _Buttons.cpp. Modified 2026 for
// RPGame by bateske (see Round.h and NOTICE).
#include "Round.h"
#include <rpgame/Input.h>   // the button masks only, so the rules build on the PC for the host tests

// Frames at 60 fps; PPOT dealt a card every 15.
enum : uint16_t {
    T_DEAL = 14, T_PEEK = 50, T_PEEK_RESULT = 30, T_BUST = 50, T_SETTLE = 40,
    T_REVEAL = 22, T_SHUFFLE = 70, T_SPLIT = 16, T_END = 30,
};

static const uint8_t CHIPS[4] = {1, 5, 10, 25};
static const uint16_t MAX_BET = 200;

// ---------------------------------------------------------------------------
// Hands
// ---------------------------------------------------------------------------
void Hand::clear() {
    count = 0; bet = 0;
    doubled = bust = stood = fromSplit = false;
}

uint8_t Hand::hard() const {
    uint8_t t = 0;
    for (uint8_t i = 0; i < count; i++) t = (uint8_t)(t + cardValue(cards[i]));
    return t;
}

uint8_t Hand::best() const {
    uint8_t t = hard();
    bool ace = false;
    for (uint8_t i = 0; i < count; i++) if (cardRank(cards[i]) == 0) ace = true;
    return (ace && t <= 11) ? (uint8_t)(t + 10) : t;
}

// ---------------------------------------------------------------------------
// Shoe
// ---------------------------------------------------------------------------
uint32_t Round::rand32() {
    if (!rng) rng = 0x9E3779B9u;
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

void Round::shuffle() {
    shoeLen = (uint16_t)(decks() * 52);
    for (uint16_t i = 0; i < shoeLen; i++) shoe[i] = (uint8_t)(i % 52);
    for (uint16_t i = shoeLen - 1; i > 0; i--) {
        uint16_t j = (uint16_t)(rand32() % (i + 1u));
        uint8_t t = shoe[i]; shoe[i] = shoe[j]; shoe[j] = t;
    }
    shoePos = 0;
    // Cut card around three quarters deep, like a real six-deck game.
    cutAt = (uint16_t)(shoeLen * 3 / 4 - (rand32() % 16));
}

uint8_t Round::draw() {
    if (nStacked) {
        uint8_t c = stacked[0];
        for (uint8_t i = 1; i < nStacked; i++) stacked[i - 1] = stacked[i];
        nStacked--;
        return c;
    }
    if (shoePos >= shoeLen) shuffle();      // never happens with the cut card
    return shoe[shoePos++];
}

void Round::stackDeck(const uint8_t *cards, uint8_t n) {
    if (n > sizeof stacked) n = sizeof stacked;
    for (uint8_t i = 0; i < n; i++) stacked[i] = cards[i];
    nStacked = n;
}

uint8_t Round::shoeLeftPercent() const {
    if (!shoeLen) return 100;
    return (uint8_t)(100u * (uint32_t)(shoeLen - shoePos) / shoeLen);
}

void Round::deal(uint8_t seat, bool faceDown, bool sideways) {
    Hand &h = seat ? hands[seat - 1] : dealer;
    if (h.count >= 12) return;
    uint8_t c = draw();
    h.cards[h.count] = c;
    emit(Ev::Deal, seat, h.count, c, (int16_t)((faceDown ? 1 : 0) | (sideways ? 2 : 0)));
    h.count++;
}

// ---------------------------------------------------------------------------
// Plumbing
// ---------------------------------------------------------------------------
void Round::emit(Ev t, uint8_t a, uint8_t b, uint8_t c, int16_t amount) {
    if (qLen >= 16) return;
    Event &e = q[(qHead + qLen) & 15];
    e.type = t; e.a = a; e.b = b; e.c = c; e.amount = amount;
    qLen++;
}

bool Round::popEvent(Event &e) {
    if (!qLen) return false;
    e = q[qHead];
    qHead = (uint8_t)((qHead + 1) & 15);
    qLen--;
    return true;
}

void Round::go(Phase p, Bar b, uint8_t s) {
    phase = p; bar = b; sel = s; step = 0;
    if (b != Bar::None) fixSel();
}

void Round::pace(uint16_t frames) { wait = opt.speed ? (uint16_t)(frames / 2) : frames; }

int32_t Round::goal() const {
    if (opt.goal == GOAL_ENDLESS) return 0x7FFFFFFF;
    return opt.goal == GOAL_5000 ? 5000 : 1000;
}

uint16_t Round::insuranceMax() const {
    uint16_t half = (uint16_t)(initBet / 2);
    return (int32_t)half > purse ? (uint16_t)purse : half;
}

void Round::newGame() {
    purse = 500;
    lastBet = 0;
    shoeLen = 0;
    qLen = 0;
    wait = 0;
    dealer.clear(); hands[0].clear(); hands[1].clear();
    go(Phase::StartHand);
    say(L_WELCOME, F_SMILE);
}

void Round::resume() {
    int32_t p = purse;
    newGame();
    purse = p;
    qLen = 0;
    say(L_GOOD_LUCK, F_SMILE);
}

// ---------------------------------------------------------------------------
// Buttons (PlayGameState_Buttons.cpp, generalised)
// ---------------------------------------------------------------------------
uint8_t Round::slotCount() const {
    switch (bar) {
        case Bar::Bet: return BET_SLOTS;
        case Bar::Play: return PLAY_SLOTS;
        case Bar::Insurance: case Bar::End: return 2;
        default: return 0;
    }
}

bool Round::slotEnabled(uint8_t s) const {
    switch (bar) {
        case Bar::Bet:
            if (s <= B_25) return initBet + CHIPS[s] <= MAX_BET && purse >= CHIPS[s];
            return initBet > 0;
        case Bar::Play: {
            const Hand &h = hands[active];
            switch (s) {
                case P_HIT: case P_STAND: return true;
                case P_DOUBLE: return h.count == 2 && !h.natural() && purse >= initBet;
                case P_SPLIT: return nHands == 1 && h.count == 2 &&
                                     cardRank(h.cards[0]) == cardRank(h.cards[1]) && purse >= initBet;
            }
            return false;
        }
        case Bar::Insurance: return s == I_NO || insureAmt > 0;
        case Bar::End: return true;
        default: return false;
    }
}

void Round::fixSel() {
    uint8_t n = slotCount();
    if (!n) return;
    if (sel >= n) sel = 0;
    if (slotEnabled(sel)) return;
    for (int8_t d = 1; d < n; d++) {                // nearest enabled, left first
        if (sel >= d && slotEnabled((uint8_t)(sel - d))) { sel = (uint8_t)(sel - d); return; }
        if (sel + d < n && slotEnabled((uint8_t)(sel + d))) { sel = (uint8_t)(sel + d); return; }
    }
}

void Round::moveSel(int8_t dir) {
    uint8_t n = slotCount();
    for (int s = sel + dir; s >= 0 && s < n; s += dir)
        if (slotEnabled((uint8_t)s)) { sel = (uint8_t)s; emit(Ev::Cursor); return; }
}

void Round::betInput(uint8_t pressed, uint8_t repeat) {
    if (repeat & LEFT_BUTTON) moveSel(-1);
    if (repeat & RIGHT_BUTTON) moveSel(1);
    bool add = (repeat & (A_BUTTON | UP_BUTTON)) && sel <= B_25;
    bool take = (repeat & (B_BUTTON | DOWN_BUTTON)) && sel <= B_25;
    if (add) {
        if (slotEnabled(sel)) {
            uint8_t v = CHIPS[sel];
            initBet = (uint16_t)(initBet + v);
            purse -= v;
            hands[0].bet = initBet;
            emit(Ev::BetAdd, 1, sel, 0, v);
        } else emit(Ev::Deny);
    } else if (take) {
        uint8_t v = CHIPS[sel];
        if (initBet >= v) {
            initBet = (uint16_t)(initBet - v);
            purse += v;
            hands[0].bet = initBet;
            emit(Ev::BetRemove, 1, sel, 0, v);
        } else emit(Ev::Deny);
    } else if ((pressed & A_BUTTON && sel == B_CLEAR) && initBet) {
        purse += initBet;
        emit(Ev::BetRemove, 1, 0, 0, (int16_t)initBet);
        initBet = 0;
        hands[0].bet = 0;
        sel = B_5;
    } else if (((pressed & A_BUTTON) && sel == B_DEAL) || (pressed & START_BUTTON)) {
        if (initBet) {
            lastBet = initBet;
            go(Phase::InitDeal);
            return;
        }
        emit(Ev::Deny);
    }
    fixSel();
}

void Round::playInput(uint8_t pressed) {
    if (pressed & LEFT_BUTTON) moveSel(-1);
    if (pressed & RIGHT_BUTTON) moveSel(1);
    uint8_t action = 0xFF;
    if (pressed & A_BUTTON) action = sel;
    if (pressed & UP_BUTTON) action = P_HIT;        // shortcuts
    if (pressed & DOWN_BUTTON) action = P_STAND;
    if (action == 0xFF) return;
    if (!slotEnabled(action)) { emit(Ev::Deny); return; }
    Hand &h = cur();
    switch (action) {
        case P_HIT:
            deal((uint8_t)(active + 1));
            if (h.hard() > 21) go(Phase::Bust);
            else pace(T_DEAL);
            break;
        case P_STAND:
            nextHand();
            break;
        case P_DOUBLE:
            go(Phase::DoubleUp);
            break;
        case P_SPLIT:
            go(Phase::SplitCards);
            break;
    }
}

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------
bool Round::dealerShouldHit() const {
    if (opt.rules == RULES_CASINO) return dealer.best() < 17;         // stands on all 17s
    return dealer.hard() <= 16 && dealer.best() != 21;                // PPOT's dealer
}

void Round::nextHand() {
    hands[active].stood = true;
    if (active == 0 && nHands == 2) {
        active = 1;
        go(Phase::PlayHand, Bar::Play, P_HIT);
        return;
    }
    go(Phase::RevealHole);
}

void Round::settleHand(uint8_t i) {
    Hand &h = hands[i];
    if (h.bust) return;
    uint8_t p = h.best(), d = dealer.best();
    bool pn = h.natural(), dn = dealer.natural();
    Result r;
    if (opt.rules == RULES_CASINO) {
        if (pn && !dn) r = R_BLACKJACK;
        else if (dn && !pn) r = R_LOSE;
        else if (pn && dn) r = R_PUSH;
        else if (d > 21 || p > d) r = R_WIN;
        else if (p == d) r = R_PUSH;
        else r = R_LOSE;
    } else {                                   // PPOT: totals decide, 3:2 on a winning natural
        if (d > 21 || p > d) r = pn ? R_BLACKJACK : R_WIN;
        else if (p == d) r = R_PUSH;
        else r = R_LOSE;
    }
    int32_t ret = 0;
    switch (r) {
        case R_BLACKJACK: ret = h.bet + h.bet * 3 / 2; stats.won++; stats.blackjacks++; break;
        case R_WIN:       ret = 2 * h.bet; stats.won++; break;
        case R_PUSH:      ret = h.bet; stats.pushed++; break;
        default:          stats.lost++; break;
    }
    purse += ret;
    if (ret - h.bet > stats.biggestWin) stats.biggestWin = ret - h.bet;
    emit(Ev::Settle, (uint8_t)(i + 1), r, 0, (int16_t)ret);
    static const uint8_t LINES[4] = {L_DEALER_WINS, L_PUSH, L_YOU_WIN, L_PLAYER_BJ};
    static const uint8_t FACES[4] = {F_SMILE, F_RAISED, F_ANGRY, F_SURPRISED};
    say(LINES[r], FACES[r]);
}

const char *Round::lineText(uint8_t line) const {
    static const char *const TEXT[LINE_COUNT] = {
        "Welcome to\nthe table!",       // L_WELCOME
        "Place your\nbet.",             // L_PLACE_BET
        "Insurance?",                   // L_INSURANCE
        "Let me\npeek...",              // L_PEEK
        "Dealer has\nBlackjack!",       // L_DEALER_BJ
        "No blackjack\nPlay on!",       // L_NO_BJ
        "You lose the\ninsurance.",     // L_INS_LOST
        "Insurance\npays 2 to 1!",      // L_INS_PAYS
        "Blackjack!\nYou win!",         // L_PLAYER_BJ
        "You win!",                     // L_YOU_WIN
        "Push.",                        // L_PUSH
        "You are\nbusted!",             // L_BUST
        "House wins.",                  // L_DEALER_WINS
        "Dealer\nbusts!",               // L_DEALER_BUSTS
        "Shuffling\nthe shoe...",       // L_SHUFFLE
        "Double down!",                 // L_DOUBLE
        "Split!",                       // L_SPLIT
        "Two\nblackjacks!",             // L_BOTH_BJ
        "Good luck!",                   // L_GOOD_LUCK
        "Twenty-one!",                  // L_TWENTY_ONE
    };
    return line < LINE_COUNT ? TEXT[line] : "";
}

// ---------------------------------------------------------------------------
// The flow (PPOT's ViewState machine)
// ---------------------------------------------------------------------------
void Round::update(uint8_t pressed, uint8_t repeat, bool fxBusy) {
    if (wait) { wait--; return; }

    switch (phase) {

        case Phase::StartHand:
            dealer.clear(); hands[0].clear(); hands[1].clear();
            nHands = 1; active = 0; insurance = 0; insureAmt = 0;
            holeShown = false; dealerPeeked = false;
            if (!shoeLen || opt.rules == RULES_CLASSIC || shoePos >= cutAt) {
                bool show = shoeLen != 0 || opt.rules == RULES_CASINO;
                shuffle();
                emit(Ev::Shuffle, 0, 0, 0, decks());
                if (show && opt.rules == RULES_CASINO) {
                    say(L_SHUFFLE, F_NORMAL);
                    go(Phase::Shuffle);
                    pace(T_SHUFFLE);
                    return;
                }
            }
            initBet = 0;
            // Offer the last bet again; CLR or B takes it back.
            if (lastBet && lastBet <= MAX_BET && purse >= lastBet) {
                initBet = lastBet;
                purse -= lastBet;
                hands[0].bet = initBet;
                emit(Ev::BetAdd, 1, 0xFF, 0, (int16_t)lastBet);
            }
            go(Phase::InitBet, Bar::Bet, initBet ? B_DEAL : B_5);
            break;

        case Phase::Shuffle:
            if (fxBusy) return;
            phase = Phase::StartHand;
            break;

        case Phase::InitBet:
            betInput(pressed, repeat);
            break;

        case Phase::InitDeal:
            // Standard order: player, dealer up, player, dealer hole.
            switch (step) {
                case 0: deal(1); break;
                case 1: deal(0); break;
                case 2: deal(1); break;
                case 3: deal(0, true); break;
                default: {
                    if (fxBusy) return;
                    stats.hands++;
                    uint8_t up = dealer.cards[0];
                    if (cardRank(up) == 0 && purse >= 1 && initBet >= 2) {
                        insureAmt = insuranceMax();
                        say(L_INSURANCE, F_RAISED);
                        go(Phase::OfferInsurance, Bar::Insurance, I_NO);
                    } else if (opt.rules == RULES_CASINO && cardValue(up) == 10) {
                        emit(Ev::PeekStart);
                        go(Phase::Peeking);
                        pace(T_PEEK);
                    } else {
                        go(Phase::PeekResult);
                    }
                    return;
                }
            }
            step++;
            pace(T_DEAL);
            break;

        case Phase::OfferInsurance:
            if (pressed & (LEFT_BUTTON | RIGHT_BUTTON)) { sel = sel == I_YES ? I_NO : I_YES; fixSel(); emit(Ev::Cursor); }
            if (repeat & UP_BUTTON) { if (insureAmt < insuranceMax()) { insureAmt++; emit(Ev::Cursor); } else emit(Ev::Deny); }
            if (repeat & DOWN_BUTTON) { if (insureAmt > 1) { insureAmt--; emit(Ev::Cursor); } else emit(Ev::Deny); }
            if (pressed & A_BUTTON) {
                if (sel == I_YES && insureAmt) {
                    insurance = insureAmt;
                    purse -= insurance;
                    emit(Ev::Insure, 0, 0, 0, (int16_t)insurance);
                }
                emit(Ev::PeekStart);
                go(Phase::Peeking);
                pace(T_PEEK);
            }
            break;

        case Phase::Peeking:
            dealerPeeked = true;
            if (dealer.natural()) {
                holeShown = true;
                emit(Ev::Reveal);
                emit(Ev::PeekEnd, 1);
            } else {
                emit(Ev::PeekEnd, 0);
                if (insurance) { emit(Ev::Insurance, 0, 0, 0, 0); say(L_INS_LOST, F_RAISED); insurance = 0; }
                else say(L_NO_BJ, F_NORMAL);
            }
            go(Phase::PeekResult);
            pace(T_PEEK_RESULT);
            break;

        case Phase::PeekResult:
            if (fxBusy) return;
            if (dealerPeeked && dealer.natural()) {
                // PPOT paid insurance 1:1 and nothing on a partial bet; 2:1 here.
                if (insurance) {
                    int32_t ret = (int32_t)insurance * 3;
                    purse += ret;
                    emit(Ev::Insurance, 0, 1, 0, (int16_t)ret);
                }
                settleHand(0);
                if (hands[0].natural()) say(L_BOTH_BJ, F_SURPRISED);
                else say(insurance ? L_INS_PAYS : L_DEALER_BJ, F_SMILE);
                insurance = 0;
                go(Phase::OverallWinOrLose);
                pace(T_END);
                return;
            }
            if (hands[0].natural()) emit(Ev::Natural, 1);
            go(Phase::PlayHand, Bar::Play, P_HIT);
            break;

        case Phase::PlayHand: {
            if (fxBusy) return;
            Hand &h = cur();
            // Nothing left to decide: blackjack, 21, a doubled hand, split aces.
            bool splitAces = h.fromSplit && cardRank(h.cards[0]) == 0;
            if (h.natural() || h.best() == 21 || h.doubled || h.count >= 12 || (splitAces && h.count >= 2)) {
                if (h.best() == 21 && !h.natural()) emit(Ev::Hand21, (uint8_t)(active + 1));
                nextHand();
                return;
            }
            playInput(pressed);
            break;
        }

        case Phase::SplitCards:
            switch (step) {
                case 0:
                    hands[1].clear();
                    hands[1].cards[0] = hands[0].cards[1];
                    hands[1].count = 1;
                    hands[1].bet = initBet;
                    hands[1].fromSplit = true;
                    hands[0].count = 1;
                    hands[0].fromSplit = true;
                    purse -= initBet;
                    nHands = 2;
                    emit(Ev::Split, 0, 0, 0, (int16_t)initBet);
                    say(L_SPLIT, F_RAISED);
                    pace(T_SPLIT);
                    break;
                case 1: deal(1); pace(T_DEAL); break;
                case 2: deal(2); pace(T_DEAL); break;
                default:
                    if (fxBusy) return;
                    active = 0;
                    go(Phase::PlayHand, Bar::Play, P_HIT);
                    return;
            }
            step++;
            break;

        case Phase::DoubleUp: {
            Hand &h = cur();
            if (step == 0) {
                h.doubled = true;
                h.bet = (uint16_t)(h.bet + initBet);
                purse -= initBet;
                emit(Ev::Double, (uint8_t)(active + 1), 0, 0, (int16_t)initBet);
                say(L_DOUBLE, F_RAISED);
                deal((uint8_t)(active + 1), false, true);
                step = 1;
                pace(T_DEAL);
                return;
            }
            if (fxBusy) return;
            if (h.hard() > 21) go(Phase::Bust);
            else nextHand();
            break;
        }

        case Phase::Bust:
            if (step == 0) {
                if (fxBusy) return;
                cur().bust = true;
                stats.lost++;
                emit(Ev::Bust, (uint8_t)(active + 1), 0, 0, (int16_t)cur().bet);
                say(L_BUST, F_RAISED);
                step = 1;
                pace(T_BUST);
                return;
            }
            nextHand();
            break;

        case Phase::RevealHole: {
            if (fxBusy) return;
            if (!holeShown) {
                holeShown = true;
                emit(Ev::Reveal);
                pace(T_REVEAL);
                return;
            }
            // Does the dealer need to draw? Not against busts, and in casino
            // rules not against a blackjack either (it is paid straight away).
            bool play = false;
            for (uint8_t i = 0; i < nHands; i++) {
                const Hand &h = hands[i];
                if (h.bust) continue;
                if (opt.rules == RULES_CASINO && h.natural()) continue;
                play = true;
            }
            go(play ? Phase::PlayDealerHand : Phase::CheckForWins);
            break;
        }

        case Phase::PlayDealerHand:
            if (fxBusy) return;
            if (dealerShouldHit() && dealer.count < 12) {
                deal(0);
                pace(T_DEAL);
                return;
            }
            if (dealer.best() > 21) { emit(Ev::DealerBust); say(L_DEALER_BUSTS, F_ANGRY); pace(T_SETTLE); }
            go(Phase::CheckForWins);
            if (dealer.best() > 21) pace(T_SETTLE);
            break;

        case Phase::CheckForWins:
            if (fxBusy) return;
            // One hand at a time, with a beat between (PPOT: 16/33 frames).
            while (step < nHands && hands[step].bust) step++;
            if (step < nHands) {
                settleHand(step);
                step++;
                pace(T_SETTLE);
                return;
            }
            go(Phase::OverallWinOrLose);
            break;

        case Phase::OverallWinOrLose:
            if (fxBusy) return;
            if (purse > stats.bestPurse) stats.bestPurse = purse;
            if (purse >= goal()) {
                stats.gamesWon++;
                emit(Ev::GameOver, 1);
                go(Phase::GameWon);
            } else if (purse < 1) {              // PPOT skipped this after a peek loss
                stats.gamesBroke++;
                emit(Ev::GameOver, 0);
                go(Phase::GameLost);
            } else {
                go(Phase::EndOfGame, Bar::End, E_CONTINUE);
            }
            break;

        case Phase::EndOfGame:
            if (pressed & (LEFT_BUTTON | RIGHT_BUTTON)) { sel = sel ? 0 : 1; emit(Ev::Cursor); }
            if (pressed & START_BUTTON) { go(Phase::StartHand); break; }
            if (pressed & A_BUTTON) go(sel == E_CONTINUE ? Phase::StartHand : Phase::Quit);
            break;

        default:
            break;
    }
}
