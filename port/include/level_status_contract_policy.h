#ifndef GE007_LEVEL_STATUS_CONTRACT_POLICY_H
#define GE007_LEVEL_STATUS_CONTRACT_POLICY_H

#include <stddef.h>

/* These small predicates are shared by the guarded PORT call sites and their
 * contract tests. They keep malformed converted level data fail-closed at
 * the dereference boundary. */

static inline int geTileWalkResultCurrentlyAccepted(int walk_result,
                                                    const void *tile)
{
    return walk_result > 0 && tile != NULL;
}

static inline int gePortalPointsCurrentlyAccepted(const void *points,
                                                  unsigned point_count)
{
    return points != NULL && point_count >= 3;
}

static inline int geModelLoadCurrentlyAccepted(int model_id,
                                               size_t table_count,
                                               const void *header)
{
    return model_id >= 0 && (size_t)model_id < table_count && header != NULL;
}

static inline int geSoundSetupCurrentlyAccepted(const void *sound,
                                                const void *keymap,
                                                const void *envelope)
{
    return sound != NULL && keymap != NULL && envelope != NULL;
}

#endif /* GE007_LEVEL_STATUS_CONTRACT_POLICY_H */
