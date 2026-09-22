#include "world_lighting.h"

#include "../fast3d/gfx_world_lighting_api.h"
#include "../fast3d/world_lighting_policy.h"
#include "game/explosion.h"
#include "game/player.h"

static int worldLightingBoundCount(s32 count, s32 capacity)
{
    if (count <= 0) {
        return 0;
    }
    return count < capacity ? count : capacity;
}

static int worldLightingWorldToView(const coord3d *world, float view[3])
{
    Mtxf *camera;

    if (world == NULL || g_CurrentPlayer == NULL) {
        return 0;
    }

    camera = camGetWorldToScreenMtxf();
    if (camera == NULL) {
        return 0;
    }

    /* Mtxf uses the same row-vector convention as the software RSP path. */
    view[0] = (world->x * camera->m[0][0]) +
              (world->y * camera->m[1][0]) +
              (world->z * camera->m[2][0]) + camera->m[3][0];
    view[1] = (world->x * camera->m[0][1]) +
              (world->y * camera->m[1][1]) +
              (world->z * camera->m[2][1]) + camera->m[3][1];
    view[2] = (world->x * camera->m[0][2]) +
              (world->y * camera->m[1][2]) +
              (world->z * camera->m[2][2]) + camera->m[3][2];

    return geWorldLightingIsFinite(view[0]) &&
           geWorldLightingIsFinite(view[1]) &&
           geWorldLightingIsFinite(view[2]);
}

static void worldLightingSubmitPoint(const coord3d *world,
                                     GeWorldLightProfile profile)
{
    float view[3];

    if (!worldLightingWorldToView(world, view)) {
        return;
    }

    gfx_world_lighting_add_point_light(view[0], view[1], view[2],
                                       profile.radius, profile.intensity,
                                       profile.color[0], profile.color[1],
                                       profile.color[2], profile.priority);
}

static float worldLightingExplosionVisualSize(const struct Explosion *explosion)
{
    float visual_size = 0.0f;
    int part_index;

    for (part_index = 0; part_index < EXPLOSION_PARTS_LEN; ++part_index) {
        const struct ExplosionPart *part = &explosion->parts[part_index];
        if (part->frame > 0 && geWorldLightingIsFinite(part->size)) {
            const float size = part->size < 0.0f ? -part->size : part->size;
            if (size > visual_size) {
                visual_size = size;
            }
        }
    }

    return visual_size;
}

static void worldLightingSubmitExplosions(void)
{
    const int count = worldLightingBoundCount(g_NumExplosionEntries,
                                              EXPLOSION_BUFFER_LEN);
    int index;

    if (g_ExplosionBuffer == NULL) {
        return;
    }

    for (index = 0; index < count; ++index) {
        const struct Explosion *explosion = &g_ExplosionBuffer[index];
        if (explosion->prop == NULL) {
            continue;
        }
        const GeWorldLightProfile profile = geWorldExplosionLightProfile(
            worldLightingExplosionVisualSize(explosion),
            (float)explosion->age);
        worldLightingSubmitPoint(&explosion->pos, profile);
    }
}

static void worldLightingSubmitMuzzleFlashes(void)
{
    const GeWorldLightProfile profile = geWorldMuzzleFlashLightProfile();
    int hand_index;

    if (g_CurrentPlayer == NULL) {
        return;
    }

    for (hand_index = 0; hand_index < 2; ++hand_index) {
        const struct hand *hand = &g_CurrentPlayer->hands[hand_index];
        if (hand->field_87D != 0) {
            worldLightingSubmitPoint(&hand->field_B58, profile);
        }
    }
}

void portWorldLightingSubmitEffects(void)
{
    if (g_CurrentPlayer == NULL) {
        return;
    }

    worldLightingSubmitExplosions();
    worldLightingSubmitMuzzleFlashes();
}
