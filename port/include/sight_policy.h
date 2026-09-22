#ifndef GE007_SIGHT_POLICY_H
#define GE007_SIGHT_POLICY_H

/* These bits are the stable gunsight-mode contract used by gunfire.c. Keep
 * the policy independent from the renderer so it can be checked without
 * constructing a player or a display list. */
#define GE_SIGHT_REASON_NOTAIMING 0x02

static inline int geSightIsAiming(int gunsight_mode)
{
    return (gunsight_mode & GE_SIGHT_REASON_NOTAIMING) == 0;
}

static inline int geSightShouldDraw(int gunsight_mode, int multiplayer_menu)
{
    return gunsight_mode == 0 && !multiplayer_menu;
}

#endif /* GE007_SIGHT_POLICY_H */
