/*
 * SpriteBatch.cpp - see SpriteBatch.h for the why and the how.
 */
#include "SpriteBatch.h"

void spriteBatchBegin(SpriteBatch &b)
{
    b.n = 0;
    b.stats.layers = 0;
    b.stats.reads  = 0;
    b.stats.blocks = 0;
}

bool spriteBatchAdd(SpriteBatch &b, const SpriteFile &s, int x, int y, int transparent)
{
    if (s.w == 0 || s.blocks != 1 || s.tailLen) return false;
    if (x >= GFX_W || y >= GFX_H || x + s.w <= 0 || y + s.h <= 0) return true;
    if (b.n >= SPRITE_BATCH_MAX) return false;

    SpriteBatchItem &it = b.item[b.n++];
    it.block       = s.firstBlock;
    it.x           = (int16_t)x;
    it.y           = (int16_t)y;
    it.w           = s.w;
    it.h           = s.h;
    it.transparent = (int8_t)transparent;
    it.layer       = 0;
    return true;
}

static inline bool overlaps(const SpriteBatchItem &a, const SpriteBatchItem &b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

/* Sort key: layer first (drawn in order), then block (card order). */
static inline uint32_t sortKey(const SpriteBatchItem &it)
{
    return ((uint32_t)it.layer << 24) | (it.block & 0x00FFFFFFu);
}

/* State shared with the per-block callback for one run. */
struct RunCtx {
    SpriteBatch *b;
    uint8_t      cur;        /* next entry of b->order to draw     */
    uint8_t      end;        /* one past the run's last entry      */
    uint32_t     blk;        /* LBA of the block being handed over */
    SpriteDirty *dirty;
};

/* Runs while the NEXT block of the run is arriving by DMA. */
static void onBlock(const uint8_t *block, void *user)
{
    RunCtx &c = *static_cast<RunCtx *>(user);
    while (c.cur < c.end) {
        const SpriteBatchItem &it = c.b->item[c.b->order[c.cur]];
        if (it.block != c.blk) break;
        /* The whole sprite is in this one block: one blit, straight from
         * the DMA buffer, skipping the 2-byte header. */
        gfx_blit(block + SPRITE_HEADER_BYTES, it.x, it.y, it.w, it.h, it.transparent);
        if (c.dirty) spriteDirtyAddRect(*c.dirty, it.x, it.y, it.w, it.h);
        c.cur++;
    }
    c.blk++;
}

int spriteBatchDraw(SpriteBatch &b, uint8_t flags, SpriteDirty *dirty)
{
    b.stats.layers = 0;
    b.stats.reads  = 0;
    b.stats.blocks = 0;
    if (b.n == 0) return SPRITE_OK;

    /* ---- 1. depth layers ------------------------------------------------ */
    uint8_t layers = 1;
    if (!(flags & SPRITE_BATCH_IGNORE_DEPTH)) {
        for (uint8_t i = 1; i < b.n; i++) {
            uint8_t L = 0;
            for (uint8_t j = 0; j < i; j++) {
                if (b.item[j].layer >= L && overlaps(b.item[i], b.item[j]))
                    L = (uint8_t)(b.item[j].layer + 1);
            }
            b.item[i].layer = L;
            if (L + 1 > layers) layers = (uint8_t)(L + 1);
        }
    } else {
        for (uint8_t i = 0; i < b.n; i++) b.item[i].layer = 0;
    }
    b.stats.layers = layers;

    /* ---- 2. order by (layer, block): insertion sort, n <= 64 ------------ */
    for (uint8_t i = 0; i < b.n; i++) {
        const uint8_t v = i;
        const uint32_t k = sortKey(b.item[v]);
        uint8_t j = i;
        while (j && sortKey(b.item[b.order[j - 1]]) > k) {
            b.order[j] = b.order[j - 1];
            j--;
        }
        b.order[j] = v;
    }

    /* Both steps below write gfx_fb and borrow RPGfx's chunk buffers. */
    gfx_wait();
    uint8_t *buf0 = gfx_chunkScratch();
    uint8_t *buf1 = buf0 + GFX_CHUNK_BYTES;
    Sd2Card &card = SD.rawCard();
    int result = SPRITE_OK;

    /* ---- 3. one run per stretch of nearby blocks within a layer --------- */
    uint8_t i = 0;
    while (i < b.n) {
        const SpriteBatchItem &first = b.item[b.order[i]];
        const uint8_t  layer = first.layer;
        const uint32_t start = first.block;
        uint32_t last = start;
        uint8_t  j = (uint8_t)(i + 1);
        while (j < b.n) {
            const SpriteBatchItem &it = b.item[b.order[j]];
            if (it.layer != layer) break;
            if (it.block > last + 1 + SPRITE_BATCH_GAP_BLOCKS) break;
            last = it.block;       /* equal blocks just share the read */
            j++;
        }

        RunCtx c;
        c.b     = &b;
        c.cur   = i;
        c.end   = j;
        c.blk   = start;
        c.dirty = dirty;
        const uint16_t count = (uint16_t)(last - start + 1);
        if (!card.readBlocksPipelined(start, count, buf0, buf1, onBlock, &c))
            result = SPRITE_ERR_IO;
        b.stats.reads++;
        b.stats.blocks += count;
        i = j;
    }
    return result;
}
