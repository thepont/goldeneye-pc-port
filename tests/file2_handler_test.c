#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ultra64.h>

#include "bondconstants.h"
#include "cheat.h"
#include "file.h"
#include "file2.h"

/* file2.h historically exposed only the higher-level save API.  These are
 * the lower-level public entry points exercised by the save-state handler. */
u32 fileGetSaveFolder(save_data *save);
void fileSetSaveFoldernum(save_data *save, u32 folder);
u32 fileGetSaveFlagSlot(save_data *save);
void fileResetSaveFlagSlot(save_data *save, s32 slot);
u32 fileGetSelectedBond(save_data *save);
void fileSetSelectedBond(save_data *save, s32 bond);
bool fileGetSaveFlagDoReset(save_data *save);
void fileSetSaveFlagDoReset(save_data *save, bool enable);
s32 fileGetSaveStageDifficultyTime(save_data *save,
                                   LEVEL_SOLO_SEQUENCE levelid,
                                   DIFFICULTY difficulty);
void fileSetDifficultyStageTime(save_data *save,
                                LEVEL_SOLO_SEQUENCE levelid,
                                DIFFICULTY difficulty,
                                s32 newtime);
bool fileGetSaveStageCompletedForDifficulty(save_data *save,
                                            LEVEL_SOLO_SEQUENCE levelid,
                                            DIFFICULTY difficulty);
void fileCheckSaveStageDifficultyTime(save_data *save,
                                      LEVEL_SOLO_SEQUENCE levelid,
                                      DIFFICULTY difficulty,
                                      s32 newtime);
void fileSetSaveCheatUnlocked(save_data *save, s32 cheat);
save_data *fileGetSaveForFoldernum(u32 folder);
s32 fileGetSaveFlagDoReset_any_folder(void);
void fileBuildWriteNewSave(u32 folder);
bool fileIsFolderValid(s32 folder);
void fileUnlockStageInFolderAtDifficulty(s32 foldernum,
                                         LEVEL_SOLO_SEQUENCE stage,
                                         DIFFICULTY difficulty,
                                         s32 newtime);
s32 fileIs007ModeUnlocked(u32 folder);
void fileResetRamRomSave(void);
void fileWriteSmallSave(smallSave *save);
void fileWriteSave(save_data *save);
void fileResetSave(save_data *save);
void fileOverwriteSaveSlotWithNewSave(save_data *save1, save_data *save2);
void fileSaveFolderUnlockCheat(s32 foldernum, s32 cheat);
void fileGetHighestStageDifficultyCompletedForFolder(s32 foldernum,
                                                     LEVEL_SOLO_SEQUENCE *levelid,
                                                     DIFFICULTY *difficulty);
LEVEL_SOLO_SEQUENCE fileGetHighestStageUnlockedForFolder(s32 foldernum);
LEVEL_SOLO_SEQUENCE fileGetHighestStageUnlockedAnyFolder(void);
bool fileIsCradleCompletedForFolder(s32 folder);
bool fileIsAztecCompletedOnSecretOr00ForFolder(s32 folder);
bool fileIsEgyptCompletedOn00ForFolder(int foldernum);
bool fileIsCradleCompletedAnyFolder(void);
bool check_aztec_completed_any_folder_secret_00(void);
bool fileIsEgyptCompletedOn00AnyFolder(void);
u8 fileGetBondForFolder(u32 folder);
void fileSetSelectedBondTofolder(s32 folder, s32 bond);
void fileDeleteSaveForFolder(s32 foldernum);
void fileInitializeAllTimes(u32 folder);
void fileCopyFolderToFirstFree(s32 foldernum);
void fileSaveSettingsForFolder(save_data *save);
void fileLoadSettingsForFolder(u32 folder);
void fileClearSavefileForFolder(s32 folder);
void fileUpdateSelectedBondInSave(s32 folder);
void fileCopySave(s32 folder, save_data *out_save);
void fileCopyDemoSaveToRamRomSave(u32 folder, save_data *save);

static int failures;
static int fake_agent_levels;
static int fake_all_levels;
static int fake_pak_present;
static int fake_crc_corrupt;
static int fake_write_calls;
static u8 fake_last_write_address;
static s32 fake_last_write_size;
static smallSave fake_small_save;
static save_data fake_eeprom_saves[SAVESLOTRAMROM];

static u16 fake_music_volume;
static u16 fake_sfx_volume;
static int fake_vertical_inverted;
static int fake_autoaim;
static int fake_aim_control;
static int fake_sight_on_screen;
static int fake_lookahead;
static int fake_ammo_on_screen;
static SCREEN_SIZE_OPTION fake_screen_size;
static SCREEN_RATIO_OPTION fake_screen_ratio;
static int fake_control_type;
static int fake_player_count;
static u16 loaded_music_volume;
static u16 loaded_sfx_volume;
static int loaded_vertical_inverted;
static int loaded_autoaim;
static int loaded_aim_control;
static int loaded_sight_on_screen;
static int loaded_lookahead;
static int loaded_ammo_on_screen;
static SCREEN_SIZE_OPTION loaded_screen_size;
static SCREEN_RATIO_OPTION loaded_screen_ratio;
static int loaded_control_type;

