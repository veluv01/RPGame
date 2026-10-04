#pragma once
/*
 * SpriteBatch - draw MANY small SD-card sprites per frame, with as few card
 * commands as possible, without breaking painter's order.
 *
 * ---------------------------------------------------------------------------
 * WHY
 * ---------------------------------------------------------------------------
 * spriteDraw() costs one card command per sprite. For a small sprite (one
 * 512-byte block) almost all of that is the card's own access latency:
 *
 *     command + first-block latency   ~0.6 ms   <- paid per COMMAND
 *     one block at 24 MHz by DMA      ~0.17 ms  <- paid per BLOCK
 *
 * so 30 sprites cost ~25 ms of SD time a frame, mostly spent waiting.
 * If the sprites' frames sit in consecutive blocks (a sheet file, see
 * spriteLoadSheet), ONE CMD18 can stream all of them for ~0.6 ms plus
 * ~0.17 ms per block, and every queued sprite can be drawn from its frame
 * as that block arrives, while the next one is still on the wire.
 *
 * The catch is order. The card delivers blocks in block order, but sprites
 * must be drawn back to front. SpriteBatch keeps both:
 *
 *   1. Depth layers. Sprites are queued back to front. A sprite's layer is
 *      one more than the deepest earlier sprite whose rectangle it overlaps.
 *      Sprites in the same layer never overlap each other, so within a
 *      layer the order does not matter, and block order is fine.
 *   2. One read plan per layer. The layer's blocks are sorted and merged
 *      into runs. A gap of up to SPRITE_BATCH_GAP_BLOCKS unused blocks is
 *      read through, because that is cheaper than a new command.
 *   3. Each run is one readBlocksPipelined(). The per-block callback blits
 *      every sprite of the layer that uses that block.
 *
 * Sprites that do not overlap all go in layer 0, so the whole batch is one
 * sheet stream. A tall stack of overlapping sprites degrades gracefully
 * toward one command per sprite, which is what spriteDraw() costs anyway.
 *
 * SPRITE_BATCH_IGNORE_DEPTH skips step 1 (everything in one layer). That is
 * the fastest possible case, and is correct when sprites never overlap or
 * their order does not matter (particles, bullets, a HUD).
 *
 * ---------------------------------------------------------------------------
 * USE
 * ---------------------------------------------------------------------------
 *     static SpriteFile frames[24];
 *     spriteLoadSheet(frames, 24, "WALK.BIN");         // setup(), once
 *
 *     static SpriteBatch batch;                         // ~850 B: keep it static
 *     spriteBatchBegin(batch);
 *     for (each sprite, back to front)
 *         spriteBatchAdd(batch, frames[f], x, y, 0);   // 0 = transparent index
 *     spriteBatchDraw(batch, 0, &dirty);
 *
 * Limits: sprites must fit in ONE block (2 + ceil(w/2)*h <= 512 bytes, e.g.
 * 32x30 or 23x27); spriteBatchAdd() returns false otherwise, and the caller
 * falls back to spriteDraw(). Use even x for the fast transparent blit.
 */
#include "RPSpriteView.h"

/* Maximum sprites per batch. The struct lives in the caller, so change it
 * only with a global -D (every file must agree on the layout). 12 bytes
 * plus 1 byte of scratch per sprite. */
#ifndef SPRITE_BATCH_MAX
#define SPRITE_BATCH_MAX 64
#endif

/* Read through up to this many unwanted blocks rather than issue a new
 * command: ~0.17 ms per block against ~0.65 ms per command on the test
 * card. */
#ifndef SPRITE_BATCH_GAP_BLOCKS
#define SPRITE_BATCH_GAP_BLOCKS 3
#endif

/* spriteBatchDraw() flags */
#define SPRITE_BATCH_IGNORE_DEPTH 0x01   /* one layer: fastest, order not kept */

struct SpriteBatchItem {
    uint32_t block;          /* card LBA of the sprite's one block          */
    int16_t  x, y;
    uint8_t  w, h;
    int8_t   transparent;    /* -1 opaque, else the palette index to skip   */
    uint8_t  layer;
};

/* What the last spriteBatchDraw() did, for tuning and HUDs. */
struct SpriteBatchStats {
    uint8_t  layers;         /* depth layers used                           */
    uint16_t reads;          /* card commands issued                        */
    uint16_t blocks;         /* blocks read (including read-through gaps)   */
};

struct SpriteBatch {
    uint8_t          n;
    SpriteBatchItem  item[SPRITE_BATCH_MAX];
    uint8_t          order[SPRITE_BATCH_MAX];   /* scratch: by (layer, block) */
    SpriteBatchStats stats;
};

void spriteBatchBegin(SpriteBatch &b);

/* Queue a sprite. Call in back-to-front order. Fully off-screen sprites are
 * accepted and dropped. Returns false if the batch is full or the sprite is
 * not a single-block sprite (draw that one with spriteDraw()). */
bool spriteBatchAdd(SpriteBatch &b, const SpriteFile &s, int x, int y,
                    int transparent = -1);

/* Read and draw everything queued, into gfx_fb only (flush it yourself).
 * Waits for any async flush first, like spriteDraw(). If `dirty` is given,
 * every drawn rectangle is added to it. Returns SPRITE_OK, or
 * SPRITE_ERR_IO if any read failed (the other runs are still drawn). */
int spriteBatchDraw(SpriteBatch &b, uint8_t flags = 0, SpriteDirty *dirty = nullptr);
