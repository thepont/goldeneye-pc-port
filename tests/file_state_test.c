#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <ultra64.h>
#include "file.h"
#include "file2.h"
#include "front.h"

#ifdef PORT
extern int setenv(const char *name, const char *value, int overwrite);
extern int unsetenv(const char *name);
#endif

s32 selected_folder_num;
s32 selected_folder_num_copy;
GAMEMODE gamemode;
s32 selected_stage;
s32 briefingpage;
DIFFICULTY selected_difficulty;
s32 g_AppendCheatSinglePlayer;
s32 g_NewCheatUnlocked;
f32 slider_007_mode_reaction;
f32 slider_007_mode_health;
f32 slider_007_mode_damage;
f32 slider_007_mode_accuracy;
s32 array_favweapon[MAX_PLAYER_COUNT][GUNHANDS];
struct mission_folder_setup mission_folder_setup_entries[2];
s16 solo_target_time_array[20][3];

static DIFFICULTY fake_difficulty;
static s32 fake_mission_timer;
static s32 fake_player_number;
static bool fake_cheat_unlocked;
static save_data fake_save;
static int unlock_calls;
static int save_cheat_calls;
static int load_settings_calls;
static int clear_save_calls;
static int update_bond_calls;
static int copy_save_calls;
static int copy_demo_calls;
static s32 last_unlock_folder;
static LEVEL_SOLO_SEQUENCE last_unlock_stage;
static DIFFICULTY last_unlock_difficulty;
static s32 last_unlock_time;
static s32 last_saved_cheat;
static s32 last_copied_demo_folder;
static int failures;

void osSyncPrintf(const char *format, ...)
{
    (void)format;
}

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

DIFFICULTY lvlGetSelectedDifficulty(void)
{
    return fake_difficulty;
}

s32 getMissiontimer(void)
{
    return fake_mission_timer;
}

s32 get_cur_playernum(void)
{
    return fake_player_number;
}

void fileUnlockStageInFolderAtDifficulty(s32 foldernum,
                                         LEVEL_SOLO_SEQUENCE levelid,
                                         DIFFICULTY difficulty,
                                         s32 newtime)
{
    unlock_calls++;
    last_unlock_folder = foldernum;
    last_unlock_stage = levelid;
    last_unlock_difficulty = difficulty;
    last_unlock_time = newtime;
}

bool fileGetIsCheatUnlocked(save_data *save, s32 cheat)
{
    (void)save;
    (void)cheat;
    return fake_cheat_unlocked;
}

save_data *fileGetSaveForFoldernum(u32 folder)
{
    (void)folder;
    return &fake_save;
}

void fileSaveFolderUnlockCheat(s32 foldernum, s32 cheat)
{
    save_cheat_calls++;
    last_unlock_folder = foldernum;
    last_saved_cheat = cheat;
}

void fileLoadSettingsForFolder(u32 folder)
{
    expect(folder == (u32)selected_folder_num,
           "selected-folder settings wrapper forwards the active folder");
    load_settings_calls++;
}

void fileClearSavefileForFolder(s32 folder)
{
    expect(folder == selected_folder_num,
           "selected-folder delete wrapper forwards the active folder");
    clear_save_calls++;
}

void fileUpdateSelectedBondInSave(s32 folder)
{
    expect(folder == selected_folder_num,
           "selected-folder bond wrapper forwards the active folder");
    update_bond_calls++;
}

u8 fileGetBondForFolder(u32 folder)
{
    expect(folder == (u32)selected_folder_num,
           "selected-folder bond query forwards the active folder");
    return 7;
}

void fileCopySave(s32 folder, save_data *out_save)
{
    expect(folder == selected_folder_num && out_save != NULL,
           "save-copy wrapper forwards its selected folder and output");
    copy_save_calls++;
}

void fileCopyDemoSaveToRamRomSave(u32 folder, save_data *save)
{
    expect(folder == 100U && save != NULL,
           "demo-save wrapper uses the reserved demo folder and output");
    copy_demo_calls++;
    last_copied_demo_folder = (s32)folder;
}

s32 pull_and_display_text_for_folder_a0(s32 stage)
{
    return stage + 10;
}