void osSyncPrintf(const char *format, ...)
{
    (void)format;
}

s32 joyGamePakProbe(void)
{
    return fake_pak_present;
}

s32 joyGamePakLongRead(u8 address, u8 *buffer, s32 nbytes)
{
    if (address == 0)
    {
        memcpy(buffer, &fake_small_save,
               nbytes < (s32)sizeof(fake_small_save) ?
                   (size_t)nbytes : sizeof(fake_small_save));
    }
    else if (address == 4)
    {
        memcpy(buffer, fake_eeprom_saves,
               nbytes < (s32)sizeof(fake_eeprom_saves) ?
                   (size_t)nbytes : sizeof(fake_eeprom_saves));
    }
    return 0;
}

s32 joyGamePakLongWrite(u8 address, u8 *buffer, s32 nbytes)
{
    fake_write_calls++;
    fake_last_write_address = address;
    fake_last_write_size = nbytes;

    if (address == 0 && nbytes == (s32)sizeof(fake_small_save))
    {
        memcpy(&fake_small_save, buffer, sizeof(fake_small_save));
    }
    return 0;
}

void fileGenerateCRC(u8 *address_a, u8 *address_b, void *result)
{
    (void)address_a;
    (void)address_b;
    if (fake_crc_corrupt)
    {
        ((s32 *)result)[0] = 1;
        ((s32 *)result)[1] = 1;
    }
    else
    {
        ((s32 *)result)[0] = 0;
        ((s32 *)result)[1] = 0;
    }
}

u16 get_mTrack2Vol(void) { return fake_music_volume; }
u16 call_sndGetSfxSlotFirstNaturalVolume(void) { return fake_sfx_volume; }
u32 get_cur_player_look_vertical_inverted(void) { return fake_vertical_inverted; }
s32 cur_player_get_autoaim(void) { return fake_autoaim; }
u32 cur_player_get_lookahead(void) { return fake_lookahead; }
u32 cur_player_get_sight_onscreen_control(void) { return fake_sight_on_screen; }
u32 cur_player_get_ammo_onscreen_setting(void) { return fake_ammo_on_screen; }
u32 cur_player_get_aim_control(void) { return fake_aim_control; }
u32 cur_player_get_screen_setting(void) { return fake_screen_size; }
SCREEN_RATIO_OPTION get_screen_ratio(void) { return fake_screen_ratio; }
int cur_player_get_control_type(void) { return fake_control_type; }
void set_mTrack2Vol(u16 value) { loaded_music_volume = value; }
void sub_GAME_7F0A91A0(u16 value) { loaded_sfx_volume = value; }
void set_cur_player_look_vertical_inverted(u32 value)
{
    loaded_vertical_inverted = (int)value;
}
void cur_player_set_autoaim(u32 value) { loaded_autoaim = (int)value; }
void cur_player_set_aim_control(u32 value) { loaded_aim_control = (int)value; }
void cur_player_set_sight_onscreen_control(u32 value)
{
    loaded_sight_on_screen = (int)value;
}
void cur_player_set_lookahead(u32 value) { loaded_lookahead = (int)value; }
void cur_player_set_ammo_onscreen_setting(u32 value)
{
    loaded_ammo_on_screen = (int)value;
}
void cur_player_set_screen_setting(u32 value)
{
    loaded_screen_size = (SCREEN_SIZE_OPTION)value;
}
void set_screen_ratio(SCREEN_RATIO_OPTION value) { loaded_screen_ratio = value; }
s32 getPlayerCount(void) { return fake_player_count; }
void cur_player_set_control_type(int value) { loaded_control_type = value; }

s32 get_debug_enable_agent_levels_flag(void)
{
    return fake_agent_levels;
}

