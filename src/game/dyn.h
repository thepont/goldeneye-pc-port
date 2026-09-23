#ifndef _DYN_H_
#define _DYN_H_

#include <ultra64.h>

/* Dynamic graphics memory follows the N64's four player-count presets. Each
 * pool is double-buffered; the third pointer is the end marker for the
 * inactive half. */
enum
{
    DYN_PLAYER_COUNT_BUCKETS = 4,
    DYN_BUFFER_SLOT_COUNT = 3,
    DYN_DOUBLE_BUFFER_COUNT = 2,
    DYN_N64_GFX_COMMAND_BYTES = 8,
    DYN_BYTES_PER_KIBIBYTE = 1024,
    DYN_DEFAULT_BUFFER_BYTES_1P = 0x10000,
    DYN_DEFAULT_BUFFER_BYTES_2P = 0x18000,
    DYN_DEFAULT_BUFFER_BYTES_3P = 0x20000,
    DYN_DEFAULT_BUFFER_BYTES_4P = 0x28000
};

void dynInit(void);
void dynInitMemory(void);
Gfx *dynGetMasterDisplayList(void);
s32 dynGetFreeGfx2(Gfx *gdl);
Vtx *dynAllocateVertices(s32 count);
Mtx *dynAllocateMatrix(void);
Light *dynAllocateLights(s32 count);
void *dynAllocate(s32 size);
void dynSwapBuffers(void);
void dynRemovedFunc(Gfx *gdl);
s32 dynGetFreeGfx(Gfx *gdl);
s32 dynGetFreeVtx(void);
void dynDrawMembars(Gfx *gdl);

#endif
