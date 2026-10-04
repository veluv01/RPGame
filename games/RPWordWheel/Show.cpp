#pragma GCC optimize("Os", "no-ipa-sra", "no-inline-functions-called-once", "no-jump-tables", "no-guess-branch-probability")   // cold code: size over speed
// The show's rules and flow (Show.h). The phase machine, pacing and event
// queue follow CHBlackjack's Round.
#include <string.h>
#include "Show.h"
#include "Cpu.h"
#include <rpgame/Input.h>  // the button masks and
#include <rpgame/Fmt.h>    // the number formatting only, so the rules build on the PC for the host tests

using namespace pz;

// Frames at 60 fps (halved at QUICK pace).
enum : uint16_t { T_INTRO = 40, T_THINK = 45, T_PICK = 35, T_GIVEN = 28, T_TOSS = 42, T_END = 150 };
enum : uint8_t { DPAD = UP_BUTTON | DOWN_BUTTON | LEFT_BUTTON | RIGHT_BUTTON };
enum : uint8_t { T_WILD = 1, T_PRIZE = 2, T_MYST = 4 };          // Show::taken

static const uint8_t FULL[] = {SK_TOSS, SK_ROUND, SK_ROUND, SK_TOSS, SK_ROUND, SK_BONUS, SK_END};
static const uint8_t QUICK[] = {SK_TOSS, SK_ROUND, SK_BONUS, SK_END};

// The bonus wheel's 24 envelopes, in units of $5,000.
static const uint8_t ENVELOPE[wedge::COUNT] = {
    5, 6, 5, 8, 5, 6, 5, 10, 5, 6, 5, 8, 5, 20, 5, 6, 5, 8, 5, 6, 5, 10, 5, 6,
};

