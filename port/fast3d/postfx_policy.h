#ifndef GE007_POSTFX_POLICY_H
#define GE007_POSTFX_POLICY_H

/* Pure presentation policy shared by the OpenGL adapter and contract tests.
 * Keeping the clamp here prevents config, environment overrides, and shader
 * submission from growing subtly different strength rules. */
static inline int gePostFxClampSharpen(int amount)
{
    if (amount < 0) {
        return 0;
    }
    if (amount > 100) {
        return 100;
    }
    return amount;
}

static inline int gePostFxShouldApply(int enabled, int amount)
{
    return enabled != 0 && gePostFxClampSharpen(amount) > 0;
}

#endif /* GE007_POSTFX_POLICY_H */
