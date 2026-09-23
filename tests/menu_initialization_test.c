#include <stdio.h>
#include <stdint.h>

#include <ultra64.h>
#include <memp.h>
#include "front.h"

void init_menus_or_reset(void);

MENU current_menu;
MENU menu_update;
MENU maybe_prev_menu;
s32 screen_size;
s32 spectrum_related_flag;
s32 is_emulating_spectrum;
s32 folder_selection_screen_option_icon;
s32 folder_selected_for_deletion;
s32 folder_selected_for_deletion_choice;
s32 tab_start_highlight;
s32 tab_next_highlight;
s32 tab_prev_highlight;
s32 maybe_is_in_menu;
s32 prev_keypresses;
u8 *ptr_logo_and_walletbond_DL;
u8 *ptr_menu_videobuffer;
struct Model *walletinst[4];

static union
{
    long double alignment;
    u8 bytes[0x100000 + 64];
} allocation_storage;
static size_t allocation_offset;
static u32 allocation_sizes[4];
static u8 allocation_banks[4];
static int allocation_count;
static int intro_matrix_calls;
static int failures;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

void *mempAllocBytesInBank(u32 size, u8 bank)
{
    void *result = &allocation_storage.bytes[3 + allocation_offset];

    allocation_sizes[allocation_count] = size;
    allocation_banks[allocation_count] = bank;
    allocation_count++;
    allocation_offset += size;
    return result;
}

void alloc_intro_matrices(void)
{
    intro_matrix_calls++;
}

static void seed_menu_state(void)
{
    int i;

    current_menu = MENU_MODE_SELECT;
    menu_update = MENU_INVALID;
    maybe_prev_menu = MENU_BRIEFING;
    screen_size = SCREEN_SIZE_440x330;
    spectrum_related_flag = FALSE;
    is_emulating_spectrum = TRUE;
    folder_selection_screen_option_icon = 9;
    folder_selected_for_deletion = FOLDER4;
    folder_selected_for_deletion_choice = FOLDER1;
    tab_start_highlight = TRUE;
    tab_next_highlight = TRUE;
    tab_prev_highlight = TRUE;
    maybe_is_in_menu = FALSE;
    prev_keypresses = TRUE;
    ptr_logo_and_walletbond_DL = NULL;
    ptr_menu_videobuffer = NULL;
    for (i = 0; i < 4; ++i)
    {
        walletinst[i] = (struct Model *)(uintptr_t)(i + 1);
    }
    allocation_offset = 0;
    allocation_count = 0;
    intro_matrix_calls = 0;
}

static void test_menu_reset_preserves_full_width_video_pointer(void)
{
    uintptr_t raw_video;

    seed_menu_state();
    init_menus_or_reset();
    raw_video = (uintptr_t)&allocation_storage.bytes[3 + 0x85000];

    expect(current_menu == (MENU)~MENU_LEGAL_SCREEN &&
               maybe_prev_menu == (MENU)~MENU_LEGAL_SCREEN &&
               screen_size == SCREEN_SIZE_320x240 && spectrum_related_flag == TRUE &&
               is_emulating_spectrum == FALSE &&
               folder_selection_screen_option_icon == 0 &&
               folder_selected_for_deletion == -1 &&
               folder_selected_for_deletion_choice == 1 &&
               tab_start_highlight == FALSE && tab_next_highlight == FALSE &&
               tab_prev_highlight == FALSE && maybe_is_in_menu == TRUE &&
               menu_update == MENU_FILE_SELECT,
           "menu initialization resets navigation and selection state");
    expect(allocation_count == 2 && allocation_sizes[0] == 0x85000 &&
               allocation_sizes[1] == 0x4b040 &&
               allocation_banks[0] == MEMPOOL_STAGE &&
               allocation_banks[1] == MEMPOOL_STAGE,
           "menu initialization allocates the PC logo and video buffers");
    expect(ptr_logo_and_walletbond_DL == &allocation_storage.bytes[3] &&
               (uintptr_t)ptr_menu_videobuffer == ((raw_video + 0x3fU) & ~0x3fU),
           "menu initialization preserves and aligns the full-width video pointer");
    expect(walletinst[0] == NULL && walletinst[1] == NULL &&
               walletinst[2] == NULL && walletinst[3] == NULL &&
               intro_matrix_calls == 1,
           "menu initialization clears wallet instances and initializes intro matrices");
}

static void test_menu_update_boundary_paths(void)
{
    seed_menu_state();
    prev_keypresses = FALSE;
    menu_update = MENU_INVALID;
    init_menus_or_reset();
    expect(menu_update == MENU_FILE_SELECT,
           "menu initialization repairs a negative pending menu update");

    seed_menu_state();
    prev_keypresses = FALSE;
    menu_update = MENU_MODE_SELECT;
    init_menus_or_reset();
    expect(menu_update == MENU_MODE_SELECT,
           "menu initialization preserves a valid pending menu update");
}

int main(void)
{
    test_menu_reset_preserves_full_width_video_pointer();
    test_menu_update_boundary_paths();

    if (failures != 0)
    {
        fprintf(stderr, "%d menu-initialization contract(s) failed\n", failures);
        return 1;
    }

    puts("menu-initialization suite passed");
    return 0;
}
