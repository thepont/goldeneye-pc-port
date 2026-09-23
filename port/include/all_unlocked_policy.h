#ifndef GE007_ALL_UNLOCKED_POLICY_H
#define GE007_ALL_UNLOCKED_POLICY_H

#include <stddef.h>
#include <stdint.h>

/* The fields immediately following ge_save_slot's two CRC words. This keeps
 * the test seam tied to the actual save layout without linking the whole
 * EEPROM/libultra shim into a unit-test executable. */
typedef struct GeAllUnlockedFreshSaveFields {
    uint8_t completion_bitflags;
    uint8_t flag_007;
    uint8_t music_vol;
    uint8_t sfx_vol;
    uint16_t options;
} GeAllUnlockedFreshSaveFields;

static inline int geAllUnlockedSlotIsZero(const void *slot, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)slot;

    if (slot == NULL)
    {
        return 0;
    }
    for (size_t i = 0; i < size; ++i)
    {
        if (bytes[i] != 0)
        {
            return 0;
        }
    }
    return 1;
}

/* Seed the same blank-slot values as fileResetSave/BLANKSAVEDATA before the
 * AllUnlocked read-time patch fills completion data. Keeping the complete
 * tuple here prevents a pristine save from inheriting zero-volume audio or
 * an invalid options word. */
static inline void geAllUnlockedSeedFreshSaveDefaults(
    GeAllUnlockedFreshSaveFields *fields)
{
    if (fields == NULL)
    {
        return;
    }
    fields->completion_bitflags = 0x01;
    fields->flag_007 = 0x00;
    fields->music_vol = 0xFF;
    fields->sfx_vol = 0xFF;
    fields->options = 0x003A;
}

#endif /* GE007_ALL_UNLOCKED_POLICY_H */
