#include <stdio.h>
#include <stdint.h>

#include <ultra64.h>
#include "front.h"

void initGameData(void);

MENU current_menu;
MENU menu_update;
MENU maybe_prev_menu;
s32 g_MenuTimer;
s32 selected_folder_num;
s32 selected_stage;
s32 briefingpage;
DIFFICULTY selected_difficulty;
s32 folder_selection_screen_option_icon;
s32 mission_failed_or_aborted;
s32 is_first_time_on_legal_screen;
s32 is_first_time_on_main_menu;
s32 prev_keypresses;
s32 maybe_is_in_menu;
s32 screen_size;
f32 slider_007_mode_reaction;
f32 slider_007_mode_health;
f32 slider_007_mode_accuracy;
f32 slider_007_mode_damage;
s32 intro_character_index;
u32 randomly_selected_intro_animation;
u32 intro_animation_count;
struct Model *cast_model;
struct Model *cast_model_weapon;
u32 full_actor_intro;

static int failures;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

int main(void)
{
    current_menu = MENU_MODE_SELECT;
    menu_update = MENU_MP_OPTIONS;
    maybe_prev_menu = MENU_FILE_SELECT;
    g_MenuTimer = 99;
    selected_stage = LEVELID_DAM;
    briefingpage = BRIEFING_Q;
    selected_difficulty = DIFFICULTY_007;
    screen_size = SCREEN_SIZE_440x330;
    folder_selection_screen_option_icon = 3;
    selected_folder_num = FOLDER4;
    mission_failed_or_aborted = TRUE;
    is_first_time_on_legal_screen = FALSE;
    is_first_time_on_main_menu = FALSE;
    prev_keypresses = TRUE;
    maybe_is_in_menu = FALSE;
    slider_007_mode_reaction = 9.0f;
    slider_007_mode_health = 9.0f;
    slider_007_mode_damage = 9.0f;
    slider_007_mode_accuracy = 9.0f;
    intro_character_index = 9;
    randomly_selected_intro_animation = 9;
    intro_animation_count = 9;
    cast_model = (struct Model *)(uintptr_t)0x1;
    cast_model_weapon = (struct Model *)(uintptr_t)0x2;
    full_actor_intro = TRUE;

    initGameData();

    expect(current_menu == MENU_INVALID && menu_update == MENU_LEGAL_SCREEN &&
               maybe_prev_menu == MENU_INVALID && g_MenuTimer == 0 &&
               selected_stage == LEVELID_NONE && briefingpage == BRIEFING_INVALID &&
               selected_difficulty == DIFFICULTY_MULTI &&
               screen_size == SCREEN_SIZE_320x240 &&
               folder_selection_screen_option_icon == 0 &&
               selected_folder_num == FOLDER1 && mission_failed_or_aborted == FALSE &&
               is_first_time_on_legal_screen == TRUE &&
               is_first_time_on_main_menu == TRUE && prev_keypresses == FALSE &&
               maybe_is_in_menu == TRUE,
           "game-data initialization resets menu and stage selection state");
    expect(slider_007_mode_reaction == 0.0f && slider_007_mode_health == 1.0f &&
               slider_007_mode_damage == 1.0f && slider_007_mode_accuracy == 1.0f &&
               intro_character_index == 0 && randomly_selected_intro_animation == ANIM_idle &&
               intro_animation_count == 0 && cast_model == NULL &&
               cast_model_weapon == NULL && full_actor_intro == FALSE,
           "game-data initialization resets sliders and intro model state");

    if (failures != 0)
    {
        fprintf(stderr, "%d game-data initialization contract(s) failed\n", failures);
        return 1;
    }

    puts("game-data initialization suite passed");
    return 0;
}