static void reset_fixture(void)
{
    memset(&fake_save, 0, sizeof(fake_save));
    memset(array_favweapon, 0, sizeof(array_favweapon));
    memset(solo_target_time_array, 0, sizeof(solo_target_time_array));
    memset(mission_folder_setup_entries, 0, sizeof(mission_folder_setup_entries));
    fake_difficulty = DIFFICULTY_AGENT;
    fake_mission_timer = 0;
    fake_player_number = 0;
    fake_cheat_unlocked = FALSE;
    unlock_calls = 0;
    save_cheat_calls = 0;
    load_settings_calls = 0;
    clear_save_calls = 0;
    update_bond_calls = 0;
    copy_save_calls = 0;
    copy_demo_calls = 0;
    last_unlock_folder = -1;
    last_unlock_stage = 0;
    last_unlock_difficulty = DIFFICULTY_MULTI;
    last_unlock_time = -1;
    last_saved_cheat = -1;
    last_copied_demo_folder = -1;
    selected_folder_num = 2;
    selected_folder_num_copy = 3;
    selected_stage = LEVELID_NONE;
    briefingpage = -1;
    selected_difficulty = DIFFICULTY_MULTI;
    gamemode = GAMEMODE_INTRO;
    g_AppendCheatSinglePlayer = FALSE;
    g_NewCheatUnlocked = 0;
    mission_folder_setup_entries[0].mission_num = 4;
    solo_target_time_array[4][DIFFICULTY_AGENT] = 10;
}

static void test_007_modifiers(void)
{
    reset_fixture();
    slider_007_mode_reaction = 2.0f;
    slider_007_mode_health = 3.0f;
    slider_007_mode_damage = 4.0f;
    slider_007_mode_accuracy = 5.0f;
    fake_difficulty = DIFFICULTY_AGENT;
    expect(get_007_reaction_speed() == 0.0f && get_007_health_mod() == 1.0f &&
               get_007_damage_mod() == 1.0f && get_007_accuracy_mod() == 1.0f,
           "007 modifiers use neutral values outside 007 difficulty");

    fake_difficulty = DIFFICULTY_007;
    expect(get_007_reaction_speed() == 2.0f && get_007_health_mod() == 3.0f &&
               get_007_damage_mod() == 4.0f && get_007_accuracy_mod() == 5.0f,
           "007 modifiers expose the configured values at 007 difficulty");
}

static void test_briefing_unlock_guards_and_success(void)
{
    reset_fixture();
    briefingpage = 0;
    selected_difficulty = DIFFICULTY_AGENT;
    fake_mission_timer = 600;
    end_of_mission_briefing();
    expect(unlock_calls == 1 && last_unlock_folder == 2 &&
               last_unlock_stage == 4 && last_unlock_difficulty == DIFFICULTY_AGENT &&
               last_unlock_time == 10,
           "mission briefing unlocks the stage with the elapsed time");
    expect(save_cheat_calls == 1 && last_saved_cheat == 4 &&
               g_NewCheatUnlocked == TRUE,
           "mission briefing saves a newly earned cheat exactly once");

    reset_fixture();
    briefingpage = 0;
    selected_difficulty = DIFFICULTY_AGENT;
    fake_mission_timer = 600;
    fake_cheat_unlocked = TRUE;
    g_NewCheatUnlocked = 88;
    end_of_mission_briefing();
    expect(unlock_calls == 1 && save_cheat_calls == 0 && g_NewCheatUnlocked == FALSE,
           "mission briefing leaves an existing cheat unlocked and clears the new-cheat flag");

    reset_fixture();
    briefingpage = -1;
    end_of_mission_briefing();
    selected_difficulty = DIFFICULTY_007;
    briefingpage = 0;
    end_of_mission_briefing();
    selected_difficulty = DIFFICULTY_007;
    end_of_mission_briefing();
    g_AppendCheatSinglePlayer = TRUE;
    selected_difficulty = DIFFICULTY_AGENT;
    end_of_mission_briefing();
    expect(unlock_calls == 0 && save_cheat_calls == 0,
           "mission briefing guards invalid, 007, and cheat-run paths");
}

