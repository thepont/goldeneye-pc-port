#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "blood_animation.h"
#include "blood_decrypt.h"
#include "player.h"
#include "title.h"

extern u8 die_blood_image_1[];

static int failures;
static s16 fake_width = 320;
static s16 fake_height = 240;
static s16 fake_view_left = 0;
static s16 fake_view_top = 0;
static s16 fake_view_width = 320;
static s16 fake_view_height = 240;
static u8 fake_framebuffer[320 * 240 * 2];
static u8 fake_blood_image[96 * 80];
static u8 fake_allocations[2][80 * 96];
static int fake_allocation_count;
static u8 *fake_decrypt_next;

struct player fake_player;
struct player *g_CurrentPlayer = &fake_player;
Mtx fake_gunbarrel_matrix;
Mtx fake_rare_logo_matrices[2];
Mtx *matrixBufferGunbarrel0 = &fake_gunbarrel_matrix;
Mtx *matrixBufferRareLogo2 = fake_rare_logo_matrices;
u32 D_8002A7D0;

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static u8 opcode(const Gfx *command)
{
    return (u8)(command->words.w0 >> 24);
}

static size_t find_opcode(const Gfx *commands, size_t count, u8 wanted)
{
    size_t i;

    for (i = 0; i < count; i++)
    {
        if (opcode(&commands[i]) == wanted)
        {
            return i;
        }
    }
    return count;
}

static int texture_left(const Gfx *command)
{
    return (int)((command->words.w1 >> 12) & 0xfff);
}

static int texture_top(const Gfx *command)
{
    return (int)(command->words.w1 & 0xfff);
}

static int texture_right(const Gfx *command)
{
    return (int)((command->words.w0 >> 12) & 0xfff);
}

static int texture_bottom(const Gfx *command)
{
    return (int)(command->words.w0 & 0xfff);
}

s16 viGetX(void)
{
    return fake_width;
}

s16 viGetY(void)
{
    return fake_height;
}

s16 viGetViewLeft(void)
{
    return fake_view_left;
}

s16 viGetViewTop(void)
{
    return fake_view_top;
}

s16 viGetViewWidth(void)
{
    return fake_view_width;
}

s16 viGetViewHeight(void)
{
    return fake_view_height;
}

u8 *viGetFrameBuf2(void)
{
    return fake_framebuffer;
}

u32 osVirtualToPhysical(void *address)
{
    return (u32)(uintptr_t)address;
}

void *dynAllocate(s32 size)
{
    void *result;

    expect_true(size == 80 * 96,
                "blood animation requests the documented 80x96 image buffers");
    result = fake_allocations[fake_allocation_count % 2];
    fake_allocation_count++;
    return result;
}

u8 *decrypt_bleeding_animation_data(u8 *source, u8 width, u8 height,
                                    u8 *destination, u8 *status)
{
    (void)source;
    (void)width;
    (void)height;
    (void)destination;
    *status = 0;
    return fake_decrypt_next;
}

void bloodImgTranspose(u8 *source, s32 width, s32 height, u8 *destination)
{
    (void)source;
    (void)width;
    (void)height;
    (void)destination;
}

void sub_GAME_7F01D02C(u8 *source, s32 height, u8 *destination)
{
    (void)source;
    (void)height;
    (void)destination;
}

void sub_GAME_7F01CEEC(u8 *source, s32 width, u8 *destination)
{
    (void)source;
    (void)width;
    (void)destination;
}

void sub_GAME_7F01CC94(u8 *source, u16 size, u8 *destination)
{
    (void)source;
    (void)size;
    (void)destination;
}

static void reset_fixture(void)
{
    memset(&fake_player, 0, sizeof(fake_player));
    memset(fake_allocations, 0, sizeof(fake_allocations));
    fake_allocation_count = 0;
    fake_decrypt_next = die_blood_image_1;
    fake_width = 320;
    fake_height = 240;
    fake_view_left = 0;
    fake_view_top = 0;
    fake_view_width = 320;
    fake_view_height = 240;
    fake_player.bloodImgBufPtrArray[0] = fake_blood_image;
    fake_player.bloodImgIdx = 0;
    D_8002A7D0 = 0;
}

static void test_clear_and_projection_setup_emit_commands(void)
{
    Gfx commands[16];
    Gfx *end;

    memset(commands, 0xa5, sizeof(commands));
    end = clear_framebuffer_black(commands);
    expect_true(end == commands + 4,
                "black framebuffer clear emits exactly four display-list commands");
    expect_true(opcode(&commands[3]) == (u8)G_FILLRECT,
                "black framebuffer clear terminates with a fill rectangle");

    memset(commands, 0, sizeof(commands));
    end = sub_GAME_7F01C1A4(commands);
    expect_true(end == commands + 6,
                "blood overlay projection setup emits its complete six-command state");
}