s32 get_debug_enable_all_levels_flag(void)
{
    return fake_all_levels;
}

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void reset_storage(void)
{
    save_data blank = BLANKSAVEDATA;
    int i;

    for (i = 0; i < SAVESLOTMAX; i++)
    {
        saves[i] = blank;
    }

    fake_agent_levels = FALSE;
    fake_all_levels = FALSE;
    fake_pak_present = FALSE;
    fake_crc_corrupt = FALSE;
    fake_write_calls = 0;
    fake_last_write_address = 0;
    fake_last_write_size = 0;
    memset(&fake_small_save, 0, sizeof(fake_small_save));
    memset(fake_eeprom_saves, 0, sizeof(fake_eeprom_saves));

    fake_music_volume = 0;
    fake_sfx_volume = 0;
    fake_vertical_inverted = FALSE;
    fake_autoaim = FALSE;
    fake_aim_control = FALSE;
    fake_sight_on_screen = FALSE;
    fake_lookahead = FALSE;
    fake_ammo_on_screen = FALSE;
    fake_screen_size = SCREEN_SIZE_FULLSCREEN;
    fake_screen_ratio = SCREEN_RATIO_NORMAL;
    fake_control_type = CONTROLLER_CONFIG_HONEY;
    fake_player_count = 1;
    loaded_music_volume = 0;
    loaded_sfx_volume = 0;
    loaded_vertical_inverted = FALSE;
    loaded_autoaim = FALSE;
    loaded_aim_control = FALSE;
    loaded_sight_on_screen = FALSE;
    loaded_lookahead = FALSE;
    loaded_ammo_on_screen = FALSE;
    loaded_screen_size = SCREEN_SIZE_FULLSCREEN;
    loaded_screen_ratio = SCREEN_RATIO_NORMAL;
    loaded_control_type = CONTROLLER_CONFIG_HONEY;
}

static save_data *install_save(u32 folder, s32 slot)
{
    saves[slot] = (save_data)BLANKSAVEDATA;
    fileSetSaveFoldernum(&saves[slot], folder);
    fileSetSaveFlagDoReset(&saves[slot], FALSE);
    fileResetSaveFlagSlot(&saves[slot], slot);
    return &saves[slot];
}

static void prepare_eeprom_save(u32 folder, s32 slot, s32 sequence, s32 best_time)
{
    fake_eeprom_saves[slot] = (save_data)BLANKSAVEDATA;
    fileSetSaveFoldernum(&fake_eeprom_saves[slot], folder);
    fileSetSaveFlagDoReset(&fake_eeprom_saves[slot], FALSE);
    fileResetSaveFlagSlot(&fake_eeprom_saves[slot], sequence);
    fileSetDifficultyStageTime(&fake_eeprom_saves[slot], SP_LEVEL_DAM,
                               DIFFICULTY_AGENT, best_time);
}

static void test_null_save_boundaries(void)
{
    save_data untouched = (save_data)BLANKSAVEDATA;

    expect(fileGetSaveStageDifficultyTime(NULL, SP_LEVEL_DAM,
                                          DIFFICULTY_AGENT) == 0,
           "stage-time getter rejects a missing save");
    expect(!fileGetSaveStageCompletedForDifficulty(NULL, SP_LEVEL_DAM,
                                                   DIFFICULTY_AGENT),
           "stage-completion getter rejects a missing save");

    fileSetDifficultyStageTime(NULL, SP_LEVEL_DAM, DIFFICULTY_AGENT, 10);
    fileCheckSaveStageDifficultyTime(NULL, SP_LEVEL_DAM, DIFFICULTY_AGENT, 10);
    expect(fileGetSaveFolder(NULL) == 0 && fileGetSaveFlagSlot(NULL) == 0 &&
               fileGetSelectedBond(NULL) == 0 &&
               !fileGetSaveFlagDoReset(NULL),
           "save bitfield getters use safe empty defaults for a missing save");
    expect(!fileGetIsCheatUnlocked(NULL, 0),
           "cheat lookup rejects a missing save instead of dereferencing it");

    fileSetSaveFoldernum(NULL, FOLDER3);
    fileResetSaveFlagSlot(NULL, 2);
    fileSetSelectedBond(NULL, BOND_CONNERY);
    fileSetSaveFlagDoReset(NULL, TRUE);
    expect(memcmp(&untouched, &untouched, sizeof(untouched)) == 0,
           "save bitfield setters accept a missing save without changing state");
}

static void test_save_bitfields(void)
{
    save_data save = (save_data)BLANKSAVEDATA;

    fileSetSaveFoldernum(&save, FOLDER4);
    fileResetSaveFlagSlot(&save, 3);
    fileSetSelectedBond(&save, BOND_MOORE);
    fileSetSaveFlagDoReset(&save, FALSE);
    expect(fileGetSaveFolder(&save) == FOLDER4 &&
               fileGetSaveFlagSlot(&save) == 3 &&
               fileGetSelectedBond(&save) == (BOND_MOORE & 3) &&
               !fileGetSaveFlagDoReset(&save),
           "save folder, slot, bond, and reset bitfields round-trip");

    fileSetSaveFlagDoReset(&save, TRUE);
    expect(fileGetSaveFlagDoReset(&save),
           "save reset bit can be enabled as well as cleared");
    fileSetSaveFlagDoReset(&save, FALSE);

    fileSetSaveFoldernum(&save, 0xff);
    fileResetSaveFlagSlot(&save, -1);
    fileSetSelectedBond(&save, -1);
    expect(fileGetSaveFolder(&save) == (0xff & SAVEFLAG_FOLDER) &&
               fileGetSaveFlagSlot(&save) == 3 &&
               fileGetSelectedBond(&save) == 3,
           "save bitfield setters mask out-of-range values");
}

