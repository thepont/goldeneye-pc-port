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

struct SubmittedLight {
    float position[3];
    float radius;
    float intensity;
    float color[3];
    int priority;
};

static struct SubmittedLight g_submitted_lights[32];

Mtxf *camGetWorldToScreenMtxf(void)
{
    return &g_camera;
}

int gfx_world_lighting_add_point_light(float x, float y, float z,
                                       float radius, float intensity,
                                       float r, float g, float b,
                                       int priority)
{
    if (g_point_light_count <
        (int)(sizeof(g_submitted_lights) / sizeof(g_submitted_lights[0]))) {
        struct SubmittedLight *light =
            &g_submitted_lights[g_point_light_count];
        light->position[0] = x;
        light->position[1] = y;
        light->position[2] = z;
        light->radius = radius;
        light->intensity = intensity;
        light->color[0] = r;
        light->color[1] = g;
        light->color[2] = b;
        light->priority = priority;
    }
    ++g_point_light_count;
    return 1;
}

static int near(float actual, float expected)
{
    return __builtin_fabsf(actual - expected) <= 0.001f;
}

static int submitted_light_matches(int index, float x, float y, float z,
                                   float radius, float intensity, float r,
                                   float g, float b, int priority)
{
    if (index < 0 || index >= g_point_light_count ||
        index >= (int)(sizeof(g_submitted_lights) /
                       sizeof(g_submitted_lights[0]))) {
        return 0;
    }
    const struct SubmittedLight *light = &g_submitted_lights[index];
    return near(light->position[0], x) && near(light->position[1], y) &&
           near(light->position[2], z) && near(light->radius, radius) &&
           near(light->intensity, intensity) && near(light->color[0], r) &&
           near(light->color[1], g) && near(light->color[2], b) &&
           light->priority == priority;
}

static void reset_fixture(struct player *player,
                          struct Explosion explosions[EXPLOSION_BUFFER_LEN])
{
    memset(player, 0, sizeof(*player));
    memset(explosions, 0,
           sizeof(struct Explosion) * EXPLOSION_BUFFER_LEN);
    memset(&g_camera, 0, sizeof(g_camera));
    memset(g_submitted_lights, 0, sizeof(g_submitted_lights));
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
    explosions[EXPLOSION_BUFFER_LEN - 1].age = 60;
    explosions[EXPLOSION_BUFFER_LEN - 1].pos.x = 3.0f;
    explosions[EXPLOSION_BUFFER_LEN - 1].pos.y = 4.0f;
    explosions[EXPLOSION_BUFFER_LEN - 1].pos.z = 5.0f;
    g_camera.m[3][0] = 10.0f;
    g_camera.m[3][1] = -2.0f;
    g_camera.m[3][2] = 1.0f;
    portWorldLightingSubmitEffects();
    if (g_point_light_count != 1 ||
        !submitted_light_matches(0, 13.0f, 2.0f, 6.0f, 420.0f,
                                 1.755f, 1.0f, 0.456f, 0.025f, 100)) {
        fprintf(stderr,
                "active explosion light profile or transformed position is wrong: %d\n",
                g_point_light_count);
        return 1;
    }

    /* A positive legacy countdown must not truncate the fixed-size buffer. */
    reset_fixture(&player, explosions);
    g_NumExplosionEntries = 1;
    explosions[EXPLOSION_BUFFER_LEN - 2].prop = (PropRecord *)1;
    explosions[EXPLOSION_BUFFER_LEN - 2].parts[0].frame = 1;
    explosions[EXPLOSION_BUFFER_LEN - 2].parts[0].size = 80.0f;
    explosions[EXPLOSION_BUFFER_LEN - 2].pos.x = -3.0f;
    explosions[EXPLOSION_BUFFER_LEN - 2].pos.y = 1.0f;
    explosions[EXPLOSION_BUFFER_LEN - 2].pos.z = 7.0f;
    portWorldLightingSubmitEffects();
    if (g_point_light_count != 1 ||
        !submitted_light_matches(0, -3.0f, 1.0f, 7.0f, 340.0f,
                                 2.6f, 1.0f, 0.56f, 0.025f, 100)) {
        fprintf(stderr,
                "countdown-independent explosion light data is wrong: %d\n",
                g_point_light_count);
        return 1;
    }

    /* Empty slots remain inert even when the adapter scans the complete
     * fixed-size buffer. */
    reset_fixture(&player, explosions);
    explosions[1].prop = (PropRecord *)1;
    explosions[1].parts[0].frame = 1;
    explosions[1].parts[0].size = 50.0f;
    explosions[1].pos.x = -2.0f;
    explosions[1].pos.y = 0.0f;
    explosions[1].pos.z = 1.0f;
    explosions[3].prop = (PropRecord *)1;
    explosions[3].parts[0].frame = 1;
    explosions[3].parts[0].size = 50.0f;
    explosions[3].pos.x = 4.0f;
    explosions[3].pos.y = 5.0f;
    explosions[3].pos.z = 6.0f;
    portWorldLightingSubmitEffects();
    if (g_point_light_count != 2 ||
        !submitted_light_matches(0, -2.0f, 0.0f, 1.0f, 280.0f,
                                 2.6f, 1.0f, 0.56f, 0.025f, 100) ||
        !submitted_light_matches(1, 4.0f, 5.0f, 6.0f, 280.0f,
                                 2.6f, 1.0f, 0.56f, 0.025f, 100)) {
        fprintf(stderr, "active explosion slots or light data failed: %d\n",
                g_point_light_count);
        return 1;
    }

    reset_fixture(&player, explosions);
    player.hands[0].field_87D = 1;
    player.hands[0].field_B58.x = 2.0f;
    player.hands[0].field_B58.y = 3.0f;
    player.hands[0].field_B58.z = 4.0f;
    portWorldLightingSubmitEffects();
    if (g_point_light_count != 1 ||
        !submitted_light_matches(0, 2.0f, 3.0f, 4.0f, 240.0f,
                                 2.5f, 1.0f, 0.35f, 0.06f, 90)) {
        fprintf(stderr, "muzzle flash light profile or position is wrong: %d\n",
                g_point_light_count);
        return 1;
    }

    return 0;
}
