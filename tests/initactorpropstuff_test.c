#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>

#include <ultra64.h>
#include "chr.h"
#include "gun.h"
#include "initactorpropstuff.h"
#include "initanitable.h"
#include "model.h"

/*
 * This fixture deliberately puts the animation segment below 2 GiB.  The
 * original data is an N64 offset table and initactorpropstuff.c still has
 * several `(s32) pointer` rebases.  A real host-address fixture would test
 * the allocator rather than the initialization contract.
 */
static unsigned char *animation_segment;
static const size_t animation_segment_size = 0x100000;

struct ModelSkeleton skeleton_guard;
struct animation_table_data *ptr_animation_table;
unsigned char *g_pc_animdata_base;

u32 num_bodies;
u32 num_male_heads;
u32 num_female_heads;
s32 list_of_bodies[] = {10, 20, -1};
s32 random_male_heads[] = {30, 40, 50, -1};
s32 random_female_heads[] = {60, -1};

CasingRecord g_Casings[20];

static struct StruckAnim hit_death[3];
static struct StruckAnim hit_flinch[3];
struct StruckAnim death_stagger[3];

struct ChrHitReaction g_HitReactionTable[2] = {
    {
        .hitpart = 1,
        .deathAnims = hit_death,
        .flinchAnims = hit_flinch,
    },
    {
        .hitpart = -1,
    },
};

static struct anim_group_info empty_group;
struct anim_group_info *ptr_rifle_firing_animation_groups[32];
struct anim_group_info *ptr_pistol_firing_animation_groups[32];
struct anim_group_info *ptr_doubles_firing_animation_groups[32];
struct anim_group_info *ptr_crouched_rifle_firing_animation_groups[32];
struct anim_group_info *ptr_crouched_pistol_firing_animation_groups[32];
struct anim_group_info *ptr_crouched_doubles_firing_animation_groups[32];

struct weapon_firing_animation_table D_80030078[1];
struct weapon_firing_animation_table D_80030660[1];

f32 D_80030984;
f32 D_80030988;
f32 D_8003098C;
f32 D_80030990;
f32 D_80030994;
f32 D_80030998;
f32 D_8003099C;
f32 D_800309A0;
f32 D_800309A4;

static int failures;
static int animation_calls;
static int animation_frames[32];
static int use_offset_angles;

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static int close_enough(f32 actual, f32 expected)
{
    f32 difference = actual - expected;
    return difference < 0.00001f && difference > -0.00001f;
}