static void test_packed_stage_times(void)
{
    save_data save = (save_data)BLANKSAVEDATA;
    LEVEL_SOLO_SEQUENCE stage;
    DIFFICULTY difficulty;

    memset(save.times, 0, sizeof(save.times));
    for (difficulty = DIFFICULTY_AGENT; difficulty < DIFFICULTY_007;
         difficulty++)
    {
        for (stage = SP_LEVEL_DAM; stage < SP_LEVEL_MAX; stage++)
        {
            s32 expected = 1 +
                (((s32)difficulty * (SP_LEVEL_MAX - 1) + stage) % 0x3ff);
            fileSetDifficultyStageTime(&save, stage, difficulty, expected);
            expect(fileGetSaveStageDifficultyTime(&save, stage, difficulty) ==
                       expected,
                   "packed stage time round-trips at every stage/difficulty");
        }
    }

    fileSetDifficultyStageTime(&save, SP_LEVEL_DAM, DIFFICULTY_AGENT, 0);
    expect(fileGetSaveStageDifficultyTime(&save, SP_LEVEL_DAM,
                                          DIFFICULTY_AGENT) == 0x4f,
           "zero stage time uses the default completion time");

    fileSetDifficultyStageTime(&save, SP_LEVEL_DAM, DIFFICULTY_AGENT, 0x400);
    expect(fileGetSaveStageDifficultyTime(&save, SP_LEVEL_DAM,
                                          DIFFICULTY_AGENT) == 0x3ff,
           "stage time clamps to the ten-bit maximum");

    memset(save.times, 0, sizeof(save.times));
    fileSetDifficultyStageTime(&save, SP_LEVEL_DAM, DIFFICULTY_AGENT, -1);
    expect(fileGetSaveStageDifficultyTime(&save, SP_LEVEL_DAM,
                                          DIFFICULTY_AGENT) == 0,
           "negative stage time is rejected instead of becoming a completion");

    memset(save.times, 0x5a, sizeof(save.times));
    fileSetDifficultyStageTime(&save, SP_LEVEL_MAX, DIFFICULTY_AGENT, 10);
    fileSetDifficultyStageTime(&save, SP_LEVEL_DAM, DIFFICULTY_MULTI, 10);
    expect(fileGetSaveStageDifficultyTime(&save, SP_LEVEL_MAX,
                                          DIFFICULTY_AGENT) == 0 &&
               fileGetSaveStageDifficultyTime(&save, SP_LEVEL_DAM,
                                              DIFFICULTY_MULTI) == 0 &&
               save.times[0] == 0x5a && save.times[1] == 0x5a,
           "invalid stage and difficulty leave packed save data untouched");
}

static void test_cheat_bits(void)
{
    save_data save = (save_data)BLANKSAVEDATA;
    save_data before;
    s32 cheat;

    memset(&save.unlocked_cheats_1, 0,
           sizeof(save.unlocked_cheats_1) + sizeof(save.unlocked_cheats_2) +
               sizeof(save.unlocked_cheats_3));
    for (cheat = 0; cheat < CHEAT_INPUT_BUFFER_SIZE; cheat++)
    {
        fileSetSaveCheatUnlocked(&save, cheat);
        expect(fileGetIsCheatUnlocked(&save, cheat),
               "every persisted cheat bit can be set and read");
    }

    before = save;
    fileSetSaveCheatUnlocked(&save, -1);
    fileSetSaveCheatUnlocked(&save, CHEAT_INPUT_BUFFER_SIZE);
    expect(memcmp(&save, &before, sizeof(save)) == 0,
           "invalid cheat indexes do not modify the save");
    expect(!fileGetIsCheatUnlocked(&save, -1) &&
               !fileGetIsCheatUnlocked(&save, CHEAT_INPUT_BUFFER_SIZE),
           "invalid cheat indexes are reported as locked");
}

