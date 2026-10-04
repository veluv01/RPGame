// Finds the short game the title screen plays by itself: an opening, under
// American rules, that walks into a multiple jump. Prints it as the
// from, to squares of each hop for src/states/DemoLine.h.
//
//     zig c++ -std=gnu++17 -O2 -DCHTEST tools/tests/demo_line.cpp -o demo_line.exe
#include <stdio.h>
#include <vector>
#include "../../Engine.cpp"

int main() {
    uint32_t r = 2463534242u;
    auto rnd = [&]() { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r; };
    std::vector<eng::Step> best;
    int bestHops = 0;
    for (int game = 0; game < 200000; game++) {
        eng::newGame(eng::R_FORCED);
        std::vector<eng::Step> line;
        int moves = 0, hops = 0, longest = 0;
        while (moves < 12 && eng::status() == eng::NORMAL) {
            uint8_t n = eng::stepCount();
            uint8_t i = (uint8_t)(rnd() % n);
            eng::Step s;
            eng::stepAt(i, s);
            line.push_back(s);
            hops = s.cap != eng::NONE ? hops + 1 : 0;
            if (eng::play(i)) { moves++; if (hops > longest) longest = hops; hops = 0; if (longest >= 2 && moves >= 8) break; }
        }
        // The multiple jump has to be the last move, after a quiet build-up.
        int caps = 0;
        for (auto &s : line) caps += s.cap != eng::NONE;
        if (longest > bestHops && caps <= longest + 2) { bestHops = longest; best = line; }
        if (bestHops >= 3) break;
    }
    printf("// %d hops in the last move\n", bestHops);
    for (size_t i = 0; i < best.size(); i++) printf("%d, %d,%s", best[i].from, best[i].to, i % 6 == 5 ? "\n" : " ");
    printf("\n");
    return 0;
}
