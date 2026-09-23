#ifndef _VIEWPORT_H_
#define _VIEWPORT_H_

#include <ultra64.h>

#include <PR/gbi.h>

Gfx *zbufClearCurrentPlayer(Gfx *gdl);
Gfx *zbufInit(Gfx *gdl);
#ifdef PORT
void zbufSetBuffer(u8 *buffer, s32 width, s32 height);
#else
void zbufSetBuffer(s32 buffer, s32 width, s32 height);
#endif

void zbufDeallocate(void);

#endif
