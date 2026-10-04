# CHRoulette spec: betting layout, glove navigation, chips and limits

*Written while designing CHRoulette; paths and names brought up to date on 2026-10-02.*

I checked the geometry with a throwaway Python model that only printed to stdout: real FONT35 glyphs, the `hand.png` glove, the real spot set, and a simulation of `nearest()`. All coordinates are screen pixels, inclusive. Colours are palette names.

## 0. Things that do not fit as drafted

1. **Width is used to the last pixel.** The grid needs 128 px with no side margin: a 0-column 7 px wide inside, 12 number columns at a 9 px pitch (8 px inside), and a 2:1 column 10 px wide inside.
   - Two-digit numbers (7 px) sit 1 px from one side of their cell and touch the other.
   - "00" fills its 7 px cell exactly.
   - "2:1" and "19-36" need custom kerning (§1.4). "19-36" fills its 17 px cell exactly.
2. **Glyph set.** The task lists `;`, but FONT35 had no `;` (`IDX35[';' - 32] == -1` in CHChess's copy of the font). Also `&`, `"` and `@` were missing. The font is now the CHGame library's (`chgame/Draw.cpp`, `glyph35()`), which has since gained `;`, `&` and `"`; `@` is still blank.
3. **Plate overflow.** One plate is too wide: TOP LINE with numbers and an amount is 121 px, against 120 available. Rule: if the words exceed 120 px, drop the number list ("TOP LINE $100 6 TO 1").
4. **Chess `nearest()` copied as-is does not work here.** Three failures showed up in simulation:
   - From wide cells it skips rows (UP from `$10` jumps to 2nd 12, missing RED).
   - It picks diagonal targets in the half-plane (RIGHT from SPIN goes to the 2:1 cell; LEFT from CLR goes to 0).
   - Held runs stall on line rows.

   §3 gives a banded variant that keeps chess's scoring as a fallback.
5. **Zero colour.** The layout fills 0/00 with `ZERO = FELT_LT`, which follows the felt theme.
   - On RED_FELT, FELT_LT (0xC44) reads as red next to RED (0xE12).
   - Recommendation: replace RED_FELT in roulette's theme table with a non-red felt (e.g. teal 0x033/0x165/0x3A9). The zero then shows the house colour on every theme.
   - If the palette owner frees a slot (e.g. turns CYAN into a fixed green), only the `ZERO` constant changes.
6. **American 0/2 and 00/2 splits are not offered.** They would sit on quarter-cell points 2.5 px apart. 0/00, 0/1, 00/3, the three trios and the top line are kept.
7. **BJ `Event.amount` is int16.** A single spin can return up to $15,600 (§6), which fits int16, but use int32 for safety.
8. **No 64-bit masks.** Use a `covers(id, n)` predicate plus `count(id)`. A variable `1ull << n` on RV32 may pull in `__ashldi3`.
9. **Bets are not saved.** 159 bets do not fit the 256 B save page next to the record. SAVE + QUIT during betting refunds the bets to the purse.
10. **No room for a racetrack or call bets.**

## 1. Pixel layout of the table band (rows 46..127)

### 1.1 Bands

| Rows | Content |
|---|---|
| 46 | FELT_DK hline, full width (rail shadow, as in BJ) |
| 47 | FELT |
| 48..78 | Number grid. GOLD lines at y = 48, 58, 68, 78 |
| 79..87 | Dozens (inside), line at 88 |
| 89..97 | Even money (inside), line at 98 |
| 99 | FELT |
| 100..110 | Hover plate. Chess `plate()`: NAVY fill, GOLD roundRect r2, h 11, text at y 103, centred on x 64 |
| 111 | FELT_DK hline (no gold trim; it would merge with the plate border) |
| 112..127 | Action bar. NAVY, INK hline at 112, buttons at y 114..125 (hovered button lifts to 113) |

### 1.2 Grid (European and American)

Number n = 1..36: column `c = (n-1)/3 + 1` (1..12), row `r = (n-1)%3` (0 = bottom row 1, 4, 7..; 2 = top row 3, 6..36).

- Cell inside: `x0 = 9c .. 9c+7` (8 px), `y0 = 69 - 10r .. y0+8` (9 px).
- Fill: RED if red(n), else INK. Red rule: in 1..10 and 19..28 the odd numbers are red; in 11..18 and 29..36 the even ones are.
- Digits: WHITE at `(9c + (n < 10 ? 3 : 1), y0 + 2)`.
  - One digit: 3x5, margins 3/2 px across and 2/2 down.
  - Two digits: 7x5, margins 1/0 across.
- Chip anchor (straight spot): `(9c + 4, y0 + 4)`.

| c | inside x | anchor x | c | inside x | anchor x |
|---|---|---|---|---|---|
| 1 | 9..16 | 13 | 7 | 63..70 | 67 |
| 2 | 18..25 | 22 | 8 | 72..79 | 76 |
| 3 | 27..34 | 31 | 9 | 81..88 | 85 |
| 4 | 36..43 | 40 | 10 | 90..97 | 94 |
| 5 | 45..52 | 49 | 11 | 99..106 | 103 |
| 6 | 54..61 | 58 | 12 | 108..115 | 112 |

Rows: top 49..57 (anchor y 53), middle 59..67 (63), bottom 69..77 (73).

**Zero column (x 0..8):**

| | Inside | Fill | Label | Anchor |
|---|---|---|---|---|
| European 0 | x 1..7, y 49..77 | ZERO | "0" at (3, 61) | (4, 63) |
| American 00 | x 1..7, y 49..62 | ZERO | "00" at (1, 53), fills the width | (4, 55) |
| American 0 | x 1..7, y 64..77 | ZERO | "0" at (3, 69) | (4, 71) |

The American divider is a GOLD hline at y 63, x 0..8. The 0/00 split anchor is (4, 63).

**2:1 column cells** (`k = 0, 1, 2` = 1st, 2nd, 3rd column = bottom, middle, top row):
- Inside: x 117..126, y `69 - 10k .. +8`.
- Background: FELT.
- Anchor: (122, 73 / 63 / 53).

**Dozens** (`k = 0..2`):
- Inside: x `9 + 36k .. 43 + 36k` (35 px), y 79..87.
- Labels "1st 12", "2nd 12", "3rd 12" (23 px each) in WHITE at `(x0 + 6, 81)`.
- Chip anchor: (26 / 62 / 98, 83).

**Even money** (`k = 0..5`: LOW, EVEN, RED, BLACK, ODD, HIGH):
- Inside: x `9 + 18k .. 25 + 18k` (17 px), y 89..97.
- Chip anchor: `(17 + 18k, 93)` = 17, 35, 53, 71, 89, 107.
- "1-18" (15 px) at x 10. "EVEN" (15 px) at x 28. "ODD" (11 px) at x 84.
- "19-36": kerned, 17 px wide in a 17 px cell.
- RED and BLACK: flat 2:1 diamonds 13x7, centred (53, 93) and (71, 93).
  - Rows 91..95 with half-widths 1, 3, 5, 3, 1, filled RED or INK.
  - GOLD pixel at each row end, plus GOLD points at (cx, 90) and (cx, 96).

**GOLD grid lines:**
- Horizontal:
  - y 48 and y 78: x 0..127.
  - y 58 and y 68: x 8..127.
  - y 88 and y 98: x 8..116.
- Vertical:
  - x = 0 and x = 127: y 48..78.
  - x = 8 + 9k (k = 0..12, i.e. 8, 17, 26, …, 116): y 48..78.
  - x = 8, 44, 80, 116: y 78..98.
  - x = 26, 62, 98: y 88..98.

The blocks under the zero column (x 0..7) and the 2:1 column (x 117..127) in rows 79..98 stay plain FELT.

**Number cells are filled.** RED and INK cells with WHITE digits read the same on every felt theme. Red digits on green felt would be about the same brightness (luma ≈ 5.2 against 4.6) and unreadable. The felt only shows in the outside cells and the margins.

### 1.3 Schematic (about 2.25 px per character)

```
 y   x:0  8   17  26  35  44  53  62  71  80  89  98  107 116  127
 46  ,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,  FELT_DK
 48  +--+---+---+---+---+---+---+---+---+---+---+---+---+----+  GOLD
 49  |  | 3 | 6 | 9 |12 |15 |18 |21 |24 |27 |30 |33 |36 |2:1 |  top 49-57
 58  |  +---+---+---+---+---+---+---+---+---+---+---+---+----+
 59  |0 | 2 | 5 | 8 |11 |14 |17 |20 |23 |26 |29 |32 |35 |2:1 |  mid 59-67
 68  |  +---+---+---+---+---+---+---+---+---+---+---+---+----+
 69  |  | 1 | 4 | 7 |10 |13 |16 |19 |22 |25 |28 |31 |34 |2:1 |  bot 69-77
 78  +--+---+---+---+---+---+---+---+---+---+---+---+---+----+
 79     |    1st 12     |    2nd 12     |    3rd 12     |       dozens 79-87
 88     +-------+-------+-------+-------+-------+-------+
 89     | 1-18  | EVEN  |  <R>  |  <B>  |  ODD  | 19-36 |       even   89-97
 98     +-------+-------+-------+-------+-------+-------+
100            (( SPLIT 17/20 $10 17 TO 1 ))                   plate 100-110
111  ,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,  FELT_DK
112  ##########################################################  INK
114  [ CLR ][ $1 ][ $5 ][ $10][ $25][$100][      SPIN       ]   bar 112-127
American zero column:   48 +--+   49 |00|   63 +--+ (x0..8 only)   69 |0 |   78 +--+
```

### 1.4 Pixel-exact excerpts

Legend: Y GOLD, R RED, K INK, W WHITE, g ZERO, `.` FELT, `,` FELT_DK, A FX_A highlight ring. Glove: k INK, w WHITE, s SILVER, y GOLD, b WOOD. Chip: B BLUE, G FELT_LT, m WINE, n NAVY, d FELT_DK.

**European, x 0..47.** The glove hovers STRAIGHT 5: anchor (22, 63), fingertip (22, 59). Chips:
- $25 on 0.
- $5 on split 1/4.
- 2 × $1 on first four.
- 3 × $10 on corner 7/8/10/11.

The digit 5 stays readable. The palm hides 6 and reaches over the rail.

```
 44 ????????????????????kkkkkkkkk?????????????????
 46 ,,,,,,,,,,,,,,,,,,,kbyyyyyyybk,,,,,,,,,,,,,,,,,
 48 YYYYYYYYYYYYYYYYYYkwwwwwwwwwskYYYYYYYYYYYYYYYYY
 51 YgggggggYRRRWWWRRkwwswwswswwskWWWRRYRRWRRWWWYKK
 54 YgggggggYRRRRRWRRYKkkwwssssskRRRWRRYRRWRRWRRYKK
 56 YgggggggYRRRRRRRRYKKkwwskKYRRRRRRRRYRRRRRRRRYKK
 58 YgggggggYYYYYYYYYAAAkwwskAAYYYYYYYYYYYYYYYYYYYY
 59 YgggggggYKKKKKKKKAAAAkkkAAAKKKKKKKKYKKKKKKKKYRR
 60 YgggggggYKKKKKKKKARRRRRRRRAKKKKKKKKYKKKKKKKKYRR
 61 YgkkkkkgYKKKWWWKKARRRWWWRRAKKKWWWKKYKKWKKKWKYRR
 62 YkGGWGGkYKKKKKWKKARRRWRRRRAKKKWKWKKYKWWKKWWKYRW
 63 YkGGGGGkYKKKWWWKKARRRWWWRRAKKKWWWKKYKKWKKKWKYRR
 64 YkdWdWdkYKKKWKKKKARRRRRWRRAKKKWKWKKYKKkkkkkKYRR
 65 YgkkkkkgYKKKWWWKKARRRWWWRRAKKKWWWKKYKkBBWBBkYRW
 67 YgggggggYKKKKKKKKAAAAAAAAAAKKKKKKKKYKknWnWnkYRR
 68 YgggggggYYYYYYYYYAAAAAAAAAAYYYYYYYYYYknWnWnkYYY
 71 YgggggggYRRRRWRkkkkkKWKWKKYRRRWWWRRYKKWKKWWWYKK
 74 YgggggggYRRRRWkmWmWmkKKWKKYRRRRRWRRYKKWKKWKWYKK
 76 YggggkWWBWWkRRRRRYKKKKKKKKYRRRRRRRRYKKKKKKKKYKK
 78 YYYYYkSBSBSkYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY
 80 ......kkkkk.................................Y..
```

**Right end, x 96..127.** The kerned "2:1" (9 px) and "19-36" (17 px):

```
    67890123456789012345678901234567
 51 WWYKWWWKWWWYRWWWRWWWY.WWW....W.Y
 52 RWYKKKWKKKWYRRRWRWRRY...W.W.WW.Y
 53 RWYKKWWKKWWYRRWWRWWWY.WWW....W.Y
 54 RWYKKKWKKKWYRRRWRWRWY.W...W..W.Y
 55 WWYKWWWKWWWYRWWWRWWWY.WWW...WWWY
 91 ..Y.W..WWW...WWW.WWWY...........
 92 ..YWW..W.W.....W.W..Y...........
 93 ..Y.W..WWW.WW.WW.WWWY...........
 94 ..Y.W....W.....W.W.WY...........
 95 ..YWWW.WWW...WWW.WWWY...........
```

**Kerning rules:**
- "2:1" at (118, y): `text35(118,"2")`, pixels at (122, y+1) and (122, y+3), then `text35(124,"1")`.
- "19-36": `text35(99,"19")`, `hline(107, 93, 2)`, `text35(109,"36")`. Column 0 of the 3 is empty at row 2, so the dash keeps a 1 px gap.

## 2. Bet spots

### 2.1 ID space (`NBET = 159`, `NSPOT = 166`)

| id | spot |
|---|---|
| 0..143 | lattice, `id = v*24 + u`, u 0..23, v 0..5 |
| 144 | 0 |
| 145 | 00 (US only) |
| 146 | 0/00 split (US only) |
| 147..149 | 1st, 2nd, 3rd column |
| 150..152 | dozens |
| 153..158 | LOW, EVEN, RED, BLACK, ODD, HIGH |
| 159..165 | bar: CLR, $1, $5, $10, $25, $100, SPIN |

**Lattice anchor:** `XL(u) = (9u + 16 + (u & 1)) / 2` and `YL(v) = 78 - 5v`.
- Even u is a vertical grid line (x 8, 17, …, 107).
- Odd u is a column centre (13, 22, …, 112).
- v = 0 is the bottom edge (y 78). Odd v is a row centre (73, 63, 53). Even v of 2 or 4 is a row line (68, 58).

| u | v | type | covers (`cols(u) × rows(v)`) | count | pays |
|---|---|---|---|---|---|
| odd | odd | straight | `{(u+1)/2} × {(v-1)/2}` | 1 | 35 |
| even ≥ 2 | odd | split, side by side | `{u/2, u/2+1} × {row}` | 2 | 17 |
| odd | 2, 4 | split, one above the other | `{col} × {v/2-1, v/2}` | 2 | 17 |
| even ≥ 2 | 2, 4 | corner | 2 × 2 | 4 | 8 |
| odd | 0 | street | `{col} × {0, 1, 2}` | 3 | 11 |
| even ≥ 2 | 0 | six line | 2 × 3 | 6 | 5 |
| 0 | any | zero line (column "0" counts as the zero cell) | see next table | | |

A number is `n = 3(c-1) + r + 1`.

The zero column contributes these numbers at u = 0:
- European: always {0}.
- American: v ∈ {0, 3} gives {0, 00}; v ∈ {1, 2} gives {0}; v ∈ {4, 5} gives {00}.

| u=0, v | European | American |
|---|---|---|
| 0 | first four 0/1/2/3, pays 8 | top line 0/00/1/2/3, pays 6 |
| 1 | split 0/1 | split 0/1 |
| 2 | trio 0/1/2 | trio 0/1/2 |
| 3 | split 0/2 | trio 0/00/2 |
| 4 | trio 0/2/3 | trio 00/2/3 |
| 5 | split 0/3 | split 00/3 |

**Payout.** `count = ncols * nrows`, with the zero column adding `zc` (1, or 2 on the American v = 0 and v = 3 rows) instead of a column. `payout = 36 / count - 1` gives every standard payout: 35, 17, 11, 8, 6, 5, 2 and 1. No payout table is needed.

Outside predicates:
- Column k: `(n-1) % 3 == k`.
- Dozen k: `(n-1) / 12 == k`.
- LOW / HIGH: n ≤ 18 / n ≥ 19.
- EVEN / ODD: parity.
- RED / BLACK: the red rule.
- Zero is never covered by an outside bet.

**Counts:**
- European: 157 bet spots (37 straights, 60 splits, 12 streets + 2 trios, 22 corners + first four, 11 six lines, 12 outside) plus 7 bar slots, so 164 cursor stops.
- American: 159 bet spots (38 straights, 60 splits, 12 streets + 3 trios, 22 corners, 1 top line, 11 six lines, 12 outside), so 166 stops.

### 2.2 Representation and cost

**Procedural, pure, shared by rules and renderer (`Spots.h/.cpp`, about 0.6 KB flash):**
- `valid(id, us)`
- `geo(id, us, Geo&)`, where `Geo {uint8 ax, ay, x0, y0, x1, y1}` holds the chip anchor and the inside extent (zero size for lattice points)
- `covers(id, n, us)`
- `count(id, us)` and `payout()`
- `kind()` and `straightId(n)`

A generated table would cost about 1 KB for geometry plus 795 B for 40-bit masks.

**Rules:**
- `uint8_t bet[NBET]` (159 B). This works with per-spot caps ≤ 255 (§6). Use `uint16_t` (318 B) if the caps rise.
- `int32_t total` and `int32_t purse`.
- As in BJ, money leaves the purse when a chip is placed.
- `bet[]` is kept after settlement, so rebet means "keep `bet[]` and deduct `total` if affordable, otherwise clear". No `last[]` copy is needed.

**Presenter (displayed state):**
- `uint8_t vis[20]` (stake on the felt) and `uint8_t paid[20]` (win chips added), 40 B of bitsets.
- Displayed amount for spot s:
  - If `vis(s)` is clear: 0.
  - Otherwise: `bet[s] - inflight(s, DROP/REBET) + (paid(s) ? bet[s]*payout(s) - inflight(s, WIN_IN) : 0)`.
- The plaque's BET shows `total - Σ inflight DROP`.
- The BJ `Fly[16]` pool is reused with `seat` renamed to `spot`: 16 × 20 B.
- Net new RAM is about 210 B plus the pool. Removing cards frees about 860 B.

## 3. Navigation (`nearest()`, adapted from CHChess `Screens.cpp`)

**Glove position:**
- Normally the chip anchor.
- For dozens and even-money cells the x is sticky: `gx = clamp(previous gx, x0+4, x1-4)`. This makes vertical runs reversible. The chip still lands at the cell centre and drops in a short diagonal from the fingertip.
- Bar slots: centre x 8, 26, 43, 60, 77, 94, 115 at y 119.

**Algorithm.** `along = dx*ux + dy*uy` and `side = |dx*uy - dy*ux|`, measured from the glove to each candidate's anchor.

```
band   = candidate's extent across the motion (y0..y1 when moving left/right, x0..x1 when moving up/down)
         overlaps the from-spot's extent; tolerance 0 for left/right, 2 px for up/down
tier 1 = in band and along > 0                          -> minimum (along, side)
tier 2 = (up/down only) along > 0 and side <= along     -> minimum along + 2*side   (chess score, 45-degree cone)
wrap   = (taps only) in band and along <= 0             -> farthest (most negative along, then side),
         otherwise chess's minimum 4*along + side
```

**Tap versus run:**
- **Tap:** `justPressed`, the first event. Candidates are all valid spots, so lines and corners are visited. Wrap is allowed.
- **Run:** a `repeat` event that is not `justPressed`, firing from held = 23 and then every 5 frames.
  - Candidates are the coarse set: straights, 0 and 00, outside cells, and the bar (not 146).
  - No wrap: at an edge the glove stays put, silently.
  - From a lattice line point, snap the coordinate across the motion to the cell `(u|1, v|1)` (the cell up and to the right) so runs never stall on line rows.
- Two directions pressed together give a diagonal, as in chess.
- Cost: 166 × `geo()` per press, under 0.3 ms.

**Simulated results** (European unless marked):

| from | tap U | tap D | tap L | tap R | run U | run D | run L | run R |
|---|---|---|---|---|---|---|---|---|
| 17 | 17/18 | 16/17 | 14/17 | 17/20 | 18 | 16 | 14 | 20 |
| 17/18/20/21 | 18/21 | 17/20 | 17/18 | 20/21 | 21 | 20 | 18 | 21 |
| 16-18 street | 16 | 2nd 12 | 13-18 six | 16-21 six | 16 | 2nd 12 | 13 | 19 |
| 0 | 0/2/3 | 0/1/2 | 2:1 mid (wrap) | 0/2 | 3 | 1st 12 | stay | 2 |
| 0/1/2/3 | 0/1 | 1st 12 | 34-36 (wrap) | 1/2/3 | 1 | 1st 12 | 0 | 1 |
| US 0 | 0/1/2 | 0/1 | 2:1 bot | 0/1 | 00 | 1st 12 | stay | 1 |
| US 00 | 00/3 | 00/2/3 | 2:1 top | 00/3 | stay | 0 | stay | 3 |
| US 0/00 | 00 | 0 | 2:1 mid | 0/00/2 | 00 | 0 | stay | 2 |
| 36 | SPIN (wrap) | 35/36 | 33/36 | 2:1 top | stay | 35 | 33 | 2:1 top |
| 2:1 bot | 2:1 mid | 3rd 12 | 34 | 0 (wrap) | 2:1 mid | 3rd 12 | 34 | stay |
| 2nd 12 | six line under the glove x | RED / BLACK (by glove x) | 1st 12 | 3rd 12 | 16 | RED | 1st 12 | 3rd 12 |
| RED | 2nd 12 | $10 | EVEN | BLACK | 2nd 12 | $10 | EVEN | BLACK |
| CLR | 1-18 | 0/3 (wrap) | SPIN (wrap) | $1 | 1-18 | stay | stay | $1 |
| SPIN | 19-36 | 36 (wrap) | $100 | CLR (wrap) | 19-36 | stay | $100 | stay |

Wrapping makes UP from the top row a one-press shortcut to the bar (and to SPIN on the right).

**Glove:**
- 13x16 chess HAND, fingertip at column 5.
- Fingertip = glove anchor − (0, 4). Draw `sprite4(HAND, tx-5, ty-15+bob, rm)`; it clips, as checked.
- Bob is reduced to ±1: `(isin((frame>>3)*40) + 128) >> 8`. ±2 would blur 4–5 px half-steps.
- Tap dip on A: `tapT` 1..8, `dip = (tapT < 4 ? tapT : 8-tapT) >> 1`.
- Deny: RM_ALERT, `denyT = 24`.
- Glide: chess Q4 halving.

**Occlusion:**
- The 4 px offset keeps the hovered cell's digit fully visible: the tip stays in rows y0−1..y0+1, above digit rows y0+2..6.
- The palm hides about 13x11 px above the tip, i.e. the cell above. On the top row it reaches the rail and wall (rows 34..45), so the wall band must redraw while the glove moves or bobs there.
- On line spots the upper neighbour's digit is covered. The plate and highlight rings make up for it.
- The glove is drawn in the overlay pass after the bar.
- Fallback if playtests say it is too big: a 9x12 roulette glove, about 75 B.

**Highlight:**
- Palette HOVER mode while betting (FX_A grey ramp, as for chess hover).
- Every covered number cell gets an FX_A ring drawn on its grid lines, plus inner rows y0 and y1 (digit-free), giving a 2 px top and bottom.
  - Outside bets also ring their own cell. RED lights all 18 red cells.
  - Up to 18 rings × 6 lines is about 0.2 ms.
- A spot that has chips draws its stack outline in FX_A instead of INK (the chess INK→FX_A outline idiom).
- An empty line or corner spot shows a 7x3 FX_A "ghost chip" ring at the anchor, where the chip will land.
- Hovered bar slot: lifts 1 px and gets an FX_A roundRect.
- Fallback if the grey ramp is weak on RED/INK: TARGETS mode (cyan shimmer).

## 4. Chips on the layout

**Mini chip, centred on the anchor (x, y):**
- 7 px wide, `n + 4` rows tall, where `n = min(4, greedy chip count)`.
- Colours from BJ `CHIP_BODY/EDGE/SHADE` for `d = chipDenom(amount)`. The whole stack uses the largest denomination's colours.

```
y-n-1: .OOOOO.          top outline (O = INK, or FX_A when hovered)
y-n  : OBBEBBO          face with E accent at x
y-n+1: OBBBBBO
y-n+2 .. y+1 (n rows): OSESESO  one side band per chip (E at x-1 and x+1)
y+2  : .OOOOO.
```

- $100 is INK with GOLD flecks, which still shows on INK cells.
- $25 uses FELT_LT and so follows the theme, as in BJ.
- Cost is about 100 B.

**Draw order:**
1. Grid.
2. Rings.
3. Stacks, back to front: anchors by ascending y (v = 5..0, specials at their row, then dozens, then even money), x ascending within a row.
4. Dolly.
5. Plate.
6. Bar.
7. Overlay: flights, glove, particles, banner.

**Flights** (BJ `Fly`, OUT_CUBIC unless noted; the presenter spawns them one per 2 frames from paced rules events):

| kind | path | T (frames) | on landing |
|---|---|---|---|
| CHIP_DROP (A) | fingertip → anchor, OUT_BOUNCE, no arc | 6 | Sfx::Chip |
| CHIP_LIFT (B) | anchor → fingertip, then gone | 6 | none (bet already reduced) |
| REBET_IN | (−8, 132) → anchor, 8 px arc | 14 | sets `vis` at spawn |
| STACK_TO_TRAY (losers) | anchor → rack (76, 47) | 16 | Whoosh at spawn |
| WIN_IN | rack (60, 47) → anchor | 14 | sets `paid`, Sfx::Coin |
| STACK_TO_PLAYER (collect, CLR) | anchor → (8, 140) | 18 | `pending -= value`, purse rolls |

Each flight draws a mini chip (n = 1), or a mini stack for stacks, plus a 3 px INK shadow at the ground line when the arc is more than 2 px.

**Rules → presenter events** (Event.amount widened to int32): BetAdd, BetRemove, Rebet, Clear, Lose, Win, Collect. Clear, Lose, Win and Collect are paced one spot at a time.

## 5. Betting controls and plate wording

| Input | Effect |
|---|---|
| D-pad tap | Half step over all spots. Wraps. Sfx::Cursor |
| D-pad held | Whole cells over the coarse set. No wrap |
| A on a bet spot (repeats about 12/s) | Drop one active chip. On deny: Sfx::Deny, red glove, reason flashes in the plate |
| A on a chip button | Make that chip active (FX_B fill, lifted) |
| A on SPIN | Spin if total ≥ $1, otherwise plate "PLACE A BET FIRST" |
| A on CLR | First A arms it (plate "A AGAIN TO CLEAR $35"). A second A within 90 frames clears. B or any move disarms |
| B on a spot with a bet (repeats) | Take back `min(bet, active chip)` |
| B elsewhere | Sfx::Deny |
| SELECT | Next affordable chip (wraps). The active chip drops automatically if it becomes unaffordable |
| START | Pause: RESUME / OPTIONS / SAVE + QUIT |

- **Bar:** CLR x 0..16 (SILVER), chips at 18 + 17k, each 16 px wide (BJ `art::chip` plus label), SPIN 103..127 (GOLD). Gaps are 1 px.
- **Round start:** the cursor goes to SPIN if the bets were re-placed; otherwise the last spot, or 17 on the first round.
- **Plate wording:** words are WHITE name, GOLD numbers, CYAN " $amt" (WHITE if CYAN is repurposed), SILVER " k TO 1", joined chess-style.
  - STRAIGHT 17
  - SPLIT 17/20, SPLIT 0/00
  - STREET 16-18
  - TRIO 0/1/2, TRIO 00/2/3
  - CORNER 17/18/20/21
  - FIRST FOUR 0/1/2/3
  - TOP LINE 0/00/1/2/3
  - SIX LINE 13-18
  - 1st COLUMN
  - 1st DOZEN 1-12
  - LOW 1-18, HIGH 19-36, RED, BLACK, EVEN, ODD
- **Deny words** replace the payout and flash RED/SILVER on `denyT & 4`: " MAX $100", " TABLE MAX", " NO CASH", " NO BET".
- **Widths:** the worst cases are CORNER and FIRST FOUR with an amount, at 117 px. TOP LINE uses the drop rule from §0.3.
- **Bar hover:** no plate over chip buttons, which the glove would cover. CLR and SPIN messages are 70 px or less and centred, so they stay clear of the glove.

## 6. Limits and purse

| Item | Value | Note |
|---|---|---|
| Chips | $1 $5 $10 $25 $100 | BJ art |
| Inside bet, per spot | $1..$100 | Straight win $3,500 |
| Outside bet, per spot | $1..$250 | Keeps `bet[]` within uint8 (BJ MAX_BET 200 analogue) |
| Table total | $1,000 | Biggest one-number return: $15,600 |
| Starting purse | $500 | |
| Goal | $1000 / $5000 / ENDLESS | Same as BJ. Use $2500 / $10000 if $1000 proves too quick |

Purse, total and returns are int32.

**Cost estimate:** about 1.9 KB flash (Spots 0.6, layout draw 0.6, chips 0.2, nav and input 0.35, plate 0.25). A full table-band redraw takes about 1.5–2 ms, plus about 2 ms for the wall when the glove reaches it.

### Critical Files for Implementation
- CHChess\Screens.cpp
- CHChess\Stage.cpp
- CHBlackjack\Presenter.cpp
- CHBlackjack\CardArt.cpp
- CHBlackjack\Bar.cpp