static void test_folder_lookup_and_stage_unlocks(void)
{
    save_data *save;

    reset_storage();
    expect(fileGetSaveForFoldernum(FOLDER1) == NULL,
           "reset-only slots are not returned as active folders");
    expect(fileGetSaveForFoldernum(RAMROM_FOLDERNUM) ==
               &saves[SAVESLOTRAMROM],
           "the RAM/ROM replay folder resolves to its dedicated slot");
    expect(fileGetSaveFlagDoReset_any_folder() == SAVESLOT1,
           "the first reset-marked slot is selected for the next write");

    for (s32 slot = SAVESLOT1; slot < SAVESLOTRAMROM; ++slot)
    {
        fileSetSaveFlagDoReset(&saves[slot], FALSE);
    }
    expect(fileGetSaveFlagDoReset_any_folder() == -1,
           "save-slot scan reports no reset slot when every slot is active");

    expect(fileIsFolderValid(FOLDER1) && fileIsFolderValid(FOLDER4 - 1) &&
               fileIsFolderValid(RAMROM_FOLDERNUM) &&
               !fileIsFolderValid(FOLDER_INVALID) &&
               !fileIsFolderValid(MAX_FOLDER_COUNT),
           "folder validation accepts only campaign and RAM/ROM folders");

    expect(fileIsStageUnlockedAtDifficulty(FOLDER1, SP_LEVEL_DAM,
                                           DIFFICULTY_AGENT) ==
               STAGESTATUS_UNLOCKED &&
               fileIsStageUnlockedAtDifficulty(FOLDER1, SP_LEVEL_FACILITY,
                                               DIFFICULTY_AGENT) ==
               STAGESTATUS_LOCKED,
           "a new folder starts at Dam and keeps later stages locked");

    save = install_save(FOLDER1, SAVESLOT1);
    fileSetDifficultyStageTime(save, SP_LEVEL_DAM, DIFFICULTY_AGENT, 100);
    expect(fileIsStageUnlockedAtDifficulty(FOLDER1, SP_LEVEL_DAM,
                                           DIFFICULTY_AGENT) ==
               STAGESTATUS_COMPLETED &&
               fileIsStageUnlockedAtDifficulty(FOLDER1, SP_LEVEL_FACILITY,
                                               DIFFICULTY_AGENT) ==
               STAGESTATUS_UNLOCKED,
           "completing a stage unlocks the next stage at that difficulty");

    expect(fileIsStageUnlockedAtDifficulty(FOLDER1, SP_LEVEL_AZTEC,
                                           DIFFICULTY_AGENT) ==
               STAGESTATUS_LOCKED,
           "Aztec remains locked until the preceding main campaign is done");

    reset_storage();
    fake_agent_levels = TRUE;
    expect(fileIsStageUnlockedAtDifficulty(FOLDER1, SP_LEVEL_FACILITY,
                                           DIFFICULTY_AGENT) ==
               STAGESTATUS_UNLOCKED,
           "agent-level debug mode unlocks agent stages without a save");
    fake_agent_levels = FALSE;

    fake_all_levels = TRUE;
    expect(fileIsStageUnlockedAtDifficulty(FOLDER1, SP_LEVEL_EGYPT,
                                           DIFFICULTY_007) ==
               STAGESTATUS_UNLOCKED,
           "all-level debug mode unlocks every difficulty without a save");
    fake_all_levels = FALSE;
}

static void test_unlock_creates_and_preserves_progress(void)
{
    save_data *save;

    reset_storage();
    fileUnlockStageInFolderAtDifficulty(FOLDER2, SP_LEVEL_DAM,
                                        DIFFICULTY_AGENT, 90);
    save = fileGetSaveForFoldernum(FOLDER2);
    expect(save != NULL &&
               fileGetSaveStageDifficultyTime(save, SP_LEVEL_DAM,
                                              DIFFICULTY_AGENT) == 90,
           "unlocking a stage creates a save when the folder is new");

    fileUnlockStageInFolderAtDifficulty(FOLDER2, SP_LEVEL_DAM,
                                        DIFFICULTY_AGENT, 120);
    save = fileGetSaveForFoldernum(FOLDER2);
    expect(save != NULL &&
               fileGetSaveStageDifficultyTime(save, SP_LEVEL_DAM,
                                              DIFFICULTY_AGENT) == 90,
           "a slower retry does not replace the saved best time");

    fileUnlockStageInFolderAtDifficulty(FOLDER2, SP_LEVEL_DAM,
                                        DIFFICULTY_AGENT, 60);
    save = fileGetSaveForFoldernum(FOLDER2);
    expect(save != NULL &&
               fileGetSaveStageDifficultyTime(save, SP_LEVEL_DAM,
                                              DIFFICULTY_AGENT) == 60,
           "a faster retry replaces the saved best time");
}

