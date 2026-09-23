#ifndef _LVL_TEXT_H_
#define _LVL_TEXT_H_
#include <ultra64.h>
#include "bondconstants.h"

struct jpncharpixels {
	/*0x00*/ u32 unk00;
	/*0x04*/ u32 unk04;
	/*0x08*/ u32 unk08;
};

struct jpncacheitem {
	u16 ttl : 2; //cannonically timeleft
	u16 codepoint : 14;
};

/* The Japanese glyph cache is a fixed 124-entry table.  A cache entry
 * reserves eight jpncharpixels records, while the serialized ROM copy sizes
 * differ between single-byte and multibyte glyphs. */
enum
{
    LANGUAGE_BANK_COUNT = 45,
    JPN_CACHE_ENTRY_COUNT = 0x7c,
	JPN_CACHE_PIXEL_STRIDE = 8,
	JPN_CACHE_PIXEL_BYTES = 0x2e80,
	JPN_CACHE_ITEM_STORAGE_BYTES = 0x100,
	JPN_CODEPOINT_SHIFT = 1,
	JPN_MULTIBYTE_FLAG = 0x2000,
	JPN_MULTIBYTE_CODEPOINT_MASK = 0x1fff,
	JPN_SINGLE_GLYPH_ROM_BYTES = 0x60,
	JPN_MULTIBYTE_GLYPH_ROM_BYTES = 0x80,
	LANG_BANK_LOAD_SIZE_HINT = 0x100
};


extern s32 j_text_trigger;

void langLoadToAddr(u32 id);
void langTick(void);
u8 * langGet(s32 slotID);
void langLoadToBank(int id, u8 *target, int size);
LEVELID langGetLangBankIndexFromStagenum(LEVELID level);
struct jpncharpixels *langGetJpnCharPixels(s32 codepoint);

#endif
