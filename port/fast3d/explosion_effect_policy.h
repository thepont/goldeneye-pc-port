#ifndef GE007_EXPLOSION_EFFECT_POLICY_H
#define GE007_EXPLOSION_EFFECT_POLICY_H

#include <stdint.h>

/*
 * Pure presentation policy shared by the game-side effect adapter and tests.
 * Render mode 0 remains the original single billboard. Higher PC modes add
 * two deterministic oblique cards around that original card.
 */
typedef struct GeExplosionPresentationLayer {
    float scale;
    float yaw;
    float pitch;
    uint8_t alpha;
} GeExplosionPresentationLayer;

static inline int geExplosionPresentationExtraLayerCount(int render_mode)
{
    return render_mode >= 1 ? 2 : 0;
}

static inline GeExplosionPresentationLayer geExplosionPresentationExtraLayer(
    int layer_index)
{
    GeExplosionPresentationLayer layer = {0.0f, 0.0f, 0.0f, 0};

    switch (layer_index) {
    case 0:
        layer.scale = 0.90f;
        layer.yaw = 0.55f;
        layer.alpha = 152;
        break;
    case 1:
        layer.scale = 0.82f;
        layer.pitch = 0.55f;
        layer.alpha = 112;
        break;
    default:
        break;
    }

    return layer;
}

#endif
