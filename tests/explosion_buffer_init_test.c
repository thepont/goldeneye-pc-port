#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "bondconstants.h"
#include "explosion.h"

void alloc_explosion_smoke_casing_scorch_impact_buffers(void);

struct Smoke *g_SmokeBuffer;
struct Explosion *g_ExplosionBuffer;
struct Scorch *g_ScorchBuffer;
struct BulletImpact *g_BulletImpactBuffer;
struct FlyingParticles *g_FlyingParticlesBuffer;
s32 g_NumExplosionEntries;
s32 g_NumSmokeEntries;
s32 g_NumParticleEntries;
s32 g_NumScorchEntries;
s32 g_NumImpactEntries;
s32 max_particles;
f32 g_SpExplosionDamageMult;

static unsigned char arena[1024 * 1024];
static size_t arena_offset;
static int configured_players;
static s32 configured_stage;
static int allocation_count;
static u32 allocation_sizes[8];
static int failures;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

void *mempAllocBytesInBank(u32 bytes, u8 bank)
{
    size_t aligned_offset = (arena_offset + 15u) & ~15u;
    (void)bank;
    allocation_sizes[allocation_count++] = bytes;
    if (aligned_offset + bytes > sizeof(arena))
    {
        return NULL;
    }
    arena_offset = aligned_offset + bytes;
    return &arena[aligned_offset];
}

s32 getPlayerCount(void)
{
    return configured_players;
}

s32 lvlGetCurrentStageToLoad(void)
{
    return configured_stage;
}

static void reset_state(void)
{
    memset(arena, 0xA5, sizeof(arena));
    arena_offset = 0;
    allocation_count = 0;
    memset(allocation_sizes, 0, sizeof(allocation_sizes));
    g_ExplosionBuffer = NULL;
    g_SmokeBuffer = NULL;
    g_ScorchBuffer = NULL;
    g_BulletImpactBuffer = NULL;
    g_FlyingParticlesBuffer = NULL;
}

static void test_solo_buffer_initialization(void)
{
    s32 i;
    s32 j;

    reset_state();
    configured_players = 1;
    configured_stage = LEVELID_DAM;
    alloc_explosion_smoke_casing_scorch_impact_buffers();

    expect(g_NumExplosionEntries == 0 && g_NumSmokeEntries == 0 &&
               g_NumParticleEntries == 0 && g_NumScorchEntries == 0 &&
               g_NumImpactEntries == 0 && g_SpExplosionDamageMult == 1.0f,
           "buffer initialization resets counters and damage scale");
    expect(allocation_count == 5 && g_ExplosionBuffer != NULL &&
               g_SmokeBuffer != NULL && g_ScorchBuffer != NULL &&
               g_BulletImpactBuffer != NULL && g_FlyingParticlesBuffer != NULL,
           "solo initialization allocates every buffer including scorches");
    expect(max_particles == MAX_FLYING_PARTICLES,
           "solo normal-stage initialization keeps the full particle budget");

    for (i = 0; i < EXPLOSION_BUFFER_LEN; ++i)
    {
        expect(g_ExplosionBuffer[i].prop == NULL,
               "explosion entries start without props");
        for (j = 0; j < EXPLOSION_PARTS_LEN; ++j)
        {
            expect(g_ExplosionBuffer[i].parts[j].frame == 0,
                   "explosion parts start at frame zero");
        }
    }
    for (i = 0; i < SMOKE_BUFFER_LEN; ++i)
    {
        expect(g_SmokeBuffer[i].prop == NULL,
               "smoke entries start without props");
        for (j = 0; j < SMOKE_PARTS_LEN; ++j)
        {
            expect(g_SmokeBuffer[i].parts[j].size == 0.0f,
                   "smoke parts start with zero size");
        }
    }
    for (i = 0; i < SCORCH_BUFFER_LEN; ++i)
    {
        expect(g_ScorchBuffer[i].roomid == -1,
               "scorch entries start with no room");
    }
    for (i = 0; i < BULLET_IMPACT_BUFFER_LEN; ++i)
    {
        expect(g_BulletImpactBuffer[i].room == -1,
               "bullet-impact entries start with no room");
    }
    for (i = 0; i < MAX_FLYING_PARTICLES; ++i)
    {
        expect(g_FlyingParticlesBuffer[i].unk00 == 0,
               "flying-particle entries start inactive");
    }
}

static void test_multiplayer_stage_budget(void)
{
    reset_state();
    configured_players = 2;
    configured_stage = LEVELID_STREETS;
    g_ScorchBuffer = (struct Scorch *)(uintptr_t)0x1;

    alloc_explosion_smoke_casing_scorch_impact_buffers();

    expect(allocation_count == 4,
           "multiplayer initialization does not allocate solo scorch storage");
    expect(max_particles == MAX_FLYING_PARTICLES / 4,
           "streets multiplayer initialization applies both budget reductions");
    expect(g_ScorchBuffer == NULL,
           "multiplayer initialization clears stale solo scorch storage");
}

int main(void)
{
    test_solo_buffer_initialization();
    test_multiplayer_stage_budget();

    if (failures != 0)
    {
        fprintf(stderr, "%d explosion-buffer contract(s) failed\n", failures);
        return 1;
    }

    puts("explosion-buffer initialization suite passed");
    return 0;
}
