#include "game/explosion.h"
#include "game/player.h"
#include "port/src/world_lighting.h"

#include <stdio.h>
#include <string.h>

struct player *g_CurrentPlayer;
struct Explosion *g_ExplosionBuffer;
s32 g_NumExplosionEntries;

static Mtxf g_camera;
static int g_point_light_count;

Mtxf *camGetWorldToScreenMtxf(void)
{
    return &g_camera;
}

int gfx_world_lighting_add_point_light(float x, float y, float z,
                                       float radius, float intensity,
                                       float r, float g, float b,
                                       int priority)
{
    (void)x;
    (void)y;
    (void)z;
    (void)radius;
    (void)intensity;
    (void)r;
    (void)g;
    (void)b;
    (void)priority;
    ++g_point_light_count;
    return 1;
}

static void reset_fixture(struct player *player,
                          struct Explosion explosions[EXPLOSION_BUFFER_LEN])
{
    memset(player, 0, sizeof(*player));
    memset(explosions, 0,
           sizeof(struct Explosion) * EXPLOSION_BUFFER_LEN);
    memset(&g_camera, 0, sizeof(g_camera));
    g_camera.m[0][0] = 1.0f;
    g_camera.m[1][1] = 1.0f;
    g_camera.m[2][2] = 1.0f;
    g_camera.m[3][3] = 1.0f;
    g_CurrentPlayer = player;
    g_ExplosionBuffer = explosions;
    g_NumExplosionEntries = 0;
    g_point_light_count = 0;
}

int main(void)
{
    struct player player;
    struct Explosion explosions[EXPLOSION_BUFFER_LEN];

    reset_fixture(&player, explosions);

    /* g_NumExplosionEntries is a screen-shake countdown, not an active-slot
     * count. An active explosion must still light the world after that
     * countdown reaches zero. */
    explosions[EXPLOSION_BUFFER_LEN - 1].prop = (PropRecord *)1;
    explosions[EXPLOSION_BUFFER_LEN - 1].parts[0].frame = 1;
    explosions[EXPLOSION_BUFFER_LEN - 1].parts[0].size = 120.0f;
    portWorldLightingSubmitEffects();
    if (g_point_light_count != 1) {
        fprintf(stderr,
                "active explosion in a later slot was not submitted: %d\n",
                g_point_light_count);
        return 1;
    }

    /* A positive legacy countdown must not truncate the fixed-size buffer. */
    reset_fixture(&player, explosions);
    g_NumExplosionEntries = 1;
    explosions[EXPLOSION_BUFFER_LEN - 2].prop = (PropRecord *)1;
    explosions[EXPLOSION_BUFFER_LEN - 2].parts[0].frame = 1;
    explosions[EXPLOSION_BUFFER_LEN - 2].parts[0].size = 80.0f;
    portWorldLightingSubmitEffects();
    if (g_point_light_count != 1) {
        fprintf(stderr,
                "active explosion was incorrectly limited by countdown: %d\n",
                g_point_light_count);
        return 1;
    }

    /* Empty slots remain inert even when the adapter scans the complete
     * fixed-size buffer. */
    reset_fixture(&player, explosions);
    explosions[1].prop = (PropRecord *)1;
    explosions[1].parts[0].frame = 1;
    explosions[1].parts[0].size = 50.0f;
    explosions[3].prop = (PropRecord *)1;
    explosions[3].parts[0].frame = 1;
    explosions[3].parts[0].size = 50.0f;
    portWorldLightingSubmitEffects();
    if (g_point_light_count != 2) {
        fprintf(stderr, "active explosion slot filtering failed: %d\n",
                g_point_light_count);
        return 1;
    }

    return 0;
}
