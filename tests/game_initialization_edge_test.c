#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "image.h"

struct bondstruct_unk_animation_related {
    char *uselessPointer;
    char *animBufferPtr1;
    char *animBufferPtr2;
};

void image_entries_load(void);
void initAnimationsBuffer(struct bondstruct_unk_animation_related *animBuffer,
                          OSMesgQueue *mq, s32 unused);
void sub_GAME_7F007290(void);

struct image_entry g_Textures[4];
struct bondstruct_unk_animation_related *D_80036414;
OSMesgQueue *D_80036418;
s32 D_8003641C;
f32 flt_CODE_bss_80079E80;
f32 flt_CODE_bss_80079E84;
f32 flt_CODE_bss_80079E88;

static int failures;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void test_image_entry_offsets(void)
{
    memset(g_Textures, 0, sizeof(g_Textures));
    g_Textures[0].dataoffset = 12;
    g_Textures[1].dataoffset = 20;
    g_Textures[2].dataoffset = 8;
    g_Textures[3].dataoffset = 0xffff;

    image_entries_load();

    expect(g_Textures[0].dataoffset == 0,
           "image offset conversion starts the first entry at zero");
    expect(g_Textures[1].dataoffset == 12,
           "image offset conversion accumulates the first image size");
    expect(g_Textures[2].dataoffset == 32,
           "image offset conversion accumulates preceding image sizes");
    expect(g_Textures[3].dataoffset == 40,
           "image offset conversion stores the terminal image size");

    memset(g_Textures, 0, sizeof(g_Textures));
    g_Textures[0].dataoffset = 0xffff;
    image_entries_load();
    expect(g_Textures[0].dataoffset == 0,
           "an empty image table gets a zero terminal offset");
}

static void test_animation_buffer_registration(void)
{
    struct bondstruct_unk_animation_related animation_buffer = {};
    OSMesgQueue message_queue = {};

    D_80036414 = NULL;
    D_80036418 = NULL;
    D_8003641C = 0;
    initAnimationsBuffer(&animation_buffer, &message_queue, 37);

    expect(D_80036414 == &animation_buffer,
           "animation initialization stores the animation buffer");
    expect(D_80036418 == &message_queue,
           "animation initialization preserves the queue pointer");
    expect(D_8003641C == 37,
           "animation initialization stores the unused compatibility value");
}

static void test_animated_texture_state_reset(void)
{
    flt_CODE_bss_80079E80 = 10.0f;
    flt_CODE_bss_80079E84 = -20.0f;
    flt_CODE_bss_80079E88 = 3.0f;

    sub_GAME_7F007290();

    expect(flt_CODE_bss_80079E80 == 0.0f &&
               flt_CODE_bss_80079E84 == 0.0f &&
               flt_CODE_bss_80079E88 == 0.0f,
           "animated texture offsets reset as one state transition");
}

int main(void)
{
    test_image_entry_offsets();
    test_animation_buffer_registration();
    test_animated_texture_state_reset();

    if (failures != 0)
    {
        fprintf(stderr, "%d game initialization contract(s) failed\n", failures);
        return 1;
    }

    puts("game initialization edge suite passed");
    return 0;
}