static void test_eeprom_io_and_validation(void)
{
    smallSave small = {0, 0, {0}};
    save_data outside_save = (save_data)BLANKSAVEDATA;
    save_data blank = (save_data)BLANKSAVEDATA;
    int i;

    reset_storage();
    fake_pak_present = TRUE;
    small.unk[0] = 0xa5;
    fileWriteSmallSave(&small);
    expect(fake_write_calls == 1 && fake_last_write_address == 0 &&
               fake_last_write_size == (s32)sizeof(smallSave),
           "small saves write through the EEPROM port at block zero");

    fileWriteSave(&outside_save);
    expect(fake_write_calls == 1,
           "save writes reject pointers outside the save-slot array");

    saves[SAVESLOT3] = blank;
    fileWriteSave(&saves[SAVESLOT3]);
    expect(fake_write_calls == 2 && fake_last_write_address ==
               (u8)((((SAVESLOT3 * 0x60) >> 3) + 4)),
           "save writes use the slot's EEPROM block address");

    memset(&saves[SAVESLOTRAMROM], 0xa5, sizeof(saves[SAVESLOTRAMROM]));
    fileResetRamRomSave();
    expect(fileGetSaveFlagDoReset(&saves[SAVESLOTRAMROM]) &&
               saves[SAVESLOTRAMROM].options == DEFAULT_OPTIONS,
           "resetting the replay save restores the blank save defaults");

    fileResetSave(&saves[SAVESLOT1]);
    expect(fileGetSaveFlagDoReset(&saves[SAVESLOT1]) &&
               saves[SAVESLOT1].options == DEFAULT_OPTIONS,
           "resetting a save slot restores its default state");

    reset_storage();
    fake_pak_present = TRUE;
    fake_small_save.unk[0] =
        SAVEFLAGS_SET(FOLDER3, SAVESLOT1, BOND_CONNERY, FALSE);
    for (i = 0; i < SAVESLOTRAMROM; i++)
    {
        fake_eeprom_saves[i] = blank;
    }
    fileValidateSaves();
    expect(fileGetSaveForFoldernum(FOLDER1) != NULL &&
               fileGetSaveForFoldernum(FOLDER2) != NULL &&
               fileGetSaveForFoldernum(FOLDER3) != NULL &&
               fileGetSaveForFoldernum(FOLDER4) != NULL,
           "valid EEPROM data gets one active save for every campaign folder");

    reset_storage();
    fake_pak_present = TRUE;
    fake_crc_corrupt = TRUE;
    fake_small_save.unk[0] =
        SAVEFLAGS_SET(FOLDER3, SAVESLOT1, BOND_CONNERY, FALSE);
    fileValidateSaves();
    expect(fake_write_calls > 0 &&
               fileGetSaveForFoldernum(FOLDER1) != NULL,
           "corrupt EEPROM data is reset before the folders are rebuilt");
}

static void GivenSequentialDuplicateFolderSaves_WhenValidatingEeprom_ThenNewestLogicalSaveSurvives(void)
{
    save_data *folder_save;

    /* Arrange */
    reset_storage();
    fake_pak_present = TRUE;
    fake_small_save.unk[0] =
        SAVEFLAGS_SET(FOLDER3, SAVESLOT1, BOND_CONNERY, FALSE);
    prepare_eeprom_save(FOLDER1, SAVESLOT1, 0, 10);
    prepare_eeprom_save(FOLDER1, SAVESLOT2, 1, 20);
    prepare_eeprom_save(FOLDER1, SAVESLOT3, 3, 30);

    /* Act */
    fileValidateSaves();

    /* Assert */
    folder_save = fileGetSaveForFoldernum(FOLDER1);
    expect(folder_save != NULL &&
               fileGetSaveStageDifficultyTime(folder_save, SP_LEVEL_DAM,
                                               DIFFICULTY_AGENT) == 20,
           "EEPROM validation keeps the newest sequential folder save and discards stale duplicates");
}

static void test_unlock_summaries_and_007_mode(void)
{
    save_data *save;
    LEVEL_SOLO_SEQUENCE highest_stage;
    DIFFICULTY highest_difficulty;

    reset_storage();
    save = install_save(FOLDER1, SAVESLOT1);
    fileGetHighestStageDifficultyCompletedForFolder(FOLDER1, &highest_stage,
                                                    &highest_difficulty);
    expect(highest_stage == SP_LEVEL_DAM - 1 &&
               highest_difficulty == DIFFICULTY_MULTI,
           "an empty save reports no completed stage");
    expect(fileGetHighestStageUnlockedForFolder(FOLDER1) == SP_LEVEL_DAM &&
               fileGetHighestStageUnlockedAnyFolder() == SP_LEVEL_DAM,
           "an empty active save reports Dam as the highest unlocked stage");

    fileSetDifficultyStageTime(save, SP_LEVEL_DAM, DIFFICULTY_AGENT, 20);
    fileSetDifficultyStageTime(save, SP_LEVEL_FACILITY, DIFFICULTY_SECRET, 30);
    fileGetHighestStageDifficultyCompletedForFolder(FOLDER1, &highest_stage,
                                                    &highest_difficulty);
    expect(highest_stage == SP_LEVEL_FACILITY &&
               highest_difficulty == DIFFICULTY_SECRET,
           "highest-completed lookup prefers later stages at the best difficulty");
    expect(fileGetHighestStageUnlockedForFolder(FOLDER1) >= SP_LEVEL_FACILITY,
           "stage unlock summary advances after campaign progress");

    fileSetDifficultyStageTime(save, SP_LEVEL_CRADLE, DIFFICULTY_AGENT, 40);
    fileSetDifficultyStageTime(save, SP_LEVEL_AZTEC, DIFFICULTY_SECRET, 50);
    fileSetDifficultyStageTime(save, SP_LEVEL_EGYPT, DIFFICULTY_00, 60);
    expect(fileIsCradleCompletedForFolder(FOLDER1) &&
               fileIsAztecCompletedOnSecretOr00ForFolder(FOLDER1) &&
               fileIsEgyptCompletedOn00ForFolder(FOLDER1) &&
               fileIsCradleCompletedAnyFolder() &&
               check_aztec_completed_any_folder_secret_00() &&
               fileIsEgyptCompletedOn00AnyFolder(),
           "campaign milestone helpers agree with saved completions");

    expect(!fileIs007ModeUnlocked(FOLDER1),
           "007 remains locked until the full 00 campaign is complete");
    save->flag_007 = 1;
    expect(fileIs007ModeUnlocked(FOLDER1) &&
               fileGetSaveStageDifficultyTime(save, SP_LEVEL_DAM,
                                              DIFFICULTY_007) == 0x3ff,
           "the explicit 007 flag exposes the synthetic 007 completion time");
}