// ---------------------------------------------------------------------------
// Plumbing
// ---------------------------------------------------------------------------
uint32_t Show::rand32() {
    if (!rng) rng = 0x9E3779B9u;
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

uint8_t Show::nextStop() {
    if (nForced) {
        uint8_t s = forced[0];
        memmove(forced, forced + 1, --nForced);
        return (uint8_t)(s % wedge::STOPS);
    }
    return (uint8_t)(rand32() % wedge::STOPS);
}

void Show::forceStop(uint8_t s) {
    if (nForced < sizeof forced) forced[nForced++] = s;
}

void Show::emit(Ev t, uint8_t a, uint8_t b, uint8_t c, int32_t amount) {
    if (qLen >= 16) { qDropped++; return; }
    Event &e = q[(qHead + qLen) & 15];
    e.type = t; e.a = a; e.b = b; e.c = c; e.amount = amount;
    qLen++;
}

void Show::say(uint8_t line, uint8_t face, uint8_t letter, int32_t number) {
    emit(Ev::Say, line, face, letter, number);
}

bool Show::popEvent(Event &e) {
    if (!qLen) return false;
    e = q[qHead];
    qHead = (uint8_t)((qHead + 1) & 15);
    qLen--;
    return true;
}

void Show::pace(uint16_t frames) { wait = opt.pace == PACE_QUICK ? (uint16_t)(frames >> 1) : frames; }

uint16_t Show::seconds(uint8_t s) const {
    return (uint16_t)(s * 60 * (opt.clock == CLOCK_RELAXED ? 2 : 1));
}

// Where the episode is, worked out once a step (the screen asks all the time).
void Show::syncStep() {
    bool quick = opt.game == GAME_QUICK;
    uint8_t n = quick ? sizeof QUICK : sizeof FULL;
    const uint8_t *steps = quick ? QUICK : FULL;
    if (stepIdx >= n) stepIdx = (uint8_t)(n - 1);
    stepNow = steps[stepIdx];
    roundNow = 0;
    for (uint8_t i = 0; i <= stepIdx; i++) roundNow += steps[i] == SK_ROUND;
    if (!roundNow) roundNow = 1;
    layoutNow = quick ? 2 : (uint8_t)(roundNow - 1);
}

wedge::Wedge Show::wedgeAt(uint8_t index) const {
    wedge::Wedge w = wedge::at(layout(), index);
    if (stepKind() == SK_BONUS) { w.kind = wedge::ENVELOPE; w.value = 0; }
    else if ((w.kind == wedge::WILD && (taken & T_WILD)) || (w.kind == wedge::PRIZE && (taken & T_PRIZE)) ||
             (w.kind == wedge::MYSTERY && (taken & T_MYST)))
        w.kind = wedge::MONEY;
    return w;
}

uint8_t Show::humans() const { return (uint8_t)(human(0) + human(1) + human(2)); }

const char *Show::name(uint8_t p, char *buf) const {
    static const char CPU[3][5] = {"ACE", "DOT", "BUZZ"};
    if (p > 2) p = 0;
    if (kind[p] != P_HUMAN) return CPU[(kind[p] - 1) % 3];
    if (humans() == 1) return "YOU";
    buf[0] = 'P'; buf[1] = (char)('1' + p); buf[2] = 0;
    return buf;
}

uint8_t Show::leader() const {
    uint8_t b = 0;
    for (uint8_t p = 1; p < 3; p++)
        if (total[p] > total[b]) b = p;
    return b;
}

uint32_t Show::pickAllowed() const {
    uint32_t free = ALL & ~puzzle.used;
    if (phase == Phase::Entry || phase == Phase::Confirm) return free;
    if (pickMode == PM_CONSONANT) return free & ~VOWELS;
    if (pickMode == PM_VOWEL) return free & VOWELS;
    return free;
}

// ---------------------------------------------------------------------------
// The episode
// ---------------------------------------------------------------------------
void Show::newEpisode() {
    for (uint8_t p = 0; p < 3; p++) { total[p] = cash[p] = 0; wild[p] = prize[p] = false; }
    stepIdx = 0;
    syncStep();
    starter = 0;
    winner = NOBODY;
    qHead = qLen = 0;
    nForced = 0;
    go(Phase::Intro);
    say(L_WELCOME, F_SMILE);
    pace(T_INTRO);
}

void Show::resume() {
    for (uint8_t p = 0; p < 3; p++) { cash[p] = 0; wild[p] = prize[p] = false; }
    qHead = qLen = 0;
    wait = 0;
    beginStep();
}

void Show::beginStep() {
    syncStep();
    finalMode = false;
    freePlay = false;
    wildValue = 0;
    switch (stepKind()) {
        case SK_TOSS:
            tossValue = stepIdx ? 2000 : 1000;
            locked = 0;
            emit(Ev::NeedPuzzle, SEC_TOSS);
            go(Phase::Load);
            break;
        case SK_ROUND:
            for (uint8_t p = 0; p < 3; p++) { cash[p] = 0; prize[p] = false; }
            taken = (wild[0] || wild[1] || wild[2]) ? T_WILD : 0;
            turns = 0;
            cur = starter;
            emit(Ev::NeedPuzzle, SEC_ROUND);
            go(Phase::Load);
            break;
        case SK_BONUS:
            for (uint8_t p = 0; p < 3; p++) cash[p] = 0;
            cur = winner = leader();
            say(L_BONUS, F_SMILE);
            power = 0;
            go(Phase::Charge);
            break;
        default: {
            winner = leader();
            uint32_t best = 0;
            for (uint8_t p = 0; p < 3; p++) {
                if (!human(p)) continue;
                stats.career += (uint32_t)total[p];
                if ((uint32_t)total[p] > best) best = (uint32_t)total[p];
            }
            if (humans()) {
                stats.episodes++;
                if (human(winner)) stats.wins++;
                if (best > stats.bestEpisode) stats.bestEpisode = best;
            }
            say(L_GOODNIGHT, F_SMILE);
            emit(Ev::GameOver, winner);
            go(Phase::EpisodeEnd);
        }
    }
}

void Show::nextStep() {
    stepIdx++;
    beginStep();
}

void Show::supply(const char *text, const char *category) {
    if (phase != Phase::Load) return;
    puzzle.set(text, category);
    cpu::newPuzzle(*this);
    emit(Ev::PuzzleUp, stepKind());
    switch (stepKind()) {
        case SK_TOSS:
            say(L_TOSSUP, F_NORMAL, 0, tossValue);
            tossTimer = T_TOSS;
            go(Phase::TossReveal);
            break;
        case SK_BONUS:
            say(L_GIVEN, F_NORMAL);
            go(Phase::BonusGiven);
            pace(T_THINK);
            break;
        default:
            say(L_ROUND, F_NORMAL, cur, roundNo());
            turnStart();
    }
}

// ---------------------------------------------------------------------------
// Turns at the wheel
// ---------------------------------------------------------------------------
static uint8_t menuDefault(const Show &s) {
    uint32_t hid = s.puzzle.hiddenMask();
    if (hid & ~VOWELS) return A_SPIN;
    return (hid & VOWELS) && s.cash[s.cur] >= VOWEL_COST ? A_VOWEL : A_SOLVE;
}

void Show::turnStart() {
    wildValue = 0;
    freePlay = false;
    if (stepKind() == SK_ROUND && layout() == 2 && !finalMode && turns >= FINAL_SPINS) {
        startFinal();
        return;
    }
    emit(Ev::TurnTo, cur);
    if (finalMode) {
        pickMode = PM_ANY;
        value = finalValue;
        flat = false;
        pending = 0;
        go(Phase::PickLetter);
        pickerSettle();
        return;
    }
    menuSel = menuDefault(*this);
    go(Phase::TurnMenu);
}

void Show::nextTurn() {
    cur = (uint8_t)((cur + 1) % 3);
    turnStart();
}

// The bell: the host spins once for everyone. Bankrupts and the like are
// spun again, off camera.
void Show::startFinal() {
    finalMode = true;
    emit(Ev::FinalBell);
    say(L_FINAL, F_SURPRISED);
    uint8_t s, k, tries = 0;
    do {
        s = nextStop();
        k = wedgeAt(s / 3).kind;
    } while (k != wedge::MONEY && k != wedge::TOP && ++tries < 200);
    if (tries >= 200) s = 4;                    // (a $600 wedge: forced stops can't starve it)
    stop = s;
    spinBy = HOST;
    emit(Ev::SpinStart, HOST, s, 180);
    go(Phase::Spinning);
}

void Show::doSpin(uint8_t pow) {
    bool bonus = stepKind() == SK_BONUS;
    stop = nextStop();
    if (bonus) stop = (uint8_t)(stop / 3 * 3 + 1);      // an envelope's middle
    spinBy = cur;
    power = pow;
    if (turns < 255) turns++;
    emit(Ev::SpinStart, cur, stop, pow, bonus);
    go(Phase::Spinning);
}

void Show::bankrupt(uint8_t p) {
    int32_t lost = cash[p];
    cash[p] = 0;
    wild[p] = prize[p] = false;
    if (human(p)) stats.bankrupts++;
    emit(Ev::Bankrupt, p, 0, 0, lost);
    say(L_BANKRUPT, F_SURPRISED);
}

void Show::land() {
    if (stepKind() == SK_BONUS) {
        bonusPrize = ENVELOPE[stop / 3] * 5000u;
        emit(Ev::Envelope, (uint8_t)(stop / 3));
        emit(Ev::NeedPuzzle, SEC_BONUS);
        go(Phase::Load);
        return;
    }
    wedge::Wedge w = wedgeAt((uint8_t)(stop / 3));
    uint8_t k = w.kind;
    if (k == wedge::THIRDS && stop % 3 != 1) k = wedge::BANKRUPT;
    emit(Ev::Landed, stop, k, 0, w.value);
    if (spinBy == HOST) {
        spinBy = cur;
        finalValue = (uint16_t)(w.value + 1000);
        emit(Ev::FinalValue, 0, 0, 0, finalValue);
        say(L_FINAL_VALUE, F_NORMAL, 0, finalValue);
        turnStart();
        return;
    }
    flat = false;
    pending = 0;
    pickMode = PM_CONSONANT;
    switch (k) {
        case wedge::BANKRUPT: bankrupt(cur); nextTurn(); return;
        case wedge::LOSE:
            emit(Ev::LoseTurn, cur);
            say(L_LOSE, F_RAISED);
            nextTurn();
            return;
        case wedge::FREE:
            freePlay = true;
            pickMode = PM_ANY;
            say(L_FREE, F_SMILE);
            break;
        case wedge::THIRDS: flat = true; break;
        case wedge::MYSTERY: case wedge::WILD: case wedge::PRIZE: pending = k; break;
        case wedge::TOP:
            if (human(cur) && w.value > stats.topWedge) stats.topWedge = w.value;
            break;
    }
    value = w.value;
    go(Phase::PickLetter);
    pickerSettle();
}

// The host says so when the last consonant, or the last vowel, is turned.
void Show::announceLeft(uint32_t before) {
    uint32_t now = puzzle.hiddenMask();
    if (!now) return;
    if ((before & ~VOWELS) && !(now & ~VOWELS)) say(L_ONLY_VOWELS, F_RAISED);
    if ((before & VOWELS) && !(now & VOWELS)) say(L_NO_VOWELS, F_NORMAL);
}

void Show::call(char letter) {
    if (!isLetter(letter)) return;
    bool vowel = isVowel(letter), buying = pickMode == PM_VOWEL, free = freePlay;
    freePlay = false;
    wildValue = 0;
    if (puzzle.used & maskOf(letter)) {
        emit(Ev::NoLetter, (uint8_t)letter, 1, cur);
        say(L_REPEAT, F_RAISED, (uint8_t)letter);
        if (free) turnStart(); else nextTurn();
        return;
    }
    if (buying) cash[cur] -= VOWEL_COST;
    uint32_t before = puzzle.hiddenMask();
    uint8_t n = puzzle.reveal(letter);
    if (!n) {
        emit(Ev::NoLetter, (uint8_t)letter, 0, cur);
        say(L_NONE, F_NORMAL, (uint8_t)letter);
        if (free) turnStart(); else nextTurn();
        return;
    }
    // The mystery wedge: the money waits on the choice (unless that letter
    // finished the puzzle).
    bool choice = pending == wedge::MYSTERY && !vowel && puzzle.nHidden;
    int32_t gain = vowel || choice ? 0 : (flat ? (int32_t)value : (int32_t)value * n);
    cash[cur] += gain;
    emit(Ev::Called, (uint8_t)letter, n, cur, gain);
    say(L_COUNT, n >= 3 ? F_SMILE : F_NORMAL, (uint8_t)letter, n);
    announceLeft(before);
    if (choice) {
        hitCount = n;
        say(L_MYSTERY, F_RAISED);
        menuSel = 0;
        go(Phase::MysteryChoice);
        return;
    }
    if (pending == wedge::WILD && !vowel) {
        wild[cur] = true;
        taken |= T_WILD;
        emit(Ev::Token, cur, wedge::WILD, 1);
        say(L_WILD, F_SMILE);
    } else if (pending == wedge::PRIZE && !vowel) {
        prize[cur] = true;
        taken |= T_PRIZE;
        emit(Ev::Token, cur, wedge::PRIZE, 1);
        say(L_PRIZE, F_SMILE);
    }
    if (!vowel && !free && !flat && !finalMode && pending != wedge::MYSTERY) wildValue = value;
    pending = 0;
    afterHit(!vowel);
}

void Show::afterHit(bool) {
    if (!puzzle.nHidden) { roundWon(cur); return; }     // the last panel turned: theirs
    if (finalMode) {
        menuSel = 0;
        clock = seconds(5);
        go(Phase::FinalMenu);
        return;
    }
    menuSel = menuDefault(*this);
    go(Phase::TurnMenu);
}

void Show::mystery(bool flipIt) {
    taken |= T_MYST;
    pending = 0;
    if (!flipIt) {
        int32_t gain = (int32_t)wedge::MYSTERY_VALUE * hitCount;
        cash[cur] += gain;
        emit(Ev::Mystery, cur, 0, 0, gain);
    } else if (chance(50)) {
        cash[cur] += wedge::BIG;
        emit(Ev::Mystery, cur, 1, 0, wedge::BIG);
        say(L_BIG, F_SURPRISED);
    } else {
        emit(Ev::Mystery, cur, 2);
        bankrupt(cur);
        nextTurn();
        return;
    }
    afterHit(true);
}

void Show::choose(uint8_t act) {
    uint32_t hid = puzzle.hiddenMask();
    bool cons = (hid & ~VOWELS) != 0;
    switch (act) {
        case A_SPIN:
            if (!cons) { emit(Ev::Deny, D_NO_CONSONANTS); return; }
            wildValue = 0;
            if (human(cur)) { power = 0; go(Phase::Charge); }
            else doSpin((uint8_t)(60 + rand32() % 180));
            break;
        case A_VOWEL:
            if (!(hid & VOWELS)) { emit(Ev::Deny, D_NO_VOWELS); return; }
            if (cash[cur] < VOWEL_COST) { emit(Ev::Deny, D_NO_CASH); return; }
            wildValue = 0;
            pickMode = PM_VOWEL;
            go(Phase::PickLetter);
            pickerSettle();
            break;
        case A_SOLVE:
            emit(Ev::SolveTry, cur);
            if (human(cur)) startEntry(CTX_ROUND);
            else solveResult(cpu::solveRight(*this), false);
            break;
        case A_WILD:
            if (!wildUsable() || !cons) { emit(Ev::Deny, D_NO_CONSONANTS); return; }
            wild[cur] = false;
            emit(Ev::Token, cur, wedge::WILD, 0);
            value = wildValue;
            wildValue = 0;
            flat = false;
            pending = 0;
            pickMode = PM_CONSONANT;
            go(Phase::PickLetter);
            pickerSettle();
            break;
    }
}

void Show::menuInput(uint8_t pressed, uint8_t repeat) {
    uint8_t n = phase == Phase::TurnMenu ? menuCount() : 2;
    if (menuSel >= n) menuSel = 0;
    if (repeat & (LEFT_BUTTON | RIGHT_BUTTON)) {
        menuSel = (uint8_t)((menuSel + ((repeat & RIGHT_BUTTON) ? 1 : n - 1)) % n);
        emit(Ev::Cursor);
    }
    if (!(pressed & A_BUTTON)) return;
    if (phase == Phase::TurnMenu) choose(menuSel);
    else if (phase == Phase::MysteryChoice) mystery(menuSel == 1);
    else if (menuSel == 0) { emit(Ev::SolveTry, cur); startEntry(CTX_ROUND); }
    else nextTurn();
}

// ---------------------------------------------------------------------------
// The letter picker and solve entry
// ---------------------------------------------------------------------------
void Show::pickerSettle() {
    uint32_t ok = pickAllowed();
    if (!ok) return;
    while (!((ok >> pickCur) & 1)) pickCur = (uint8_t)((pickCur + 1) % 26);
}

// Two rows of thirteen. Left and right run through the letters in order,
// skipping the dead ones; up and down change row, to the nearest live one.
void Show::pickerInput(uint8_t repeat) {
    uint32_t ok = pickAllowed();
    if (!ok) return;
    uint8_t c = pickCur;
    if (repeat & (LEFT_BUTTON | RIGHT_BUTTON)) {
        uint8_t d = (repeat & RIGHT_BUTTON) ? 1 : 25;
        do c = (uint8_t)((c + d) % 26); while (!((ok >> c) & 1));
    } else if (repeat & (UP_BUTTON | DOWN_BUTTON)) {
        uint8_t row = (uint8_t)(c < 13 ? 13 : 0), col = (uint8_t)(c % 13);
        for (uint8_t d = 0; d < 13; d++) {
            if (col + d < 13 && ((ok >> (row + col + d)) & 1)) { c = (uint8_t)(row + col + d); break; }
            if (col >= d && ((ok >> (row + col - d)) & 1)) { c = (uint8_t)(row + col - d); break; }
        }
    }
    if (c != pickCur) { pickCur = c; emit(Ev::Cursor); }
}

void Show::startEntry(uint8_t ctx) {
    entryCtx = ctx;
    memset(guess, 0, sizeof guess);
    blank = puzzle.hiddenCell(0);
    clock = seconds(ctx == CTX_ROUND ? 30 : 20);
    go(Phase::Entry);
    pickerSettle();
}

static bool isBlank(const Show &s, uint8_t i) { return isLetter(s.puzzle.cell[i]) && !s.puzzle.isShown(i); }

void Show::entryInput(uint8_t pressed, uint8_t repeat) {
    pickerInput(repeat);
    if (pressed & SELECT_BUTTON) {
        do blank = (uint8_t)((blank + 1) % CELLS); while (!isBlank(*this, blank));
        emit(Ev::Cursor);
    }
    if (pressed & B_BUTTON) {
        if (!guess[blank]) {
            // Back to the last panel typed; none: they changed their mind.
            int8_t i = (int8_t)blank;
            while (--i >= 0 && !guess[i]) {}
            if (i < 0) {
                if (entryCtx == CTX_BONUS) go(Phase::BonusThink);
                else if (entryCtx == CTX_ROUND && !finalMode) { menuSel = A_SOLVE; go(Phase::TurnMenu); }
                return;
            }
            blank = (uint8_t)i;
        }
        guess[blank] = 0;
        emit(Ev::Type, blank, 0);
    }
    if (pressed & A_BUTTON) {
        guess[blank] = (char)('A' + pickCur);
        emit(Ev::Type, blank, (uint8_t)guess[blank]);
        for (uint8_t d = 1; d <= CELLS; d++) {
            uint8_t i = (uint8_t)((blank + d) % CELLS);
            if (isBlank(*this, i) && !guess[i]) { blank = i; return; }
        }
        go(Phase::Confirm);
    }
}

bool Show::entryRight() const {
    for (uint8_t i = 0; i < CELLS; i++)
        if (isBlank(*this, i) && guess[i] != puzzle.cell[i]) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Solving
// ---------------------------------------------------------------------------
void Show::roundWon(uint8_t p) {
    puzzle.revealAll();
    emit(Ev::SolveRight, p);
    int32_t amount = cash[p];
    minimum = amount < HOUSE_MIN;
    if (minimum) amount = HOUSE_MIN;
    if (prize[p]) amount += PRIZE_VALUE;
    total[p] += amount;
    winner = p;
    wonAmount = amount;
    emit(Ev::RoundWon, p, minimum, SK_ROUND, amount);
    say(L_SOLVED, F_SMILE);
    if (minimum) say(L_MINIMUM, F_NORMAL);
    if (human(p)) {
        stats.solved++;
        if ((uint32_t)amount > stats.bestRound) stats.bestRound = (uint32_t)amount;
    }
    starter = (uint8_t)((starter + 1) % 3);
    go(Phase::RoundEnd);
}

void Show::tossWon(uint8_t p) {
    puzzle.revealAll();
    emit(Ev::SolveRight, p);
    total[p] += tossValue;
    winner = p;
    wonAmount = tossValue;
    minimum = false;
    emit(Ev::RoundWon, p, 0, SK_TOSS, tossValue);
    say(L_SOLVED, F_SMILE);
    if (human(p)) stats.tossups++;
    starter = p;
    go(Phase::RoundEnd);
}

void Show::tossNobody() {
    puzzle.revealAll();
    winner = NOBODY;
    wonAmount = 0;
    emit(Ev::RoundWon, NOBODY, 0, SK_TOSS, 0);
    say(L_NOBODY, F_RAISED);
    go(Phase::RoundEnd);
}

void Show::buzz(uint8_t p) {
    cur = p;
    emit(Ev::Buzz, p);
    if (human(p)) startEntry(CTX_TOSS);
    else solveResult(cpu::tossRight(*this, p), false);
}

void Show::solveResult(bool right, bool timeout) {
    switch (stepKind()) {
        case SK_TOSS:
            if (right) { tossWon(cur); return; }
            locked |= (uint8_t)(1u << cur);
            emit(Ev::SolveWrong, cur, timeout);
            say(timeout ? L_TIME : L_WRONG, F_RAISED);
            if (locked == 7) tossNobody();
            else { tossTimer = T_TOSS; go(Phase::TossReveal); }
            return;
        case SK_BONUS:
            if (right) { bonusDone(true); return; }
            emit(Ev::SolveWrong, cur, timeout);
            if (!think) bonusDone(false);
            else go(Phase::BonusThink);
            return;
        default:
            if (right) { roundWon(cur); return; }
            emit(Ev::SolveWrong, cur, timeout);
            say(timeout ? L_TIME : L_WRONG, F_RAISED);
            nextTurn();
    }
}

void Show::bonusDone(bool won) {
    puzzle.revealAll();
    if (won) {
        total[cur] += (int32_t)bonusPrize;
        if (human(cur)) stats.bonusWins++;
    }
    winner = cur;
    wonAmount = won ? (int32_t)bonusPrize : 0;
    minimum = false;
    emit(Ev::Bonus, won, 0, 0, (int32_t)bonusPrize);
    say(won ? L_BONUS_WIN : L_BONUS_LOSE, won ? F_SMILE : F_RAISED, 0, (int32_t)bonusPrize);
    go(Phase::RoundEnd);
}

// ---------------------------------------------------------------------------
// Toss-ups
// ---------------------------------------------------------------------------
// One human: any button buzzes. Two: the D-pad and A/B. Three: the D-pad,
// SELECT and A/B.
static uint8_t buzzer(const Show &s, uint8_t pressed) {
    if (!pressed) return NOBODY;
    static const uint8_t GROUP[2][3] = {
        {DPAD, A_BUTTON | B_BUTTON, 0},
        {DPAD, SELECT_BUTTON, A_BUTTON | B_BUTTON},
    };
    uint8_t n = s.humans(), h = 0;
    for (uint8_t p = 0; p < 3; p++) {
        if (!s.human(p)) continue;
        uint8_t mask = n < 2 ? 0xFF : GROUP[n - 2][h];
        h++;
        if ((pressed & mask) && !((s.locked >> p) & 1)) return p;
    }
    return NOBODY;
}

// ---------------------------------------------------------------------------
// The tick
// ---------------------------------------------------------------------------
void Show::update(uint8_t pressed, uint8_t repeat, uint8_t held, bool fxBusy) {
    if (fxBusy) return;
    if (wait) { wait--; return; }
    bool me = human(cur);
    switch (phase) {
        case Phase::Intro: beginStep(); break;
        case Phase::Load: break;                // until supply()

        case Phase::TossReveal: {
            uint8_t b = buzzer(*this, pressed);
            if (b != NOBODY) { buzz(b); break; }
            if (tossTimer) { tossTimer--; break; }
            uint8_t pct = puzzle.shownPct();
            for (uint8_t p = 0; p < 3; p++)
                if (!human(p) && !((locked >> p) & 1) && pct >= buzzAt[p]) { buzz(p); return; }
            if (!puzzle.nHidden) { tossNobody(); break; }
            uint8_t c = puzzle.hiddenCell((uint8_t)(rand32() % puzzle.nHidden));
            puzzle.show(c);
            emit(Ev::TossPanel, c);
            tossTimer = opt.pace == PACE_QUICK ? T_TOSS * 2 / 3 : T_TOSS;
            break;
        }

        case Phase::TurnMenu:
            if (me) { menuInput(pressed, repeat); break; }
            if (!step) {
                cpuAct = cpu::turn(*this);
                emit(Ev::Intent, cur, cpuAct);
                step = 1;
                pace(T_THINK);
                break;
            }
            step = 0;
            choose(cpuAct);
            break;

        case Phase::Charge:
            if (!me) { doSpin((uint8_t)(60 + rand32() % 180)); break; }
            if (held & A_BUTTON) {
                int v = power + (step ? -6 : 6);
                if (v >= 255) { v = 255; step = 1; }
                if (v <= 16) { v = 16; step = 0; }
                power = (uint8_t)v;
                break;
            }
            // The bonus wheel waits for a press; a turn's spin was started by one.
            if (stepKind() == SK_BONUS && power == 0) break;
            doSpin(power);
            break;

        case Phase::Spinning: land(); break;

        case Phase::PickLetter: {
            char l = 0;
            if (me) {
                pickerInput(repeat);
                if ((pressed & B_BUTTON) && pickMode == PM_VOWEL && stepKind() == SK_ROUND && !finalMode) {
                    menuSel = A_VOWEL;          // changed their mind: nothing was paid
                    go(Phase::TurnMenu);
                    break;
                }
                if (!(pressed & A_BUTTON)) break;
                l = (char)('A' + pickCur);
            } else {
                if (!step) { cpuLetter = cpu::letter(*this); step = 1; pace(T_PICK); break; }
                l = cpuLetter;
            }
            if (stepKind() != SK_BONUS) { call(l); break; }
            picks[nPicks++] = (uint8_t)l;
            puzzle.used |= maskOf(l);
            emit(Ev::Picked, (uint8_t)l, nPicks);
            if (nPicks >= needPicks) { go(Phase::BonusReveal); pace(T_THINK); break; }
            if (nPicks == needPicks - 1) pickMode = PM_VOWEL;
            step = 0;
            pickerSettle();
            break;
        }

        case Phase::MysteryChoice:
            if (me) { menuInput(pressed, repeat); break; }
            if (!step) {
                cpuAct = cpu::flip(*this) ? A_FLIP : A_KEEP;
                emit(Ev::Intent, cur, cpuAct);
                step = 1;
                pace(T_THINK);
                break;
            }
            mystery(cpuAct == A_FLIP);
            break;

        case Phase::FinalMenu:
            if (!me) {
                if (!step) {
                    cpuAct = cpu::finalSolve(*this) ? A_SOLVE : A_PASS;
                    emit(Ev::Intent, cur, cpuAct);
                    step = 1;
                    pace(T_THINK);
                    break;
                }
                if (cpuAct == A_SOLVE) { emit(Ev::SolveTry, cur); solveResult(cpu::solveRight(*this), false); }
                else nextTurn();
                break;
            }
            if (clock) clock--;
            if (!clock) { nextTurn(); break; }
            menuInput(pressed, repeat);
            break;

        case Phase::Entry:
        case Phase::Confirm:
            if (clock) {
                clock--;
                if (clock % 60 == 0) emit(Ev::Clock, (uint8_t)(clock / 60));
            }
            if (!clock) { solveResult(false, true); break; }
            if (phase == Phase::Entry) { entryInput(pressed, repeat); break; }
            if (pressed & A_BUTTON) solveResult(entryRight(), false);
            else if (pressed & B_BUTTON) {
                guess[blank] = 0;
                emit(Ev::Type, blank, 0);
                go(Phase::Entry);
            }
            break;

        case Phase::RoundEnd: {
            // A round's summary waits for A; a toss-up, or a table of CPUs, moves on.
            bool autoNext = stepKind() == SK_TOSS || !humans();
            if (!step) { step = 1; if (autoNext) pace(T_END); break; }
            if (!autoNext && !(pressed & A_BUTTON)) break;
            nextStep();
            break;
        }

        case Phase::BonusGiven: {
            static const char GIVEN[] = "RSTLNE";
            if (step < 6) {
                char l = GIVEN[step++];
                emit(Ev::Called, (uint8_t)l, puzzle.reveal(l), cur, 0);
                pace(T_GIVEN);
                break;
            }
            if (!puzzle.nHidden) { bonusDone(true); break; }
            needPicks = 4;
            if (wild[cur]) {
                wild[cur] = false;
                needPicks = 5;
                emit(Ev::Token, cur, wedge::WILD, 0);
            }
            nPicks = 0;
            say(L_PICK, F_NORMAL, 0, needPicks - 1);
            pickMode = PM_CONSONANT;
            go(Phase::PickLetter);
            pickerSettle();
            break;
        }

        case Phase::BonusReveal:
            if (step < nPicks) {
                char l = (char)picks[step++];
                emit(Ev::Called, (uint8_t)l, puzzle.reveal(l), cur, 0);
                pace(T_GIVEN + 12);
                break;
            }
            if (!puzzle.nHidden) { bonusDone(true); break; }
            think = seconds(10);
            say(L_CLOCK, F_NORMAL, 0, think / 60);
            if (!me) {
                // A CPU's bonus round is decided now, and told at a moment
                // of its choosing (clock: the think time left when it speaks).
                cpuWin = cpu::bonusWin(*this);
                clock = (uint16_t)(think - 120 - rand32() % 240);
            }
            go(Phase::BonusThink);
            break;

        case Phase::BonusThink:
            if (think) {
                think--;
                if (think % 60 == 0) emit(Ev::Clock, (uint8_t)(think / 60));
            }
            if (me) {
                if (!think) { say(L_TIME, F_RAISED); bonusDone(false); }
                else if (pressed & A_BUTTON) { emit(Ev::SolveTry, cur); startEntry(CTX_BONUS); }
            } else if (cpuWin ? think <= clock : !think) {
                if (cpuWin) emit(Ev::SolveTry, cur);
                bonusDone(cpuWin);
            }
            break;

        case Phase::EpisodeEnd:
        case Phase::Quit:
            break;
    }
}

// ---------------------------------------------------------------------------
// The host's lines
// ---------------------------------------------------------------------------
static const char *const FIXED[LINE_COUNT] = {
    "WELCOME TO\nWORD WHEEL!",                      // L_WELCOME
    nullptr,                                        // L_TOSSUP
    "NOBODY GOT\nTHAT ONE",                         // L_NOBODY
    nullptr, nullptr, nullptr, nullptr,             // L_ROUND, L_COUNT, L_NONE, L_REPEAT
    "OH NO!\nBANKRUPT!",                            // L_BANKRUPT
    "LOSE A TURN.\nTOUGH BREAK",                    // L_LOSE
    "FREE PLAY!\nANY LETTER,\nNO RISK",             // L_FREE
    "ONLY VOWELS\nREMAIN",                          // L_ONLY_VOWELS
    "NO MORE\nVOWELS",                              // L_NO_VOWELS
    "THAT'S IT!",                                   // L_SOLVED
    "SORRY,\nTHAT'S NOT IT",                        // L_WRONG
    "TIME'S UP!",                                   // L_TIME
    "WE HAVE A\n$1,000\nHOUSE MINIMUM",             // L_MINIMUM
    "THAT BELL MEANS\nFINAL SPIN!\nI'LL SPIN IT",   // L_FINAL
    nullptr,                                        // L_FINAL_VALUE
    "MYSTERY WEDGE!\n$1,000 A LETTER\nOR FLIP IT?", // L_MYSTERY
    "$10,000!",                                     // L_BIG
    "THE WILD CARD\nIS YOURS",                      // L_WILD
    "SOLVE IT AND\nTHE TRIP\nIS YOURS",             // L_PRIZE
    "BONUS ROUND!\nSPIN FOR YOUR\nENVELOPE",        // L_BONUS
    "WE GIVE YOU\nR S T L N E",                     // L_GIVEN
    nullptr, nullptr, nullptr, nullptr,             // L_PICK, L_CLOCK, L_BONUS_WIN, L_BONUS_LOSE
    "GOOD NIGHT,\nEVERYBODY!",                      // L_GOODNIGHT
};

const char *Show::lineText(uint8_t line, uint8_t letter, int32_t number, char *buf) const {
    if (line >= LINE_COUNT) return "";
    if (FIXED[line]) return FIXED[line];
    char *p = buf;
    switch (line) {
        case L_TOSSUP:
            p = fmtCash(p, number);
            fmtStr(p, " TOSS-UP!\nBUZZ IN WHEN\nYOU KNOW IT");
            break;
        case L_ROUND: {
            char nm[5];
            p = fmtStr(p, "ROUND ");
            p = fmtInt(p, number);
            *p++ = '\n';
            const char *who = name(letter, nm);
            p = fmtStr(p, who);
            fmtStr(p, who[0] == 'Y' ? " START" : " STARTS");
            break;
        }
        case L_COUNT: {
            static const char NUM[6][6] = {"ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX"};
            p = number >= 1 && number <= 6 ? fmtStr(p, NUM[number - 1]) : fmtInt(p, number);
            *p++ = ' ';
            *p++ = (char)letter;
            fmtStr(p, number > 1 ? "'S!" : "!");
            break;
        }
        case L_NONE:
            p = fmtStr(p, "NO ");
            *p++ = (char)letter;
            fmtStr(p, "'S,\nSORRY");
            break;
        case L_REPEAT:
            *p++ = (char)letter;
            fmtStr(p, " WAS ALREADY\nCALLED");
            break;
        case L_FINAL_VALUE:
            p = fmtStr(p, "VOWELS WORTH\nNOTHING,\nCONSONANTS\n");
            fmtCash(p, number);
            break;
        case L_PICK:
            p = fmtInt(p, number);
            fmtStr(p, " CONSONANTS\nAND A VOWEL");
            break;
        case L_CLOCK:
            p = fmtInt(p, number);
            fmtStr(p, " SECONDS.\nGOOD LUCK!");
            break;
        case L_BONUS_WIN:
            p = fmtStr(p, "YOU DID IT!\n");
            p = fmtCash(p, number);
            fmtStr(p, "!");
            break;
        default:
            p = fmtStr(p, "SO CLOSE.\nIT WAS\n");
            fmtCash(p, number);
    }
    return buf;
}