static void GivenMissionTimeExceedsTarget_WhenEndingBriefing_ThenProgressIsRecordedWithoutSavingCheat(void)
{
    /* Arrange */
    reset_fixture();
    briefingpage = 0;
    selected_difficulty = DIFFICULTY_AGENT;
    fake_mission_timer = 11 * GAME_TICKRATE;

    /* Act */
    end_of_mission_briefing();

    /* Assert */
    expect(last_unlock_folder == 2 &&
               last_unlock_stage == 4 &&
               last_unlock_difficulty == DIFFICULTY_AGENT &&
               last_unlock_time == 11,
           "an over-target mission still records stage progress with elapsed seconds");
    expect(save_cheat_calls == 0 && g_NewCheatUnlocked == FALSE,
           "an over-target mission does not save or report a newly unlocked cheat");
}

static void GivenSaveLoggingIsEnabled_WhenEndingBriefing_ThenUnlockBehaviorIsUnchanged(void)
{
    /* Arrange */
    reset_fixture();
    briefingpage = 0;
    selected_difficulty = DIFFICULTY_AGENT;
    fake_mission_timer = 600;
    unsetenv("GE_SAVELOG");
    expect(setenv("GE_SAVELOG", "1", 1) == 0,
           "save logging can be enabled for the diagnostic-path test");

    /* Act */
    end_of_mission_briefing();

    /* Teardown */
    unsetenv("GE_SAVELOG");

    /* Assert */
    expect(unlock_calls == 1 && save_cheat_calls == 1 &&
               g_NewCheatUnlocked == TRUE,
           "save logging does not change mission unlock behavior");
}

static void test_selected_state_and_wrappers(void)
{
    save_data copy;

    reset_fixture();
    fileSetCurrentFolder(9);
    expect(selected_folder_num == 9,
           "folder setter stores the selected folder");
    set_selected_difficulty(DIFFICULTY_SECRET);
    expect(selected_difficulty == DIFFICULTY_SECRET,
           "difficulty setter accepts secret difficulty");
    set_selected_difficulty(DIFFICULTY_00);
    expect(selected_difficulty == DIFFICULTY_00,
           "difficulty setter accepts 00 difficulty");
    set_selected_difficulty(DIFFICULTY_007);
    expect(selected_difficulty == DIFFICULTY_007,
           "difficulty setter accepts 007 difficulty");
    set_selected_difficulty((DIFFICULTY)99);
    expect(selected_difficulty == DIFFICULTY_AGENT,
           "difficulty setter normalizes unknown values to agent");

    set_solo_and_ptr_briefing(12);
    expect(gamemode == GAMEMODE_SOLO && selected_stage == 12 && briefingpage == 22,
           "solo briefing setup selects solo mode, stage, and briefing page");
    fileLoadSaveSettingsForSelectedFolder(0);
    deleteCurrentSelectedFolder();
    fileUpdateBondInCurrentFolder();
    expect(fileGetBondForCurrentFolder() == 7 && load_settings_calls == 1 &&
               clear_save_calls == 1 && update_bond_calls == 1,
           "selected-folder wrappers call their storage operations");

    sub_GAME_7F01D61C(&copy);
    set_selected_foldernum_and_copy_demo_eeprom(&copy);
    expect(copy_save_calls == 1 && copy_demo_calls == 1 &&
               selected_folder_num_copy == 9 && selected_folder_num == 100 &&
               last_copied_demo_folder == 100,
           "save-copy operations preserve the prior folder and use demo folder 100");

    fake_player_number = 2;
    store_favorite_weapon_current_player(31, 47);
    expect(array_favweapon[2][GUNRIGHT] == 31 &&
               array_favweapon[2][GUNLEFT] == 47,
           "favorite weapons are stored in the active player's two hands");
}

int main(void)
{
    test_007_modifiers();
    test_briefing_unlock_guards_and_success();
    GivenMissionTimeExceedsTarget_WhenEndingBriefing_ThenProgressIsRecordedWithoutSavingCheat();
    GivenSaveLoggingIsEnabled_WhenEndingBriefing_ThenUnlockBehaviorIsUnchanged();
    test_selected_state_and_wrappers();

    if (failures != 0)
    {
        return 1;
    }

    puts("file state suite passed");
    return 0;
}
