// Host tests for the puzzle bank: the flash decoder against what
// tools/phrases/build_bank.py says it packed, every puzzle on the board, the
// shuffled deal, and whole CPU episodes dealt from the real bank.
//   python tools/phrases/build_bank.py && rpgame test
#include <stdio.h>
#include <string.h>
#include <set>
#include <string>
#include <vector>
#include "../../Bank.h"
#include "../../Show.h"
#include <SdSpi.h>
#include <VCard.h>

// The test's SD card: sdcard/PHRASES.BNK in memory on a pretend FAT16 card
// (RPGameSD's VCard, as in the simulator), so the bank is found the way it is on
// the board. Out: no card. failAt: the read that fails (-1: none).
static std::vector<uint8_t> cardData;
static vcard::Card vc;
static bool cardIn;
static int cardReads, failAt = -1;
namespace sd {
bool init() { return cardIn; }
bool read(uint32_t lba, uint8_t *dst) {
    if (!cardIn || lba >= vc.blocks() || cardReads++ == failAt) return false;
    long k = vc.read(lba, dst);
    if (k >= 0) memcpy(dst, cardData.data() + (size_t)k * 512, 512);
    return true;
}
}  // namespace sd

static long checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { checks++; long long x_ = (long long)(a), y_ = (long long)(b); \
    if (x_ != y_) { failures++; printf("FAIL %s:%d  %s == %s  (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, x_, y_); } } while (0)

struct Ref { int section; std::string cat, text; };

static std::vector<Ref> loadRef(const char *path) {
    std::vector<Ref> out;
    FILE *f = fopen(path, "r");
    if (!f) return out;
    char line[200];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *a = strchr(line, '|');
        if (!a) continue;
        char *b = strchr(a + 1, '|');
        if (!b) continue;
        *a = *b = 0;
        Ref r{line[0] - '0', a + 1, b + 1};
        for (char &c : r.text) if (c == '/') c = '\n';
        out.push_back(r);
    }
    fclose(f);
    return out;
}

static void testDecode(const std::vector<Ref> &ref) {
    CHECK(ref.size() >= 100);
    uint16_t total = 0;
    for (uint8_t s = 0; s < bank::SECTIONS; s++) total = (uint16_t)(total + bank::count(s));
    CHECK_EQ(total, ref.size());
    CHECK(bank::count(SEC_ROUND) >= 8 && bank::count(SEC_TOSS) >= 8 && bank::count(SEC_BONUS) >= 8);
    CHECK_EQ(bank::count(3), 0);
    size_t k = 0;
    Puzzle pz;
    for (uint8_t s = 0; s < bank::SECTIONS; s++) {
        for (uint16_t i = 0; i < bank::count(s); i++, k++) {
            char text[bank::TEXT_MAX], cat[bank::CAT_MAX];
            CHECK(bank::fetch(s, i, text, cat));
            checks++;
            if (k >= ref.size() || ref[k].section != s || ref[k].text != text || ref[k].cat != cat) {
                failures++;
                printf("FAIL puzzle %d/%d: got \"%s\" [%s]\n", s, i, text, cat);
                continue;
            }
            // On the board: every character on a panel, nothing lost.
            pz.set(text, cat);
            int placed = 0, want = 0;
            for (uint8_t c = 0; c < pz::CELLS; c++)
                if (pz.cell[c]) { placed++; CHECK(pz::panel(c)); }
            for (const char *p = text; *p; p++) want += *p != ' ' && *p != '\n';
            CHECK_EQ(placed, want);
            CHECK(pz.nLetters >= 2);
            CHECK(!strcmp(pz.category, cat));
        }
    }
    // Out of range: empty, not a crash.
    char text[bank::TEXT_MAX] = "x", cat[bank::CAT_MAX] = "x";
    bank::flashFetch(0, bank::count(0), text, cat);
    CHECK_EQ(text[0], 0);
}

static void testPermute() {
    for (uint16_t n = 1; n <= 700; n += (n < 70 ? 1 : 37)) {
        for (uint32_t key : {0u, 1u, 0xDEADBEEFu, 0x12345678u}) {
            std::vector<bool> hit(n);
            bool ok = true;
            for (uint16_t i = 0; i < n; i++) {
                uint16_t v = bank::permute(i, n, key);
                if (v >= n || hit[v]) { ok = false; break; }
                hit[v] = true;
            }
            checks++;
            if (!ok) { failures++; printf("FAIL permute n=%d key=%08x is not a bijection\n", n, key); }
        }
    }
    // Different keys give different orders.
    int same = 0;
    for (uint16_t i = 0; i < 100; i++) same += bank::permute(i, 100, 1) == bank::permute(i, 100, 2);
    CHECK(same < 20);
}

static void testDeck() {
    bank::Deck d;
    d.shuffle(99);
    for (uint8_t s = 0; s < bank::SECTIONS; s++) {
        uint16_t n = bank::count(s);
        std::set<uint16_t> seen;
        for (uint16_t i = 0; i < n; i++) seen.insert(d.draw(s));
        CHECK_EQ(seen.size(), n);                   // a whole pass without a repeat
        std::vector<uint16_t> second;
        for (uint16_t i = 0; i < n; i++) second.push_back(d.draw(s));
        CHECK_EQ(std::set<uint16_t>(second.begin(), second.end()).size(), n);
    }
    // Saved and restored, the deal carries on where it was.
    bank::Deck a, b;
    a.shuffle(5);
    for (int i = 0; i < 7; i++) a.draw(SEC_ROUND);
    b = a;
    for (int i = 0; i < 20; i++) CHECK_EQ(a.draw(SEC_ROUND), b.draw(SEC_ROUND));
    // A deck from another bank starts over.
    b.bankId ^= 1;
    b.cursor[0] = 5;
    b.draw(0);
    CHECK_EQ(b.cursor[0], 1);
    CHECK_EQ(b.bankId, bank::id());
}

// Episodes of three CPUs on the real bank.
static Show S;

static void testEpisodes() {
    bank::Deck deck;
    deck.shuffle(1);
    long ticksTotal = 0, finals = 0, rounds = 0;
    const int N = 600;
    for (int n = 0; n < N; n++) {
        memset(&S, 0, sizeof S);
        S.kind[0] = P_ACE; S.kind[1] = P_DOT; S.kind[2] = P_BUZZ;
        S.seed(77 + n * 31u);
        S.newEpisode();
        long ticks = 0;
        while (S.phase != Phase::EpisodeEnd && ticks < 400000) {
            S.update(0, 0, 0, false);
            Event e;
            while (S.popEvent(e)) {
                if (e.type == Ev::NeedPuzzle) {
                    char text[bank::TEXT_MAX], cat[bank::CAT_MAX];
                    bank::fetch(e.a, deck.draw(e.a), text, cat);
                    S.supply(text, cat);
                }
                finals += e.type == Ev::FinalBell;
                rounds += e.type == Ev::RoundWon && e.c == SK_ROUND;
            }
            ticks++;
        }
        checks++;
        if (S.phase != Phase::EpisodeEnd) { failures++; printf("FAIL episode %d never ended\n", n); }
        ticksTotal += ticks;
        CHECK_EQ(S.dropped(), 0);
    }
    printf("bank episodes: %d, %ld rounds, final spin in %ld%% of episodes, mean %ld ticks "
           "(no presentation)\n", N, rounds, finals * 100 / N, ticksTotal / N);
    CHECK_EQ(rounds, N * 3);
}

// The card's bank: every record against the builder's list, then the ways a
// card can let the game down.
static void testCard() {
    std::vector<Ref> ref = loadRef("tools/phrases/build/sd_ref.txt");
    FILE *f = fopen("sdcard/PHRASES.BNK", "rb");
    CHECK(f != nullptr && !ref.empty());
    if (!f) return;
    cardData.resize(4 << 20);
    cardData.resize(fread(cardData.data(), 1, cardData.size(), f));
    fclose(f);
    CHECK_EQ(cardData.size() % 512, 0);
    vc.setup("sdcard/PHRASES.BNK", (uint32_t)cardData.size());

    uint32_t flashId = bank::id();
    uint16_t flashRounds = bank::count(SEC_ROUND);
    cardIn = true;
    bank::begin();
    CHECK(bank::card());
    CHECK(bank::id() != flashId);
    size_t total = 0, k = 0;
    for (uint8_t s = 0; s < bank::SECTIONS; s++) total += bank::count(s);
    CHECK_EQ(total, ref.size());
    for (uint8_t s = 0; s < bank::SECTIONS; s++)
        for (uint16_t i = 0; i < bank::count(s); i++, k++) {
            char text[bank::TEXT_MAX], cat[bank::CAT_MAX];
            checks++;
            if (!bank::fetch(s, i, text, cat) || ref[k].section != s || ref[k].text != text || ref[k].cat != cat) {
                failures++;
                printf("FAIL card puzzle %d/%d: got \"%s\" [%s]\n", s, i, text, cat);
                return;
            }
        }
    // The deal covers the card's sections too.
    bank::Deck d;
    d.shuffle(3);
    std::set<uint16_t> seen;
    for (uint16_t i = 0; i < bank::count(SEC_ROUND); i++) seen.insert(d.draw(SEC_ROUND));
    CHECK_EQ(seen.size(), bank::count(SEC_ROUND));

    // A read that fails mid-game: fetch says so, the card is dropped, and the
    // built-in bank answers from then on.
    char text[bank::TEXT_MAX], cat[bank::CAT_MAX];
    cardReads = 0; failAt = 1;                      // the record arrives, the header does not
    CHECK(!bank::fetch(SEC_ROUND, 0, text, cat));
    CHECK(!bank::card());
    CHECK_EQ(bank::id(), flashId);
    CHECK_EQ(bank::count(SEC_ROUND), flashRounds);
    CHECK(bank::fetch(SEC_ROUND, 0, text, cat));
    CHECK(text[0] != 0);
    failAt = -1;
    // A deck dealt from the card starts over on the built-in bank.
    d.draw(SEC_ROUND);
    CHECK_EQ(d.bankId, flashId);
    CHECK_EQ(d.cursor[SEC_ROUND], 1);

    // Back in the slot: found again. An index past the end drops it.
    bank::begin();
    CHECK(bank::card());
    CHECK(!bank::fetch(SEC_BONUS, bank::count(SEC_BONUS), text, cat));
    CHECK(!bank::card());

    // A damaged record (its check byte no longer adds up) is refused.
    bank::begin();
    cardData[512 + 5] ^= 0x40;
    CHECK(!bank::fetch(0, 0, text, cat));
    cardData[512 + 5] ^= 0x40;
    // Not a bank at all, and no card.
    cardData[0] = 'X';
    bank::begin();
    CHECK(!bank::card());
    cardData[0] = 'W';
    cardIn = false;
    bank::begin();
    CHECK(!bank::card());
}

int main(int argc, char **argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char *path = argc > 1 ? argv[1] : "tools/phrases/build/bank_ref.txt";
    std::vector<Ref> ref = loadRef(path);
    if (ref.empty()) { printf("FAIL cannot read %s (run tools/phrases/build_bank.py)\n", path); return 1; }
    testDecode(ref);
    testPermute();
    testDeck();
    testEpisodes();
    testCard();
    printf("bank: %u puzzles (%u round, %u toss-up, %u bonus); %ld checks, %ld failures\n",
           (unsigned)ref.size(), bank::count(0), bank::count(1), bank::count(2), checks, failures);
    return failures ? 1 : 0;
}
