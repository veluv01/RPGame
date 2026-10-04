#pragma once
/*
 * StlRender - draw a binary STL as a wireframe into RPGfx's 4 bpp framebuffer,
 *             streaming the file past it one triangle at a time.
 *
 * ---------------------------------------------------------------------------
 * WHY STREAMING
 * ---------------------------------------------------------------------------
 * The CPU is not the limit here, RAM is. Once RPGfx's 8 KB framebuffer, its
 * LUT and DMA buffers and the SD library are in, about 2 KB is left, which
 * holds maybe 150 deduplicated triangles. So nothing about the mesh is kept:
 * every frame re-reads the file from the card (one CMD18, DMA, ~3 MB/s) and
 * each 50-byte record is transformed and drawn as it goes past. Model size is
 * then limited by the card, not by memory, and frame time scales with the
 * triangle count.
 *
 * ---------------------------------------------------------------------------
 * NO FLOATING POINT
 * ---------------------------------------------------------------------------
 * The CH32X035 has no FPU, and a soft-float multiply is ~60+ cycles. STL
 * coordinates are IEEE-754 floats, but converting one to a fixed-point
 * integer is just "take the 24-bit mantissa and shift it by the exponent",
 * about 10 integer instructions. A load-time scan finds the bounding box and
 * the largest exponent, which fixes one shift that maps every coordinate of
 * this model into +-2^29 without overflow. After that, each vertex is:
 *
 *      3 x float->int    (shift)
 *      9 multiplies      (rotation, with the fit-to-screen scale folded in)
 *      1 divide          (perspective)
 *
 * ---------------------------------------------------------------------------
 * DRAWING EACH EDGE ONCE
 * ---------------------------------------------------------------------------
 * In a closed, consistently wound mesh every edge belongs to two triangles
 * and appears in them in opposite directions: A->B in one, B->A in the
 * other. So "draw A->B only if key(A) < key(B)", for any fixed ordering of
 * vertices, draws every edge exactly once with no adjacency information -
 * half the line work for free. The ordering used is the raw float bits.
 *
 * It is only exact for closed, consistently wound meshes, so the load-time
 * scan checks for that: every directed edge adds +hash(A,B) or -hash(A,B)
 * depending on its direction, and a mesh where every edge is matched by its
 * reverse sums to exactly zero. Anything else (open meshes, flipped faces)
 * falls back to drawing every edge.
 *
 * ---------------------------------------------------------------------------
 * DEPTH CUEING AND A 4-BIT Z-BUFFER FOR FREE
 * ---------------------------------------------------------------------------
 * Each edge is shaded by depth, nearer = brighter, using palette indices
 * STL_SHADE0 .. STL_SHADE0 + STL_SHADES - 1 in brightness order. Pixels are
 * written only if the new index is larger than the one already there, so
 * where two wires cross the nearer one wins regardless of file order.
 */
#include <stdint.h>

/* ------------------------------------------------------------------------ */
/* Framebuffer                                                               */
/* ------------------------------------------------------------------------ */
/* RPGfx's layout: 4 bpp, 2 px per byte, even x in the LOW nibble. */
#ifndef STL_W
#define STL_W 128
#endif
#ifndef STL_H
#define STL_H 128
#endif
#define STL_STRIDE (STL_W / 2)

/* Palette indices the renderer paints wires with, dimmest first. The
 * max-blend relies on brighter shades having larger indices, and on the
 * background (index 0) being below all of them. */
#ifndef STL_SHADE0
#define STL_SHADE0 8
#endif
#ifndef STL_SHADES
#define STL_SHADES 8
#endif

/* ------------------------------------------------------------------------ */
/* File layout                                                               */
/* ------------------------------------------------------------------------ */
/*   80 bytes   header (free text; some exporters start it with "solid")
 *    4 bytes   triangle count, little-endian
 *   50 bytes   per triangle: normal (3 floats), 3 vertices (9 floats),
 *              2-byte attribute
 * The normal is ignored: facing comes from the projected winding instead. */
#define STL_HEADER_BYTES 84
#define STL_RECORD_BYTES 50

typedef void (*StlRecordFn)(const uint8_t *rec);

/* ------------------------------------------------------------------------ */
/* Record assembler                                                          */
/* ------------------------------------------------------------------------ */
/* Records are 50 bytes and blocks are 512, so records straddle blocks. Feed
 * the file in order and whole records come out; a straddling record is
 * reassembled in rec[]. Jumping forward (fileOff != where the last chunk
 * ended) is allowed: it drops the partial record and resyncs on the next
 * record boundary, which is what draft rendering relies on. */
