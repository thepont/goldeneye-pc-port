#include <stdio.h>
#include <string.h>

#include <fr.h>
#include "chrai.h"
#include "explosion.h"

void cleanupExplosions(void);

struct Smoke *g_SmokeBuffer;
struct Explosion *g_ExplosionBuffer;
s32 g_NumExplosionEntries;

static PropRecord *cleanup_order[EXPLOSION_BUFFER_LEN + SMOKE_BUFFER_LEN][3];
static int cleanup_order_count;
static f32 last_shake;
static int failures;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

void viShake(f32 amount)
{
    last_shake = amount;
}

void chrpropDelist(PropRecord *prop)
{
    cleanup_order[cleanup_order_count][0] = prop;
}

void chrpropDisable(PropRecord *prop)
{
    cleanup_order[cleanup_order_count][1] = prop;
}

void chrpropFree(PropRecord *prop)
{
    cleanup_order[cleanup_order_count][2] = prop;
    cleanup_order_count++;
}

static void reset_spies(void)
{
    memset(cleanup_order, 0, sizeof(cleanup_order));
    cleanup_order_count = 0;
    last_shake = -1.0f;
}

int main(void)
{
    struct Explosion explosions[EXPLOSION_BUFFER_LEN] = {};
    struct Smoke smoke[SMOKE_BUFFER_LEN] = {};
    PropRecord explosion_prop = {};
    PropRecord smoke_prop = {};

    reset_spies();
    g_NumExplosionEntries = 4;
    g_ExplosionBuffer = explosions;
    g_SmokeBuffer = smoke;
    explosions[1].prop = &explosion_prop;
    explosions[5].prop = &explosion_prop;
    smoke[2].prop = &smoke_prop;
    smoke[19].prop = &smoke_prop;

    cleanupExplosions();

    expect(g_NumExplosionEntries == 0,
           "explosion cleanup resets the active explosion count");
    expect(last_shake == 0.0f,
           "explosion cleanup clears the view shake");
    expect(cleanup_order_count == 4,
           "explosion cleanup releases every active explosion and smoke prop");
    expect(explosions[1].prop == NULL && explosions[5].prop == NULL &&
               smoke[2].prop == NULL && smoke[19].prop == NULL,
           "explosion cleanup clears released prop ownership");
    expect(cleanup_order[0][0] == &explosion_prop &&
               cleanup_order[0][1] == &explosion_prop &&
               cleanup_order[0][2] == &explosion_prop &&
               cleanup_order[2][0] == &smoke_prop &&
               cleanup_order[2][1] == &smoke_prop &&
               cleanup_order[2][2] == &smoke_prop,
           "explosion cleanup calls delist, disable, and free in order");

    reset_spies();
    g_ExplosionBuffer = NULL;
    g_SmokeBuffer = NULL;
    g_NumExplosionEntries = 2;
    cleanupExplosions();
    expect(g_NumExplosionEntries == 0 && cleanup_order_count == 0 &&
               last_shake == 0.0f,
           "explosion cleanup safely handles unallocated buffers");

    if (failures != 0)
    {
        fprintf(stderr, "%d explosion-cleanup contract(s) failed\n", failures);
        return 1;
    }

    puts("explosion-cleanup suite passed");
    return 0;
}
