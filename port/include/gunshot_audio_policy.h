#ifndef GE007_GUNSHOT_AUDIO_POLICY_H
#define GE007_GUNSHOT_AUDIO_POLICY_H

#include <stdint.h>

/* Port-side extraction of gunfire.c's sound-trigger decision. A positive
 * trigger rate uses the per-hand deadline; a zero rate follows the actual
 * weapon-fire edge. Deadlines are inclusive: a shot due on this tick must be
 * emitted before the next interval is scheduled. */
static inline int geGunshotSoundIsDue(uint32_t now,
                                      uint32_t next_due,
                                      uint32_t other_hand_last_sound,
                                      uint32_t trigger_rate,
                                      int weapon_firing)
{
    if (trigger_rate > 0)
    {
        return other_hand_last_sound != now && next_due <= now;
    }
    return weapon_firing != 0;
}

#endif /* GE007_GUNSHOT_AUDIO_POLICY_H */