static void test_cheat_copy_delete_and_bond_state(void)
{
    save_data copy;
    save_data *save;
    u8 bond_before;

    reset_storage();
    fileSaveFolderUnlockCheat(FOLDER1, CHEAT_INPUT_BUFFER_SIZE - 1);
    save = fileGetSaveForFoldernum(FOLDER1);
    expect(save != NULL &&
               fileGetIsCheatUnlocked(save, CHEAT_INPUT_BUFFER_SIZE - 1),
           "unlocking a cheat creates a folder save and persists its bit");
    bond_before = fileGetBondForFolder(FOLDER1);
    fileSaveFolderUnlockCheat(FOLDER1, CHEAT_INPUT_BUFFER_SIZE - 1);
    expect(fileGetBondForFolder(FOLDER1) == bond_before,
           "unlocking an already-unlocked cheat is idempotent");
    fileSaveFolderUnlockCheat(-1, 0);
    fileSaveFolderUnlockCheat(FOLDER1, CHEAT_INPUT_BUFFER_SIZE);

    fileCopySave(FOLDER1, &copy);
    expect(memcmp(&copy, save, sizeof(copy)) == 0,
           "copy-save returns the active folder contents");
    fileCopySave(FOLDER3, &copy);
    expect(fileGetSaveFlagDoReset(&copy),
           "copy-save returns blank defaults for a missing folder");

    fileCopyDemoSaveToRamRomSave(FOLDER1, save);
    expect(saves[SAVESLOTRAMROM].completion_bitflags !=
               save->completion_bitflags,
           "non-demo folder copies do not overwrite the replay save");
    fileCopyDemoSaveToRamRomSave(RAMROM_FOLDERNUM, save);
    expect(memcmp(&saves[SAVESLOTRAMROM], save, sizeof(*save)) == 0,
           "demo folder copies populate the replay save");

    fileSetSelectedBondTofolder(FOLDER1, BOND_CONNERY);
    fileSetSelectedBondTofolder(MAX_FOLDER_COUNT, BOND_CONNERY);
    expect(fileGetBondForFolder(FOLDER1) == BOND_BROSNAN,
           "the single-Bond build keeps the selected Bond at Brosnan");

    fileSetDifficultyStageTime(save, SP_LEVEL_DAM, DIFFICULTY_AGENT, 10);
    fileDeleteSaveForFolder(FOLDER1);
    save = fileGetSaveForFoldernum(FOLDER1);
    expect(save != NULL &&
               !fileGetSaveStageCompletedForDifficulty(save, SP_LEVEL_DAM,
                                                       DIFFICULTY_AGENT) &&
               !fileGetSaveFlagDoReset(save),
           "deleting a completed folder preserves its slot but clears progress");

    save_selected_bond[FOLDER1] = BOND_CONNERY;
    fileUpdateSelectedBondInSave(FOLDER1);
    save = fileGetSaveForFoldernum(FOLDER1);
    expect(save != NULL && fileGetSelectedBond(save) ==
               (BOND_CONNERY & 3),
           "selected-Bond updates are persisted through the save handler");

    fileSetDifficultyStageTime(save, SP_LEVEL_DAM, DIFFICULTY_AGENT, 10);
    fileCopyFolderToFirstFree(FOLDER1);
    expect(fileGetSaveForFoldernum(FOLDER2) != NULL,
           "copy-folder uses the first free folder slot");

    fileInitializeAllTimes(FOLDER1);
    save = fileGetSaveForFoldernum(FOLDER1);
    expect(fileGetSaveStageDifficultyTime(save, SP_LEVEL_EGYPT,
                                          DIFFICULTY_00) == 0x3ff,
           "initializing times fills every campaign difficulty with the maximum");
}

