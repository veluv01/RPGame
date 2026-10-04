// Host tests for the rules: the puzzle board, the wheel layouts, the spin
// solver, the show's flow and money, the CPU contestants.
//   rpgame test
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "../../Show.h"
#include "../../Spin.h"
#include "../../Cpu.h"
#include <rpgame/Input.h>

static long checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { checks++; long long x_ = (long long)(a), y_ = (long long)(b); \
    if (x_ != y_) { failures++; printf("FAIL %s:%d  %s == %s  (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, x_, y_); } } while (0)

static const char *const PUZZLES[][2] = {
    {"ONCE IN A\nBLUE MOON", "PHRASE"},
    {"A PENNY FOR\nYOUR THOUGHTS", "PHRASE"},
    {"FRESH\nSQUEEZED\nLEMONADE", "FOOD & DRINK"},
    {"QUICK\nTHINKING", "PHRASE"},
    {"THE GRAND\nCANYON", "ON THE MAP"},
    {"JAZZ", "THING"},
    {"ROCK & ROLL\nHALL OF FAME", "PLACE"},
    {"IT'S A SMALL\nWORLD AFTER\nALL", "PHRASE"},
    {"BIG BAD WOLF", "CHARACTER"},
    {"EAU", "THING"},
    // (random draws come from here on: show-sized puzzles)
    {"BETTER LATE\nTHAN NEVER", "PHRASE"},
    {"CHOCOLATE\nCHIP COOKIES", "FOOD & DRINK"},
    {"WALKING THE\nDOG IN THE PARK", "WHAT ARE YOU\nDOING?"},
    {"THE STATUE OF\nLIBERTY", "LANDMARK"},
    {"A PICTURE IS\nWORTH A\nTHOUSAND WORDS", "PHRASE"},
    {"GRANDFATHER\nCLOCK", "AROUND THE HOUSE"},
    {"SUNDAY MORNING\nPANCAKES", "FOOD & DRINK"},
    {"HIGH SCHOOL\nSWEETHEARTS", "PEOPLE"},
    {"DOWN THE\nRABBIT HOLE", "PHRASE"},
    {"VANILLA ICE\nCREAM CONE", "FOOD & DRINK"},
    {"BRIGHT LIGHTS\nBIG CITY", "PHRASE"},
    {"JUMPING TO\nCONCLUSIONS", "WHAT ARE YOU\nDOING?"},
};
constexpr int NPUZ = sizeof PUZZLES / sizeof PUZZLES[0];
constexpr int NFIXED = 10;

struct Log {
    std::vector<Event> ev;
    int count(Ev t) const { int n = 0; for (auto &e : ev) n += e.type == t; return n; }
    const Event *last(Ev t) const {
        for (size_t i = ev.size(); i--;) if (ev[i].type == t) return &ev[i];
        return nullptr;
    }
};

// Drain events, answering NeedPuzzle. fixed >= 0: always that puzzle.
static void drain(Show &s, Log &log, int fixed = -1) {
    Event e;
    while (s.popEvent(e)) {
        log.ev.push_back(e);
        if (e.type == Ev::NeedPuzzle) {
            int i = fixed >= 0 ? fixed : NFIXED + (int)(s.rand32() % (NPUZ - NFIXED));
            s.supply(PUZZLES[i][0], PUZZLES[i][1]);
        }
    }
}

static void tick(Show &s, Log &log, uint8_t pressed = 0, uint8_t held = 0, int fixed = -1) {
    s.update(pressed, pressed, (uint8_t)(held | pressed), false);
    drain(s, log, fixed);
}

static void run(Show &s, Log &log, Phase until, int max = 5000, int fixed = -1) {
    while (s.phase != until && max-- > 0) tick(s, log, 0, 0, fixed);
    CHECK(s.phase == until);
}

static void fresh(Show &s, uint8_t k0, uint8_t k1, uint8_t k2, uint32_t seed, uint8_t game = GAME_FULL) {
    memset(&s, 0, sizeof s);
    s.opt.game = game;
    s.kind[0] = k0; s.kind[1] = k1; s.kind[2] = k2;
    s.seed(seed);
    s.newEpisode();
}

// ---------------------------------------------------------------------------
static void testPuzzle() {
    Puzzle p;
    p.set("ONCE IN A\nBLUE MOON", "PHRASE");
    CHECK_EQ(p.nLetters, 15);
    CHECK_EQ(p.nHidden, 15);
    // Two lines sit on rows 1 and 2, centred: "ONCE IN A" is 9 wide -> col 2.
    CHECK_EQ(p.cell[14 + 2], 'O');
    CHECK_EQ(p.cell[14 + 10], 'A');
    CHECK_EQ(p.cell[28 + 2], 'B');
    CHECK_EQ(p.cell[14 + 6], 0);
    CHECK_EQ(p.count('O'), 3);
    CHECK_EQ(p.reveal('O'), 3);
    CHECK_EQ(p.reveal('O'), 0);
    CHECK_EQ(p.nHidden, 12);
    CHECK(p.used & pz::maskOf('O'));
    CHECK_EQ(p.reveal('Z'), 0);
    CHECK(!(p.hiddenMask() & pz::maskOf('O')));
    CHECK(p.hiddenMask() & pz::maskOf('N'));
    CHECK_EQ(p.shownPct(), 20);
    p.revealAll();
    CHECK_EQ(p.nHidden, 0);
    CHECK_EQ(p.hiddenMask(), 0);

    // Punctuation starts turned and is not a letter; four lines fill the board.
    p.set("ROCK & ROLL\nHALL OF FAME", "PLACE");
    CHECK_EQ(p.nLetters, 18);
    int amp = -1;
    for (int i = 0; i < pz::CELLS; i++) if (p.cell[i] == '&') amp = i;
    CHECK(amp >= 0 && p.isShown((uint8_t)amp));
    p.set("AAAAAAAAAAAA\nBBBBBBBBBBBBBB\nCCCCCCCCCCCCCC\nDDDDDDDDDDDD", "X");
    CHECK_EQ(p.nLetters, 52);
    for (int i = 0; i < pz::CELLS; i++) CHECK_EQ(p.cell[i] != 0, pz::panel((uint8_t)i));
    // Three lines start on the top row.
    p.set("AB\nCD\nEF", "X");
    CHECK_EQ(p.cell[6], 'A');
    CHECK_EQ(p.cell[28 + 6], 'E');
    // hiddenCell walks the unturned letters in order.
    CHECK_EQ(p.hiddenCell(0), 6);
    CHECK_EQ(p.hiddenCell(5), 28 + 7);
    CHECK_EQ(p.hiddenCell(6), 255);
    p.show(6);
    CHECK_EQ(p.hiddenCell(0), 7);
    CHECK_EQ(p.nHidden, 5);
}

static void testWedges() {
    for (uint8_t r = 0; r < 3; r++) {
        int bank = 0, lose = 0, free = 0, top = 0, money = 0, myst = 0, thirds = 0, wild = 0, prize = 0;
        for (uint8_t i = 0; i < wedge::COUNT; i++) {
            wedge::Wedge w = wedge::at(r, i);
            switch (w.kind) {
                case wedge::BANKRUPT: bank++; break;
                case wedge::LOSE: lose++; break;
                case wedge::FREE: free++; break;
                case wedge::TOP: top++; CHECK_EQ(w.value, r == 0 ? 2500 : r == 1 ? 3500 : 5000); break;
                case wedge::MONEY: money++; CHECK(w.value >= 500 && w.value <= 900 && w.value % 50 == 0); break;
                case wedge::MYSTERY: myst++; CHECK_EQ(w.value, 1000); break;
                case wedge::THIRDS: thirds++; CHECK_EQ(w.value, 10000); break;
                case wedge::WILD: wild++; break;
                case wedge::PRIZE: prize++; break;
                default: CHECK(false);
            }
        }
        CHECK_EQ(bank, 2); CHECK_EQ(lose, 1); CHECK_EQ(free, 1); CHECK_EQ(top, 1);
        CHECK_EQ(myst, r == 1 ? 2 : 0);
        CHECK_EQ(thirds, r == 2 ? 1 : 0);
        CHECK_EQ(wild, r == 0 ? 1 : 0);
        CHECK_EQ(prize, r == 0 ? 1 : 0);
        CHECK_EQ(bank + lose + free + top + money + myst + thirds + wild + prize, 24);
    }
}

static void testSpin() {
    for (int stop = 0; stop < 72; stop++)
        for (int power = 0; power < 256; power += 15)
            for (int jit = -4; jit <= 4; jit += 4)
                for (int quick = 0; quick < 2; quick++) {
                    spin::Spin sp;
                    int32_t start = ((stop * 977 + power * 31) % spin::RIM) << 8 | 77;
                    sp.start(start, (uint8_t)stop, (uint8_t)power, (int8_t)jit, quick);
                    int32_t prev = sp.pos(), travelled = 0, lastStep = 0;
                    int frames = 0;
                    while (sp.step()) {
                        int32_t p = sp.pos();
                        int32_t d = (p - prev + (spin::RIM << 8)) % (spin::RIM << 8);
                        if (d >= (spin::RIM << 7)) { failures++; printf("FAIL spin went backwards\n"); break; }
                        travelled += d;
                        lastStep = d;
                        prev = p;
                        frames++;
                    }
                    int32_t p = sp.pos();
                    travelled += (p - prev + (spin::RIM << 8)) % (spin::RIM << 8);
                    checks++;
                    if (p != ((stop * spin::PEG + spin::PEG / 2 + jit) << 8)) {
                        failures++;
                        printf("FAIL spin stop %d power %d: rests at %d\n", stop, power, (int)p);
                    }
                    CHECK(frames >= 90 && frames <= 245);
                    CHECK(travelled >= (spin::RIM << 8) && travelled < (4 * spin::RIM << 8));
                    CHECK(lastStep < (spin::PEG << 8));          // creeping at the end
                }
}

// ---------------------------------------------------------------------------
// A round played by hand through the action hooks: money, penalties, tokens.
static Show S;

static void startRound1(Log &log, uint32_t seed = 7) {
    fresh(S, P_HUMAN, P_HUMAN, P_HUMAN, seed);
    log.ev.clear();
    // Toss-up: nobody buzzes, every panel turns, nobody wins.
    run(S, log, Phase::RoundEnd, 5000, 5);
    CHECK_EQ(S.winner, NOBODY);
    run(S, log, Phase::TurnMenu, 2000, 0);         // ONCE IN A / BLUE MOON
    CHECK_EQ(S.roundNo(), 1);
    CHECK_EQ(S.cur, 0);
    CHECK_EQ(S.total[0] + S.total[1] + S.total[2], 0);
}

// Spin to a stop through the real phases (TurnMenu -> Charge -> Spinning).
static void spinTo(Log &log, uint8_t wedgeIndex, uint8_t third = 1) {
    CHECK(S.phase == Phase::TurnMenu);
    S.forceStop((uint8_t)(wedgeIndex * 3 + third));
    S.menuSel = A_SPIN;
    tick(S, log, A_BUTTON);                         // SPIN
    CHECK(S.phase == Phase::Charge);
    tick(S, log, 0, A_BUTTON);                      // charging
    tick(S, log);                                   // released
    CHECK(S.phase == Phase::Spinning);
    tick(S, log);                                   // lands
}

// The action hooks, with their events logged.
static Log *LOG;
static void call(char l) { S.actCall(l); drain(S, *LOG); }
static void solve(bool right) { S.actSolve(right); drain(S, *LOG); }

static void testRoundByHand() {
    Log log;
    LOG = &log;
    startRound1(log);
    // $600 wedge, three N's.
    spinTo(log, 1);
    CHECK(S.phase == Phase::PickLetter);
    CHECK_EQ(S.value, 600);
    CHECK(!pz::isVowel((char)('A' + S.pickCur)));
    call('N');
    CHECK_EQ(S.cash[0], 1800);
    CHECK(S.phase == Phase::TurnMenu);
    CHECK_EQ(S.cur, 0);
    CHECK_EQ(log.last(Ev::Called)->b, 3);
    CHECK_EQ(log.last(Ev::Called)->amount, 1800);
    // Buy a vowel: $250 whatever comes of it.
    S.menuSel = A_VOWEL;
    tick(S, log, A_BUTTON);
    CHECK(S.phase == Phase::PickLetter);
    CHECK_EQ(S.pickMode, PM_VOWEL);
    CHECK(pz::isVowel((char)('A' + S.pickCur)));
    // B backs out without paying.
    tick(S, log, B_BUTTON);
    CHECK(S.phase == Phase::TurnMenu);
    CHECK_EQ(S.cash[0], 1800);
    tick(S, log, A_BUTTON);
    call('U');
    CHECK_EQ(S.cash[0], 1550);
    CHECK_EQ(S.cur, 0);
    S.menuSel = A_VOWEL;
    tick(S, log, A_BUTTON);
    call('A');
    CHECK_EQ(S.cash[0], 1300);
    // A miss passes the turn.
    spinTo(log, 2);
    call('Z');
    CHECK_EQ(S.cur, 1);
    CHECK_EQ(S.cash[0], 1300);
    CHECK_EQ(log.last(Ev::NoLetter)->a, 'Z');
    // Player 2: lose a turn.
    spinTo(log, 16);
    CHECK_EQ(S.cur, 2);
    CHECK_EQ(log.count(Ev::LoseTurn), 1);
    // Player 3: a vowel they cannot afford is refused.
    S.menuSel = A_VOWEL;
    tick(S, log, A_BUTTON);
    CHECK(S.phase == Phase::TurnMenu);
    CHECK_EQ(log.last(Ev::Deny)->a, D_NO_CASH);
    // The prize wedge: the tag comes with a letter that is there...
    spinTo(log, 9);
    CHECK_EQ(S.wedgeAt(9).kind, wedge::PRIZE);
    call('C');
    CHECK_EQ(S.cash[2], 500);
    CHECK(S.prize[2]);
    CHECK_EQ(S.wedgeAt(9).kind, wedge::MONEY);
    // ... and goes with everything else on a bankrupt.
    spinTo(log, 7);
    CHECK_EQ(S.cash[2], 0);
    CHECK(!S.prize[2]);
    CHECK_EQ(S.cur, 0);
    CHECK_EQ(log.last(Ev::Bankrupt)->amount, 500);
    CHECK_EQ(S.stats.bankrupts, 1);
    // Free play: a miss costs nothing and keeps the turn; a vowel is free.
    spinTo(log, 14);
    CHECK_EQ(S.pickMode, PM_ANY);
    call('X');
    CHECK_EQ(S.cur, 0);
    CHECK(S.phase == Phase::TurnMenu);
    spinTo(log, 14);
    call('I');
    CHECK_EQ(S.cash[0], 1300);
    CHECK_EQ(S.cur, 0);
    CHECK(!S.wildUsable());
    // The wild card: picked up with a letter that is there.
    spinTo(log, 22);
    CHECK_EQ(S.wedgeAt(22).kind, wedge::WILD);
    call('L');
    CHECK_EQ(S.cash[0], 1800);
    CHECK(S.wild[0]);
    CHECK_EQ(S.wedgeAt(22).kind, wedge::MONEY);     // the wedge is plain now
    // The top wedge, then the wild card replays it.
    spinTo(log, 0);
    CHECK_EQ(S.value, 2500);
    call('M');
    CHECK_EQ(S.cash[0], 4300);
    CHECK_EQ(S.stats.topWedge, 2500);
    CHECK(S.wildUsable());
    CHECK_EQ(S.menuCount(), 4);
    S.menuSel = A_WILD;
    tick(S, log, A_BUTTON);
    CHECK(S.phase == Phase::PickLetter);
    CHECK(!S.wild[0]);
    call('B');
    CHECK_EQ(S.cash[0], 6800);
    CHECK(!S.wildUsable());
    CHECK_EQ(S.menuCount(), 3);
    // Only vowels remain: spinning is refused.
    CHECK_EQ(S.puzzle.hiddenMask(), pz::maskOf('E') | pz::maskOf('O'));
    CHECK_EQ(S.menuSel, A_VOWEL);
    S.menuSel = A_SPIN;
    tick(S, log, A_BUTTON);
    CHECK_EQ(log.last(Ev::Deny)->a, D_NO_CONSONANTS);
    S.menuSel = A_VOWEL;
    tick(S, log, A_BUTTON);
    call('O');
    CHECK_EQ(S.cash[0], 6550);
    solve(false);                              // a wrong solve passes the turn
    CHECK_EQ(S.cur, 1);
    // The last panel turned wins the round outright: others keep nothing,
    // and the house makes a small win up to $1,000.
    S.cash[1] = 900;
    S.menuSel = A_VOWEL;
    tick(S, log, A_BUTTON);
    call('E');
    CHECK(S.phase == Phase::RoundEnd);
    CHECK_EQ(S.winner, 1);
    CHECK_EQ(S.total[1], 1000);                     // 900 - 250 = 650
    CHECK(S.minimum);
    CHECK_EQ(S.total[0], 0);
    CHECK_EQ(S.stats.solved, 1);
    CHECK_EQ(log.last(Ev::RoundWon)->amount, 1000);
    CHECK_EQ(S.dropped(), 0);

    // The prize is added to a winning round.
    startRound1(log);
    spinTo(log, 9);
    call('N');
    CHECK(S.prize[0]);
    solve(true);
    CHECK_EQ(S.total[0], 1500 + PRIZE_VALUE);
    CHECK(!S.minimum);
}

static void testSolveEntry() {
    Log log;
    LOG = &log;
    startRound1(log);
    spinTo(log, 1);
    call('N');
    // SOLVE, then type the answer with the picker.
    S.menuSel = A_SOLVE;
    tick(S, log, A_BUTTON);
    CHECK(S.phase == Phase::Entry);
    CHECK_EQ(S.clock, 1800);
    const char *answer = "OCEIABLUEMOO";            // the blanks in board order
    for (const char *a = answer; *a; a++) {
        int guard = 0;
        while (S.pickCur != *a - 'A' && guard++ < 30) tick(S, log, RIGHT_BUTTON);
        CHECK_EQ(S.pickCur, *a - 'A');
        CHECK(S.phase == Phase::Entry);
        tick(S, log, A_BUTTON);
    }
    CHECK(S.phase == Phase::Confirm);
    // B re-opens the last panel; a wrong letter there fails the solve.
    tick(S, log, B_BUTTON);
    CHECK(S.phase == Phase::Entry);
    tick(S, log, A_BUTTON);                         // 'O' again
    CHECK(S.phase == Phase::Confirm);
    tick(S, log, A_BUTTON);
    CHECK(S.phase == Phase::RoundEnd);
    CHECK_EQ(S.winner, 0);
    CHECK_EQ(S.total[0], 1800);
    CHECK(!S.minimum);
    // RoundEnd waits for A, then round 2 loads with player 2 starting.
    for (int i = 0; i < 300; i++) tick(S, log);
    CHECK(S.phase == Phase::RoundEnd);
    tick(S, log, A_BUTTON, 0, 1);
    run(S, log, Phase::TurnMenu, 100, 1);
    CHECK_EQ(S.roundNo(), 2);
    CHECK_EQ(S.cur, 1);
    CHECK_EQ(S.cash[0], 0);
    CHECK_EQ(S.total[0], 1800);

    // A wrong solve passes the turn; the picker never rests on a called letter.
    S.menuSel = A_SOLVE;
    tick(S, log, A_BUTTON);
    int typed = 0;
    while (S.phase == Phase::Entry && typed++ < 60) tick(S, log, A_BUTTON);
    CHECK(S.phase == Phase::Confirm);
    tick(S, log, A_BUTTON);
    CHECK(S.phase == Phase::TurnMenu);
    CHECK_EQ(S.cur, 2);
    CHECK_EQ(log.last(Ev::SolveWrong)->a, 1);
    // B on an empty first panel backs out of a solve.
    S.menuSel = A_SOLVE;
    tick(S, log, A_BUTTON);
    tick(S, log, B_BUTTON);
    CHECK(S.phase == Phase::TurnMenu);
    CHECK_EQ(S.cur, 2);
    // The clock running out is a wrong solve.
    tick(S, log, A_BUTTON);
    for (int i = 0; i < 1800 && S.phase == Phase::Entry; i++) tick(S, log);
    CHECK(S.phase == Phase::TurnMenu);
    CHECK_EQ(S.cur, 0);
    CHECK_EQ(log.last(Ev::SolveWrong)->b, 1);

    // Mystery (round 2): keep = $1,000 a letter.
    spinTo(log, 9);
    CHECK_EQ(S.wedgeAt(9).kind, wedge::MYSTERY);
    call('N');                                 // A PENNY FOR / YOUR THOUGHTS: two
    CHECK(S.phase == Phase::MysteryChoice);
    CHECK_EQ(S.cash[0], 0);
    S.menuSel = 0;
    tick(S, log, A_BUTTON);
    CHECK_EQ(S.cash[0], 2000);
    CHECK_EQ(S.wedgeAt(21).kind, wedge::MONEY);     // both mystery wedges are spent
    CHECK(S.phase == Phase::TurnMenu);
}

static void testMysteryFlip() {
    int big = 0, bust = 0;
    for (uint32_t seed = 1; seed <= 200; seed++) {
        Log log;
        LOG = &log;
        fresh(S, P_HUMAN, P_HUMAN, P_HUMAN, seed);
        run(S, log, Phase::RoundEnd, 5000, 5);
        run(S, log, Phase::TurnMenu, 2000, 0);
        spinTo(log, 1);
        call('N');
        solve(true);
        tick(S, log);
        tick(S, log, A_BUTTON, 0, 1);
        run(S, log, Phase::TurnMenu, 100, 1);
        S.cash[S.cur] = 400;
        uint8_t who = S.cur;
        spinTo(log, 21);
        call('T');
        CHECK(S.phase == Phase::MysteryChoice);
        S.menuSel = 1;
        tick(S, log, A_BUTTON);
        if (log.last(Ev::Mystery)->b == 1) { big++; CHECK_EQ(S.cash[who], 10400); CHECK_EQ(S.cur, who); }
        else { bust++; CHECK_EQ(S.cash[who], 0); CHECK(S.cur != who); CHECK_EQ(log.last(Ev::Mystery)->b, 2); }
    }
    CHECK(big > 60 && bust > 60);
}

static void testFinalSpin() {
    Log log;
    LOG = &log;
    fresh(S, P_HUMAN, P_HUMAN, P_HUMAN, 11, GAME_QUICK);
    run(S, log, Phase::RoundEnd, 5000, 5);
    run(S, log, Phase::TurnMenu, 2000, 0);
    CHECK_EQ(S.layout(), 2);
    CHECK_EQ(S.wedgeAt(18).kind, wedge::THIRDS);
    // The thirds wedge: its outer slots are bankrupt, the middle is $10,000 flat.
    S.cash[0] = 700;
    spinTo(log, 18, 0);
    CHECK_EQ(S.cash[0], 0);
    CHECK_EQ(S.cur, 1);
    spinTo(log, 18, 1);
    call('N');                                 // three N's, still $10,000
    CHECK_EQ(S.cash[1], 10000);
    // Ten turns in, the bell rings at the next turn change.
    for (int i = 0; i < 12 && !S.finalMode; i++) {
        if (S.phase != Phase::TurnMenu) break;
        spinTo(log, 16);                             // lose a turn
    }
    CHECK(S.finalMode);
    CHECK_EQ(log.count(Ev::FinalBell), 1);
    run(S, log, Phase::PickLetter, 50, 0);
    const Event *fv = log.last(Ev::FinalValue);
    CHECK(fv && fv->amount >= 1500 && fv->amount <= 6000);
    CHECK_EQ(log.last(Ev::SpinStart)->a, HOST);
    CHECK_EQ(S.pickMode, PM_ANY);
    int32_t v = fv->amount;
    uint8_t p = S.cur;
    int32_t before = S.cash[p];
    call('M');
    CHECK_EQ(S.cash[p], before + v);
    CHECK(S.phase == Phase::FinalMenu);
    S.menuSel = 1;                                  // pass
    tick(S, log, A_BUTTON);
    CHECK_EQ(S.cur, (p + 1) % 3);
    CHECK(S.phase == Phase::PickLetter);
    before = S.cash[S.cur];
    call('E');                                 // vowels are free and worth nothing
    CHECK_EQ(S.cash[S.cur], before);
    CHECK(S.phase == Phase::FinalMenu);
    // The five-second clock passes for them.
    p = S.cur;
    for (int i = 0; i < 301 && S.phase == Phase::FinalMenu; i++) tick(S, log);
    CHECK_EQ(S.cur, (p + 1) % 3);
    call('Q');                                 // miss: next
    CHECK_EQ(S.cur, (p + 2) % 3);
    // Solve from the final menu.
    call('B');
    S.menuSel = 0;
    tick(S, log, A_BUTTON);
    CHECK(S.phase == Phase::Entry);
    solve(true);
    CHECK(S.phase == Phase::RoundEnd);
}

static void testTossUp() {
    // One human: any button buzzes; a right answer is $1,000 and the start.
    Log log;
    LOG = &log;
    fresh(S, P_DOT, P_HUMAN, P_ACE, 3);
    S.buzzAt[0] = S.buzzAt[2] = 200;
    run(S, log, Phase::TossReveal, 100, 5);         // JAZZ
    S.buzzAt[0] = S.buzzAt[2] = 200;                // (supply() drew them)
    tick(S, log, SELECT_BUTTON, 0, 5);
    CHECK(S.phase == Phase::Entry);
    CHECK_EQ(S.cur, 1);
    CHECK_EQ(S.clock, 1200);
    solve(true);
    CHECK_EQ(S.total[1], 1000);
    CHECK_EQ(S.starter, 1);
    CHECK_EQ(S.stats.tossups, 1);
    run(S, log, Phase::TurnMenu, 2000, 0);
    CHECK_EQ(S.cur, 1);

    // A wrong answer locks them out; the panels keep turning.
    fresh(S, P_HUMAN, P_HUMAN, P_HUMAN, 3);
    log.ev.clear();
    run(S, log, Phase::TossReveal, 100, 2);
    tick(S, log, A_BUTTON, 0, 2);                   // three humans: A/B is podium 3
    CHECK_EQ(S.cur, 2);
    solve(false);
    CHECK(S.phase == Phase::TossReveal);
    CHECK_EQ(S.locked, 4);
    tick(S, log, B_BUTTON, 0, 2);                   // locked: ignored
    CHECK(S.phase == Phase::TossReveal);
    tick(S, log, UP_BUTTON, 0, 2);
    CHECK_EQ(S.cur, 0);
    solve(false);
    tick(S, log, SELECT_BUTTON, 0, 2);
    CHECK_EQ(S.cur, 1);
    solve(false);                              // all three wrong
    CHECK(S.phase == Phase::RoundEnd);
    CHECK_EQ(S.winner, NOBODY);
    CHECK_EQ(S.total[0] + S.total[1] + S.total[2], 0);
    CHECK_EQ(S.puzzle.nHidden, 0);
}

static void testBonus() {
    for (int win = 0; win < 2; win++) {
        Log log;
        LOG = &log;
        fresh(S, P_HUMAN, P_DOT, P_ACE, 5, GAME_QUICK);
        S.stepIdx = 2;                               // straight to the bonus round
        S.total[0] = 5000;
        S.resume();
        S.wild[0] = win;
        CHECK(S.phase == Phase::Charge);
        CHECK_EQ(S.cur, 0);
        for (int i = 0; i < 50; i++) tick(S, log);  // waits for the press
        CHECK(S.phase == Phase::Charge);
        S.forceStop(13 * 3);
        tick(S, log, A_BUTTON, 0, 3);
        tick(S, log, 0, 0, 3);
        CHECK(S.phase == Phase::Spinning);
        CHECK_EQ(log.last(Ev::SpinStart)->amount, 1);
        CHECK_EQ(log.last(Ev::SpinStart)->b, 13 * 3 + 1);
        run(S, log, Phase::PickLetter, 600, 3);      // QUICK / THINKING, R S T L N E given
        CHECK_EQ(S.bonusPrize, 100000u);
        CHECK_EQ(log.count(Ev::Called), 6);
        CHECK(S.puzzle.used & pz::maskOf('R'));
        CHECK_EQ(S.needPicks, win ? 5 : 4);
        CHECK(!S.wild[0]);
        CHECK_EQ(S.pickMode, PM_CONSONANT);
        const char *want = win ? "CHKGI" : "CHKI";
        for (const char *w = want; *w; w++) {
            CHECK_EQ(S.pickMode, pz::isVowel(*w) ? PM_VOWEL : PM_CONSONANT);
            int guard = 0;
            while (S.pickCur != *w - 'A' && guard++ < 30) tick(S, log, RIGHT_BUTTON);
            tick(S, log, A_BUTTON);
        }
        CHECK(S.phase == Phase::BonusReveal);
        run(S, log, Phase::BonusThink, 600);
        CHECK_EQ(S.think, 600);
        for (int i = 0; i < 100; i++) tick(S, log);
        CHECK_EQ(S.think, 500);
        tick(S, log, A_BUTTON);
        CHECK(S.phase == Phase::Entry);
        if (win) {
            solve(true);
            CHECK(S.phase == Phase::RoundEnd);
            CHECK_EQ(S.total[0], 105000);
            CHECK_EQ(S.stats.bonusWins, 1);
            CHECK_EQ(log.last(Ev::Bonus)->a, 1);
        } else {
            solve(false);                       // wrong: back to the clock
            CHECK(S.phase == Phase::BonusThink);
            CHECK_EQ(S.think, 499);
            for (int i = 0; i < 600 && S.phase == Phase::BonusThink; i++) tick(S, log);
            CHECK(S.phase == Phase::RoundEnd);
            CHECK_EQ(S.total[0], 5000);
            CHECK_EQ(log.last(Ev::Bonus)->a, 0);
            CHECK_EQ(log.last(Ev::Bonus)->amount, 100000);
        }
        tick(S, log);
        tick(S, log, A_BUTTON);
        CHECK(S.phase == Phase::EpisodeEnd);
        CHECK_EQ(log.count(Ev::GameOver), 1);
        CHECK_EQ(S.stats.episodes, 1);
        CHECK_EQ(S.stats.wins, 1);
        CHECK_EQ(S.stats.career, (uint32_t)S.total[0]);
    }
}

// ---------------------------------------------------------------------------
// Whole episodes played by the CPUs: they end, the money adds up, and the
// personas rank as designed.
static void testCpuEpisodes() {
    long solved[4] = {0, 0, 0, 0}, wrong[4] = {0, 0, 0, 0}, won[4] = {0, 0, 0, 0};
    long ticksMax = 0, finals = 0, bonusWon = 0, nobody = 0;
    const int N = 2000;
    for (int n = 0; n < N; n++) {
        Log log;
        LOG = &log;
        uint8_t game = n % 4 == 3 ? GAME_QUICK : GAME_FULL;
        fresh(S, P_ACE, P_DOT, P_BUZZ, 1000 + n * 7919u, game);
        if (n % 5 == 0) S.opt.pace = PACE_QUICK;
        long ticks = 0;
        while (S.phase != Phase::EpisodeEnd && ticks < 400000) { tick(S, log); ticks++; }
        checks++;
        if (S.phase != Phase::EpisodeEnd) { failures++; printf("FAIL episode %d never ended\n", n); continue; }
        if (ticks > ticksMax) ticksMax = ticks;
        CHECK_EQ(S.dropped(), 0);
        long sum = 0;
        uint8_t kind = SK_TOSS;
        for (auto &e : log.ev) {
            if (e.type == Ev::PuzzleUp) kind = e.a;
            if (e.type == Ev::RoundWon) {
                sum += e.amount;
                if (e.a == NOBODY) nobody++;
                else if (e.c == SK_ROUND) solved[S.kind[e.a]]++;
            }
            if (e.type == Ev::Bonus && e.a) { sum += e.amount; bonusWon++; }
            if (e.type == Ev::SolveWrong && kind == SK_ROUND) wrong[S.kind[e.a]]++;
            if (e.type == Ev::FinalBell) finals++;
        }
        CHECK_EQ(sum, S.total[0] + S.total[1] + S.total[2]);
        CHECK_EQ(log.count(Ev::GameOver), 1);
        CHECK_EQ(log.count(Ev::NeedPuzzle), game == GAME_QUICK ? 3 : 6);
        CHECK_EQ(log.count(Ev::Bonus), 1);
        CHECK(S.total[0] >= 0 && S.total[1] >= 0 && S.total[2] >= 0);
        CHECK_EQ(S.stats.episodes, 0);              // no humans: no career
        won[S.kind[S.winner]]++;
    }
    printf("cpu episodes: %d, longest %ld ticks (%.1f min); rounds solved ACE %ld DOT %ld BUZZ %ld; "
           "wrong solves %ld/%ld/%ld; episodes won %ld/%ld/%ld; final spins %ld; bonus wins %ld; "
           "toss-ups nobody got %ld\n",
           N, ticksMax, ticksMax / 3600.0, solved[P_ACE], solved[P_DOT], solved[P_BUZZ],
           wrong[P_ACE], wrong[P_DOT], wrong[P_BUZZ], won[P_ACE], won[P_DOT], won[P_BUZZ], finals,
           bonusWon, nobody);
    CHECK(solved[P_ACE] > solved[P_DOT] && solved[P_DOT] > solved[P_BUZZ]);
    CHECK(solved[P_BUZZ] > N / 10);                 // the rookie still gets some
    CHECK(won[P_ACE] > won[P_BUZZ]);
    CHECK(wrong[P_BUZZ] > wrong[P_ACE]);
    CHECK(bonusWon > N / 10 && bonusWon < N * 9 / 10);
    CHECK(finals > N / 20);
}

// The same seed plays the same episode.
static void testDeterminism() {
    std::string a, b;
    for (int pass = 0; pass < 2; pass++) {
        Log log;
        LOG = &log;
        fresh(S, P_ACE, P_DOT, P_BUZZ, 4242);
        long ticks = 0;
        while (S.phase != Phase::EpisodeEnd && ticks++ < 400000) tick(S, log);
        std::string &out = pass ? b : a;
        for (auto &e : log.ev) {
            char t[48];
            snprintf(t, sizeof t, "%d,%d,%d,%d,%ld;", (int)e.type, e.a, e.b, e.c, (long)e.amount);
            out += t;
        }
    }
    CHECK(a == b);
    CHECK(a.size() > 500);
}

static void testLines() {
    fresh(S, P_HUMAN, P_DOT, P_ACE, 1);
    char buf[80];
    for (uint8_t l = 0; l < LINE_COUNT; l++) {
        for (int32_t num : {1, 2, 7, 12, 1000, 2000, 6000, 100000}) {
            const char *t = S.lineText(l, l == L_ROUND ? 2 : 'W', l == L_ROUND ? num % 4 : num, buf);
            CHECK(t && *t);
            int lines = 1, w = 0, wmax = 0;
            for (const char *p = t; *p; p++) {
                if (*p == '\n') { lines++; w = 0; } else if (++w > wmax) wmax = w;
            }
            checks++;
            if (lines > 4 || wmax > 18 || strlen(t) >= 72) {
                failures++;
                printf("FAIL line %d does not fit the bubble: \"%s\"\n", l, t);
            }
        }
    }
    CHECK(!strcmp(S.lineText(L_COUNT, 'T', 3, buf), "THREE T'S!"));
    CHECK(!strcmp(S.lineText(L_COUNT, 'T', 1, buf), "ONE T!"));
    CHECK(!strcmp(S.lineText(L_NONE, 'S', 0, buf), "NO S'S,\nSORRY"));
    CHECK(!strcmp(S.lineText(L_ROUND, 0, 2, buf), "ROUND 2\nYOU START"));
    CHECK(!strcmp(S.lineText(L_ROUND, 1, 3, buf), "ROUND 3\nDOT STARTS"));
    CHECK(!strcmp(S.lineText(L_TOSSUP, 0, 1000, buf), "$1,000 TOSS-UP!\nBUZZ IN WHEN\nYOU KNOW IT"));
    char nm[5];
    S.kind[1] = P_HUMAN;
    CHECK(!strcmp(S.name(0, nm), "P1"));
    CHECK(!strcmp(S.name(2, nm), "ACE"));
}

#define RUN(t) do { puts(#t); t(); } while (0)

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    RUN(testPuzzle);
    RUN(testWedges);
    RUN(testSpin);
    RUN(testRoundByHand);
    RUN(testSolveEntry);
    RUN(testMysteryFlip);
    RUN(testFinalSpin);
    RUN(testTossUp);
    RUN(testBonus);
    RUN(testCpuEpisodes);
    RUN(testDeterminism);
    RUN(testLines);
    printf("%ld checks, %ld failures\n", checks, failures);
    return failures ? 1 : 0;
}