struct StlFeed {
    uint32_t pos;          /* file offset the next chunk is expected at   */
    uint32_t end;          /* file offset just past the last record       */
    uint8_t  have;         /* bytes of a straddling record held in rec[]  */
    uint8_t  rec[STL_RECORD_BYTES] __attribute__((aligned(4)));
};

void stlFeedBegin(StlFeed &f, uint32_t tris);
void stlFeedBytes(StlFeed &f, uint32_t fileOff, const uint8_t *p, uint32_t n,
                  StlRecordFn fn);

/* ------------------------------------------------------------------------ */
/* Pass 1: scan (once, when a file is opened)                               */
/* ------------------------------------------------------------------------ */
struct StlModel {
    uint32_t tris;         /* records scanned                             */
    uint32_t badTris;      /* records containing NaN or infinity          */
    int32_t  c[3];         /* bounding-box centre, fixed point            */
    int32_t  k;            /* Q14 scale: bounding radius -> 2^14          */
    int16_t  sh0;          /* added to a float's exponent to convert it  */
    bool     closed;       /* every edge matched by its reverse           */
    uint32_t sizeMilli[3]; /* bounding box size, file units x 1000        */
    int16_t  box[3];       /* box half-size, bounding radius = 2^14       */
};

/* The scan also samples up to maxSamples vertices spread evenly through the
 * file, for the draft view. sampleBuf is scratch for their raw floats (12
 * bytes each) until stlScanEnd() packs them into pts (3 bytes each). Pass
 * nullptr to skip sampling. */
void stlScanBegin(uint32_t tris, uint32_t *sampleBuf, uint16_t maxSamples);
void stlScanRecord(const uint8_t *rec);     /* an StlRecordFn */
bool stlScanEnd(StlModel &m, int8_t (*pts)[3], uint16_t *nPts);  /* false: nothing drawable */

/* ------------------------------------------------------------------------ */
/* Pass 2: draw (every frame)                                                */
/* ------------------------------------------------------------------------ */
enum : uint8_t {
    STL_XRAY  = 0,         /* every edge, depth cued                      */
    STL_FRONT = 1,         /* only edges of triangles facing the camera   */
    STL_MODES
};

/* Angles are 16-bit: 65536 = one full turn. */
struct StlView {
    uint16_t yaw;          /* turntable spin about the model's Z (up) axis */
    uint16_t pitch;        /* tilt towards the camera; + looks from above   */
    uint16_t zoomQ8;       /* 256 = bounding sphere just fits the screen    */
    uint8_t  mode;         /* STL_XRAY / STL_FRONT                          */
    bool     ortho;        /* orthographic instead of perspective           */
    bool     dedup;        /* draw shared edges once (needs a closed mesh)  */
    int16_t  cx, cy;       /* screen position of the model centre           */
};

#define STL_ZOOM_MIN   64          /* x0.25 */
#define STL_ZOOM_MAX   4096        /* x16   */

/* What a frame drew. The box is inclusive pixel coordinates and empty when
 * x1 < x0. */
struct StlFrameInfo {
    int16_t  x0, y0, x1, y1;
    uint32_t tris;         /* records processed                           */
    uint32_t edges;        /* lines that reached the screen               */
    uint32_t pixels;       /* pixels those lines covered                  */
};

void stlFrameBegin(const StlModel &m, const StlView &v);
void stlDrawRecord(const uint8_t *rec);     /* an StlRecordFn */
void stlFrameEnd(StlFrameInfo &out);
/* The same, part-way through a frame, without ending it. */
void stlFramePeek(StlFrameInfo &out);

/* Instead of streaming the file: draw the bounding box and the points the
 * scan sampled. No card access at all, so this is what the viewer shows
 * while a model too big to stream at a usable frame rate is being turned.
 * Call between stlFrameBegin() and stlFrameEnd(). */
void stlDrawDraft(const StlModel &m, const int8_t (*pts)[3], uint16_t n);

/* The current view's rotation (no scale), Q14. Row 0 is screen x, row 1
 * screen y (down), row 2 depth (away). For drawing an axis gizmo. */
void stlRotation(int32_t out[3][3]);

/* Fixed-point sine/cosine of a 16-bit angle, Q14. */
int32_t stlSin(uint16_t a);
int32_t stlCos(uint16_t a);

/* Plot a line into the framebuffer with clipping, overwriting (no blend). */
void stlLine(int x0, int y0, int x1, int y1, uint8_t c);