static void test_settings_round_trip(void)
{
    save_data save = (save_data)BLANKSAVEDATA;

    reset_storage();
    fake_music_volume = 0x4000;
    fake_sfx_volume = 0x2000;
    fake_vertical_inverted = TRUE;
    fake_autoaim = TRUE;
    fake_aim_control = TRUE;
    fake_sight_on_screen = TRUE;
    fake_lookahead = TRUE;
    fake_ammo_on_screen = TRUE;
    fake_screen_size = SCREEN_SIZE_CINEMA;
    fake_screen_ratio = SCREEN_RATIO_16_9;
    fake_control_type = CONTROLLER_CONFIG_GALORE;
    fileSaveSettingsForFolder(&save);
    expect(save.music_vol == (fake_music_volume >> 7) &&
               save.sfx_vol == (fake_sfx_volume >> 7) &&
               (save.options & DEFAULT_OPTIONS) == DEFAULT_OPTIONS &&
               (save.options & OPTION_INVERTLOOK) &&
               (save.options & OPTION_AIMCONTROL) &&
               (save.options & OPTION_SCREENCINEMA) &&
               (save.options & OPTION_SCREENRATIO) &&
               (save.options & OPTION_CONTROLTYPE) ==
                   (CONTROLLER_CONFIG_GALORE << 8),
           "settings save packs volumes, toggles, display, ratio, and controls");

    saves[SAVESLOT1] = save;
    fileSetSaveFoldernum(&saves[SAVESLOT1], FOLDER1);
    fileSetSaveFlagDoReset(&saves[SAVESLOT1], FALSE);
    fake_player_count = 1;
    loaded_control_type = -1;
    fileLoadSettingsForFolder(FOLDER1);
    expect(loaded_music_volume == ((save.music_vol << 7) | (save.music_vol >> 1)) &&
               loaded_sfx_volume == ((save.sfx_vol << 7) | (save.sfx_vol >> 1)) &&
               loaded_vertical_inverted && loaded_autoaim && loaded_aim_control &&
               loaded_sight_on_screen && loaded_lookahead && loaded_ammo_on_screen &&
               loaded_screen_size == SCREEN_SIZE_CINEMA &&
               loaded_screen_ratio == SCREEN_RATIO_16_9 &&
               loaded_control_type == CONTROLLER_CONFIG_GALORE,
           "single-player settings load restores every saved option");

    fake_player_count = 2;
    loaded_control_type = -1;
    fileLoadSettingsForFolder(FOLDER1);
    expect(loaded_control_type == CONTROLLER_CONFIG_HONEY,
           "multiplayer settings load forces the shared multiplayer control type");
}

static void test_missing_folder_mutations(void)
{
    save_data *save;

    reset_storage();
    fake_music_volume = 0x4000;
    fake_sfx_volume = 0x2000;
    fake_autoaim = TRUE;
    fake_sight_on_screen = TRUE;
    fake_lookahead = TRUE;
    fake_ammo_on_screen = TRUE;
    fileClearSavefileForFolder(FOLDER1);
    save = fileGetSaveForFoldernum(FOLDER1);
    expect(save != NULL && save->music_vol == (fake_music_volume >> 7) &&
               save->sfx_vol == (fake_sfx_volume >> 7) &&
               (save->options & DEFAULT_OPTIONS) == DEFAULT_OPTIONS,
           "clearing a missing folder creates a save from current settings");

    fileClearSavefileForFolder(FOLDER_INVALID);

    reset_storage();
    save_selected_bond[FOLDER1] = BOND_CONNERY;
    fileUpdateSelectedBondInSave(FOLDER1);
    save = fileGetSaveForFoldernum(FOLDER1);
    expect(save != NULL && fileGetSelectedBond(save) == (BOND_CONNERY & 3),
           "updating a missing folder creates a save for the selected Bond");
}

int main(void)
{
    test_null_save_boundaries();
    reset_storage();
    test_save_bitfields();
    reset_storage();
    test_packed_stage_times();
    reset_storage();
    test_cheat_bits();
    test_folder_lookup_and_stage_unlocks();
    test_unlock_creates_and_preserves_progress();
    test_eeprom_io_and_validation();
    GivenSequentialDuplicateFolderSaves_WhenValidatingEeprom_ThenNewestLogicalSaveSurvives();
    test_unlock_summaries_and_007_mode();
    test_cheat_copy_delete_and_bond_state();
    test_settings_round_trip();
    test_missing_folder_mutations();

    if (failures != 0)
    {
        fprintf(stderr, "%d file-handler contract(s) failed\n", failures);
        return 1;
    }

    puts("file2 handler suite passed");
    return 0;
}