static void test_fullscreen_blood_overlay_is_screen_bounded(void)
{
    Gfx commands[64];
    Gfx *end;
    size_t rectangle;

    reset_fixture();
    fake_player.bloodImgBufPtrArray[0] = fake_blood_image;
    memset(commands, 0, sizeof(commands));
    end = gunbarrelBloodOverlayDL(commands);
    rectangle = find_opcode(commands, (size_t)(end - commands), (u8)G_TEXRECT);

    expect_true(rectangle < (size_t)(end - commands),
                "gun-barrel blood overlay emits a texture rectangle");
    if (rectangle < (size_t)(end - commands))
    {
        expect_true(texture_left(&commands[rectangle]) == 0 &&
                        texture_top(&commands[rectangle]) == 0,
                    "gun-barrel blood overlay starts at the full-screen origin");
        expect_true(texture_right(&commands[rectangle]) == fake_width * 4 - 1 &&
                        texture_bottom(&commands[rectangle]) == fake_height * 4 - 1,
                    "gun-barrel blood overlay ends at the full-screen bounds");
    }
}

static void test_gameplay_blood_overlay_uses_current_viewport(void)
{
    Gfx commands[64];
    Gfx *end;
    size_t rectangle;

    reset_fixture();
    fake_view_left = 40;
    fake_view_top = 30;
    fake_view_width = 120;
    fake_view_height = 90;
    memset(commands, 0, sizeof(commands));
    end = gameplayBloodOverlayDL(commands);
    rectangle = find_opcode(commands, (size_t)(end - commands), (u8)G_TEXRECT);

    expect_true(rectangle < (size_t)(end - commands),
                "gameplay blood overlay emits a viewport texture rectangle");
    if (rectangle < (size_t)(end - commands))
    {
        expect_true(texture_left(&commands[rectangle]) == fake_view_left * 4 &&
                        texture_top(&commands[rectangle]) == fake_view_top * 4,
                    "gameplay blood overlay starts at the active viewport origin");
        expect_true(texture_right(&commands[rectangle]) ==
                            (fake_view_left + fake_view_width) * 4 - 1 &&
                        texture_bottom(&commands[rectangle]) ==
                            (fake_view_top + fake_view_height) * 4 - 1,
                    "gameplay blood overlay does not draw over the other split viewport");
    }
}

static void test_blood_tint_overlay_restores_projection_state(void)
{
    Gfx commands[32];
    Gfx *end;
    size_t rectangle;

    reset_fixture();
    memset(commands, 0, sizeof(commands));
    end = sub_GAME_7F01CA18(commands);
    rectangle = find_opcode(commands, (size_t)(end - commands), (u8)G_FILLRECT);

    expect_true(rectangle < (size_t)(end - commands),
                "blood tint overlay emits a fill rectangle");
    expect_true(end > commands + 6,
                "blood tint overlay appends tint state after projection setup");
}

static void test_blood_animation_toggles_buffers_and_advances(void)
{
    u8 *first_current;

    reset_fixture();
    fake_decrypt_next = (u8 *)(uintptr_t)UINTPTR_MAX;
    expect_true(die_blood_image_routine(0) == 1,
                "a decoder result at the animation end reports completion");
    expect_true(fake_player.bloodImgCur == die_blood_image_1 &&
                    fake_player.bloodImgIdx == 1 &&
                    fake_player.bloodImgBufPtrArray[1] != NULL &&
                    fake_allocation_count == 2,
                "starting blood animation selects the source and toggles the back buffer");

    first_current = fake_player.bloodImgCur;
    fake_decrypt_next = die_blood_image_1;
    expect_true(die_blood_image_routine(1) == 0,
                "a decoder result before the animation end remains in progress");
    expect_true(fake_player.bloodImgCur == first_current &&
                    fake_player.bloodImgIdx == 0 &&
                    fake_allocation_count == 4,
                "animation continuation keeps the current frame and toggles buffers again");
}

int main(void)
{
    test_clear_and_projection_setup_emit_commands();
    test_fullscreen_blood_overlay_is_screen_bounded();
    test_gameplay_blood_overlay_uses_current_viewport();
    test_blood_tint_overlay_restores_projection_state();
    test_blood_animation_toggles_buffers_and_advances();

    if (failures != 0)
    {
        fprintf(stderr, "%d blood-animation test(s) failed\n", failures);
        return 1;
    }

    puts("blood-animation display suite passed");
    return 0;
}