static void map_animation_segment(void)
{
    void *mapped;

    mapped = mmap(NULL, animation_segment_size, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (mapped == MAP_FAILED || (uintptr_t)mapped > UINT32_MAX)
    {
        fprintf(stderr, "unable to allocate a low animation fixture\n");
        exit(2);
    }

    animation_segment = (unsigned char *)mapped;
    memset(animation_segment, 0, animation_segment_size);
}

static void set_offset_animation(struct StruckAnim *entry,
                                 uintptr_t offset,
                                 s32 flip)
{
    entry->struck_anim = (ModelAnimation *)offset;
    entry->flip = flip;
}

static void reset_fixture(void)
{
    int i;

    memset(animation_segment, 0, animation_segment_size);
    ptr_animation_table = (struct animation_table_data *)animation_segment;
    g_pc_animdata_base = animation_segment;

    num_bodies = 99;
    num_male_heads = 99;
    num_female_heads = 99;

    memset(g_Casings, 0, sizeof(g_Casings));
    for (i = 0; i < 20; ++i)
    {
        g_Casings[i].header = (ModelFileHeader *)(uintptr_t)(i + 1);
    }

    set_offset_animation(&hit_death[0], 0x120, 0);
    set_offset_animation(&hit_death[1], 0x220, 1);
    hit_death[2].struck_anim = NULL;
    set_offset_animation(&hit_flinch[0], 0x320, 0);
    set_offset_animation(&hit_flinch[1], 0x420, 1);
    hit_flinch[2].struck_anim = NULL;
    set_offset_animation(&death_stagger[0], 0x520, 0);
    set_offset_animation(&death_stagger[1], 0x620, 1);
    death_stagger[2].struck_anim = NULL;

    g_HitReactionTable[0].deathAnimCount = 0;
    g_HitReactionTable[0].flinchAnimCount = 0;
    g_HitReactionTable[1].hitpart = -1;

    memset(&empty_group, 0, sizeof(empty_group));
    for (i = 0; i < 32; ++i)
    {
        ptr_rifle_firing_animation_groups[i] = &empty_group;
        ptr_pistol_firing_animation_groups[i] = &empty_group;
        ptr_doubles_firing_animation_groups[i] = &empty_group;
        ptr_crouched_rifle_firing_animation_groups[i] = &empty_group;
        ptr_crouched_pistol_firing_animation_groups[i] = &empty_group;
        ptr_crouched_doubles_firing_animation_groups[i] = &empty_group;
    }
    memset(D_80030078, 0, sizeof(D_80030078));
    memset(D_80030660, 0, sizeof(D_80030660));

    D_80030984 = -1.0f;
    D_80030988 = -1.0f;
    D_8003098C = -1.0f;
    D_80030990 = -1.0f;
    D_80030994 = -1.0f;
    D_80030998 = -1.0f;
    D_8003099C = -1.0f;
    D_800309A0 = -1.0f;
    D_800309A4 = -1.0f;

    animation_calls = 0;
    memset(animation_frames, 0, sizeof(animation_frames));
    use_offset_angles = 0;
}

u16 sub_GAME_7F06D2E4(s32 jointnum, s32 flip, ModelSkeleton *skeleton,
                      ModelAnimation *anim, s32 frame, coord16 *out)
{
    uintptr_t offset;

    (void)jointnum;
    (void)flip;
    (void)skeleton;
    if (animation_calls < (int)(sizeof(animation_frames) /
                                sizeof(animation_frames[0])))
    {
        animation_frames[animation_calls] = frame;
    }
    animation_calls++;

    if (out != NULL)
    {
        out->x = (s16)frame;
        out->y = (s16)(frame + 1);
        out->z = (s16)(frame + 2);
    }

    if (use_offset_angles && ptr_animation_table != NULL)
    {
        offset = (uintptr_t)anim - (uintptr_t)ptr_animation_table;
        if (offset == 0x300)
        {
            return 0x1000;
        }
        if (offset == 0x400)
        {
            return 0x5000;
        }
    }

    return 0x0100;
}

s32 floorFloatToInt(f32 value)
{
    return (s32)value;
}

static void test_reset_counts_sentinel_terminated_lists(void)
{
    reset_counter_rand_body_head();

    expect_true(num_bodies == 2,
                "body count stops at the first negative sentinel");
    expect_true(num_male_heads == 3,
                "male-head count stops at the first negative sentinel");
    expect_true(num_female_heads == 1,
                "female-head count stops at the first negative sentinel");
}

static void test_root_motion_helpers_cover_empty_and_nonempty_ranges(void)
{
    ModelAnimation *animation = (ModelAnimation *)(animation_segment + 0x100);
    u32 total;

    animation_calls = 0;
    total = sub_GAME_7F0001F0(animation, 2, 5);
    expect_true(total == 0x0300 && animation_calls == 3,
                "animation angle helper sums every half-open frame range");
    expect_true(animation_frames[0] == 2 && animation_frames[1] == 3 &&
                    animation_frames[2] == 4,
                "animation angle helper visits frames in source order");

    animation_calls = 0;
    expect_true(sub_GAME_7F0001F0(animation, 5, 5) == 0 &&
                    animation_calls == 0,
                "animation angle helper leaves an empty range untouched");

    animation_calls = 0;
    expect_true(sub_GAME_7F000290(animation, 4, 4) == 0 &&
                    animation_calls == 0,
                "root-motion helper does not call the decoder for equal bounds");
    expect_true(sub_GAME_7F000290(animation, 5, 3) == 0 &&
                    animation_calls == 0,
                "root-motion helper rejects a reversed range");

    animation_calls = 0;
    expect_true(sub_GAME_7F000290(animation, 0, 3) == 9 &&
                    animation_calls == 3,
                "root-motion helper sums decoded Z motion for each frame");
}

static void test_animation_table_rebase_and_group_angles(void)
{
    struct StruckAnim entries[3];
    struct weapon_firing_animation_table configs[3];
    struct weapon_firing_animation_table zero_duration[2];
    struct anim_group_info groups[32];
    struct anim_group_info *group_pointers[32];
    s32 count;
    int i;
    f32 low_angle;
    f32 high_angle;

    reset_fixture();
    entries[0].struck_anim = (ModelAnimation *)(uintptr_t)0x120;
    entries[1].struck_anim = (ModelAnimation *)(uintptr_t)0x220;
    entries[2].struck_anim = NULL;
    count = initResolveAnimTable(entries);
    expect_true(count == 2,
                "animation table resolution returns the number of entries");
    expect_true(entries[0].struck_anim ==
                    (ModelAnimation *)(animation_segment + 0x120) &&
                    entries[1].struck_anim ==
                        (ModelAnimation *)(animation_segment + 0x220),
                "animation table resolution rebases every offset once");

    memset(configs, 0, sizeof(configs));
    configs[0].anim.offset = 0x300;
    configs[0].unk04 = 3.0f;
    configs[1].anim.offset = 0x400;
    configs[1].unk04 = 2.0f;
    use_offset_angles = 1;
    count = initResolveAnimGroupTable(configs);
    expect_true(count == 2,
                "animation group resolution stops at its null animation");
    expect_true(configs[0].anim.anim ==
                    (ModelAnimation *)(animation_segment + 0x300) &&
                    configs[1].anim.anim ==
                        (ModelAnimation *)(animation_segment + 0x400),
                "animation group resolution rebases each animation pointer");
    low_angle = (0x3000 * 0.0000958738f) / 3.0f;
    high_angle = ((0xa000 * 0.0000958738f) - M_TAU_F) / 2.0f;
    expect_true(close_enough(configs[0].turn_angle_per_frame, low_angle),
                "small signed animation angles use the positive turn path");
    expect_true(close_enough(configs[1].turn_angle_per_frame, high_angle),
                "wrapped animation angles use the negative turn path");

    memset(zero_duration, 0, sizeof(zero_duration));
    zero_duration[0].anim.offset = 0x500;
    zero_duration[0].unk04 = 0.0f;
    initResolveAnimGroupTable(zero_duration);
    expect_true(zero_duration[0].turn_angle_per_frame == 0.0f,
                "zero-duration animation groups do not divide by zero");

    memset(groups, 0, sizeof(groups));
    memset(group_pointers, 0, sizeof(group_pointers));
    memset(configs, 0, sizeof(configs));
    configs[0].anim.offset = 0x600;
    configs[0].unk04 = 1.0f;
    for (i = 0; i < 32; ++i)
    {
        group_pointers[i] = &groups[i];
        groups[i].table = (struct weapon_firing_animation_table (*)[])configs;
        groups[i].len = i == 0 ? -1 : 7;
    }
    initResolveAnimGroups(group_pointers);
    expect_true(groups[0].len == 1 && groups[1].len == 7,
                "animation group arrays resolve only groups marked unknown");
    expect_true(configs[0].anim.anim ==
                    (ModelAnimation *)(animation_segment + 0x600),
                "animation group arrays preserve the resolved table pointer");
}

static void test_casing_pool_clears_only_occupancy_headers(void)
{
    CasingRecord before;

    reset_fixture();
    before = g_Casings[7];
    initCasingPool();
    expect_true(g_Casings[0].header == NULL &&
                    g_Casings[19].header == NULL,
                "casing initialization clears the complete fixed-size pool");
    expect_true(g_Casings[7].pos.x == before.pos.x &&
                    g_Casings[7].vel.y == before.vel.y,
                "casing initialization does not destroy motion state");

    g_Casings[3].header = (ModelFileHeader *)(uintptr_t)0x1234;
    casingsInit();
    expect_true(g_Casings[3].header == NULL,
                "the public casing initializer delegates to the pool reset");
}

static void test_weapon_animation_initialization_resolves_reactions_and_rates(void)
{
    static const uintptr_t rate_offsets[] = {
        0x4018, 0x40d4, 0x4070, 0x8204, 0x777c,
        0x77d4, 0x84c4, 0x8520,
    };
    int i;

    reset_fixture();
    ptr_animation_table = NULL;
    for (i = 0; i < (int)(sizeof(rate_offsets) / sizeof(rate_offsets[0])); ++i)
    {
        ModelAnimation *animation =
            (ModelAnimation *)(animation_segment + rate_offsets[i]);
        animation->unk04 = 4;
    }

    initWeaponAnimGroups();

    expect_true(g_HitReactionTable[0].deathAnimCount == 2 &&
                    g_HitReactionTable[0].flinchAnimCount == 2,
                "weapon initialization resolves both reaction animation lists");
    expect_true((uintptr_t)death_stagger[0].struck_anim == 0x520 &&
                    (uintptr_t)death_stagger[1].struck_anim == 0x620,
                "weapon initialization resolves the stagger list sentinel");
    expect_true(close_enough(D_80030984, 0.225f) &&
                    close_enough(D_80030988, 0.225f) &&
                    close_enough(D_8003098C, 0.225f) &&
                    close_enough(D_80030990, 0.225f) &&
                    close_enough(D_80030994, 0.225f) &&
                    close_enough(D_80030998, 0.225f) &&
                    close_enough(D_8003099C, 0.225f) &&
                    close_enough(D_800309A0, 0.225f) &&
                    close_enough(D_800309A4, 0.225f),
                "weapon initialization derives all movement animation rates");
}

int main(void)
{
    map_animation_segment();
    reset_fixture();
    test_reset_counts_sentinel_terminated_lists();
    test_root_motion_helpers_cover_empty_and_nonempty_ranges();
    test_animation_table_rebase_and_group_angles();
    test_casing_pool_clears_only_occupancy_headers();
    test_weapon_animation_initialization_resolves_reactions_and_rates();

    munmap(animation_segment, animation_segment_size);
    if (failures != 0)
    {
        fprintf(stderr, "%d actor/animation initialization contract(s) failed\n",
                failures);
        return 1;
    }

    puts("actor/animation initialization suite passed");
    return 0;
}
