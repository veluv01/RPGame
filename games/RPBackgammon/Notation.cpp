// Writing a play in backgammon notation (see Notation.h).
#pragma GCC optimize("Os", "no-ipa-sra")
#include <string.h>
#include "Notation.h"
#include "Rules.h"
#include <rpgame/Fmt.h>     // the RPGame library's formatting alone: pure logic, built by the host tests too

// A checker's move: the points it touched, and which of its steps hit.
struct Tok { uint8_t pts[5], hits, n; };

static char *point(char *p, uint8_t q) {
    return q == bg::BAR ? fmtStr(p, "BAR") : q == bg::OFF ? fmtStr(p, "OFF") : fmtInt(p, q);
}

static char *write(char *p, const Tok &t) {
    p = point(p, t.pts[0]);
    for (uint8_t k = 1; k <= t.n; k++) {
        // Points passed through are named only where it hit.
        bool hit = (t.hits >> k) & 1;
        if (k < t.n && !hit) continue;
        *p++ = '/';
        p = point(p, t.pts[k]);
        if (hit) *p++ = '*';
    }
    *p = 0;
    return p;
}

char *notate(char *out, const uint8_t *from, const uint8_t *die, const uint8_t *hit, uint8_t n) {
    Tok t[4] = {};                   // zeroed: equal moves compare equal as bytes
    uint8_t nt = 0;
    for (uint8_t i = 0; i < n; i++) {
        // A checker carrying on from where an earlier one stopped (after a hit
        // there it can only be that one: no other of ours stood on a blot).
        int k = -1;
        for (int j = nt - 1; j >= 0 && k < 0; j--)
            if (t[j].pts[t[j].n] == from[i]) k = j;
        if (k < 0) { k = nt++; t[k].pts[0] = from[i]; t[k].n = 0; t[k].hits = 0; }
        Tok &c = t[k];
        c.pts[++c.n] = bg::landing(from[i], die[i]);
        if (hit[i]) c.hits = (uint8_t)(c.hits | (1 << c.n));
    }
    // Highest first.
    for (uint8_t i = 1; i < nt; i++)
        for (uint8_t j = i; j && t[j].pts[0] > t[j - 1].pts[0]; j--) { Tok x = t[j]; t[j] = t[j - 1]; t[j - 1] = x; }
    char *p = out;
    *p = 0;
    for (uint8_t i = 0; i < nt;) {
        uint8_t same = 1;
        while (i + same < nt && !memcmp(&t[i], &t[i + same], sizeof(Tok))) same++;
        if (p != out) *p++ = ' ';
        p = write(p, t[i]);
        if (same > 1) { *p++ = '('; p = fmtInt(p, same); *p++ = ')'; *p = 0; }
        i = (uint8_t)(i + same);
    }
    if (p == out) fmtStr(out, "NO MOVE");
    return out;
}
