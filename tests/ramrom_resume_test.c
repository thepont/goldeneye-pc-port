#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <ultra64.h>

#include "bondconstants.h"
#include "coop.h"
#include "front.h"
#include "file.h"
#include "joy.h"
#include "lv.h"
#include "mp_weapon.h"
#include "player.h"
#include "ramromreplay.h"

/* The replay module owns these globals in the game build. */
extern void copy_current_ingame_registers_before_ramrom_playback(
    ramromfilestructure *state);
extern void copy_recorded_ramrom_registers_to_proper_place_ingame(
    ramromfilestructure *state);
extern void clear_ramrom_block_buffer_heading_ptrs(void);
extern void setRamRomRecordSlot(s32 slot);
extern void replay_recorded_ramrom_at_address(ramromfilestructure *demofile);
extern void replay_recorded_ramrom_from_indy(void);
extern void select_ramrom_to_play(void);
extern void ramromFadeToTitle(void);
extern void record_player_input_as_packet(struct contsample *samples,
                                          s32 first, s32 last);
extern s32 ramrom_replay_handler(struct contsample *samples, s32 index);
extern void iterate_ramrom_entries_handle_camera_out(void);
extern void save_ramrom_to_devtool(void);
extern void load_ramrom_from_devtool(void);
extern void test_if_recording_demos_this_stage_load(LEVELID stage,
                                                    DIFFICULTY difficulty);
extern void stop_recording_ramrom(void);
extern void stop_demo_playback(void);
extern s32 ramrom_demo_related_3;
extern s32 ramrom_demo_related_6;
extern s32 get_recording_ramrom_flag(void);
extern s32 interface_menu0B_runstage(void);
extern ramromfilestructure *ptr_active_demofile;
extern s32 g_ramromPlayBackFlag;
extern s32 recording_ramrom_flag;
extern s32 g_ramromRecordFlag;
extern s32 is_ramrom_flag;

/* RAM/ROM symbols normally supplied by generated ROM assets.  The production
 * declarations are linker addresses, so fixed byte storage models those
 * addresses without depending on the generated asset linker script. */
enum { RAMROM_ASSET_WORDS = 0x100 };
u32 ramrom_Dam_1[RAMROM_ASSET_WORDS];
u32 ramrom_Dam_2[RAMROM_ASSET_WORDS];
u32 ramrom_Facility_1[RAMROM_ASSET_WORDS];
u32 ramrom_Facility_2[RAMROM_ASSET_WORDS];
u32 ramrom_Facility_3[RAMROM_ASSET_WORDS];
u32 ramrom_Runway_1[RAMROM_ASSET_WORDS];
u32 ramrom_Runway_2[RAMROM_ASSET_WORDS];
u32 ramrom_BunkerI_1[RAMROM_ASSET_WORDS];
u32 ramrom_BunkerI_2[RAMROM_ASSET_WORDS];
u32 ramrom_Silo_1[RAMROM_ASSET_WORDS];
u32 ramrom_Silo_2[RAMROM_ASSET_WORDS];
u32 ramrom_Frigate_1[RAMROM_ASSET_WORDS];
u32 ramrom_Frigate_2[RAMROM_ASSET_WORDS];
u32 ramrom_Train[RAMROM_ASSET_WORDS];

u64 g_randomSeed;
u64 g_chrObjRandomSeed;
char ramrom_data_target[0x380];
u8 *address_demo_loaded;

GAMEMODE gamemode;
s32 selected_num_players;
s32 player_char[MAX_PLAYER_COUNT];
s32 player_handicap[MAX_PLAYER_COUNT];
s32 controlstyle_player[MAX_PLAYER_COUNT];
s32 MP_stage_selected;
s32 game_length;
s32 aim_sight_adjustment;
s32 scenario;
s32 record_slot_num;
s32 speedgraphframes;
s32 g_ClockTimer;
s32 g_GlobalTimer;
s32 dword_CODE_bss_8008C5F8;
s32 prev_keypresses;

static s32 fake_controller_count;
static GAMEMODE fake_resume_mode;
static LEVEL_SOLO_SEQUENCE fake_highest_unlocked_stage;
static u32 fake_random_value;
static s32 fake_weapon_set;
static s32 fake_flags[MAX_MULTIPLAYER];
static s32 fake_camera_mode;
static s32 fake_camera_set_calls;
static s32 fake_front_menu;
static s32 fake_front_reload;
static s32 fake_selected_stage;
static s32 fake_selected_difficulty;
static s32 fake_pressed_buttons;
static s32 fake_cont_data_index;
static s32 fake_frame_counter_update;
static contrecordfunc fake_record_callback;
static contplaybackfunc fake_playback_callback;
static s32 fake_playback_controller_count;
static s32 fake_record_callback_calls;
static s32 fake_playback_callback_calls;
static s32 fake_file_exists_remaining;
static s32 fake_file_size;
static s32 fake_ramrom_load_calls;
static s32 fake_saved_file_calls;
static s32 fake_saved_file_size;
static u8 *fake_saved_file_data;
static char fake_saved_file_name[64];
static s32 fake_rom_write_calls;
static u8 fake_indy_ramrom[0x380];
static s32 failures;

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void expect_s32(s32 actual, s32 expected, const char *message)
{
    if (actual != expected)
    {
        fprintf(stderr, "FAIL: %s (actual %d, expected %d)\n",
                message, actual, expected);
        failures++;
    }
}

static void expect_u64(u64 actual, u64 expected, const char *message)
{
    if (actual != expected)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void reset_fixture(void)
{
    memset(ramrom_data_target, 0, sizeof(ramrom_data_target));
    memset(ramrom_Dam_1, 0, sizeof(ramrom_Dam_1));
    memset(ramrom_Dam_2, 0, sizeof(ramrom_Dam_2));
    memset(ramrom_Facility_1, 0, sizeof(ramrom_Facility_1));
    memset(ramrom_Facility_2, 0, sizeof(ramrom_Facility_2));
    memset(ramrom_Facility_3, 0, sizeof(ramrom_Facility_3));
    memset(ramrom_Runway_1, 0, sizeof(ramrom_Runway_1));
    memset(ramrom_Runway_2, 0, sizeof(ramrom_Runway_2));
    memset(ramrom_BunkerI_1, 0, sizeof(ramrom_BunkerI_1));
    memset(ramrom_BunkerI_2, 0, sizeof(ramrom_BunkerI_2));
    memset(ramrom_Silo_1, 0, sizeof(ramrom_Silo_1));
    memset(ramrom_Silo_2, 0, sizeof(ramrom_Silo_2));
    memset(ramrom_Frigate_1, 0, sizeof(ramrom_Frigate_1));
    memset(ramrom_Frigate_2, 0, sizeof(ramrom_Frigate_2));
    memset(ramrom_Train, 0, sizeof(ramrom_Train));
    memset(player_char, 0, sizeof(player_char));
    memset(player_handicap, 0, sizeof(player_handicap));
    memset(controlstyle_player, 0, sizeof(controlstyle_player));
    memset(fake_flags, 0, sizeof(fake_flags));

    fake_controller_count = 1;
    fake_resume_mode = GAMEMODE_SOLO;
    fake_highest_unlocked_stage = SP_LEVEL_DAM;
    fake_random_value = 0;
    fake_weapon_set = 0;
    fake_flags[0] = 10;
    fake_flags[1] = 20;
    fake_flags[2] = 30;
    fake_flags[3] = 40;

    gamemode = GAMEMODE_INTRO;
    selected_num_players = 0;
    MP_stage_selected = 0;
    game_length = 0;
    aim_sight_adjustment = 0;
    scenario = 0;
    g_randomSeed = 0;
    g_chrObjRandomSeed = 0;
    ptr_active_demofile = NULL;
    g_ramromPlayBackFlag = 0;
    recording_ramrom_flag = 0;
    g_ramromRecordFlag = 0;
    ramrom_demo_related_3 = 0;
    ramrom_demo_related_6 = 0;
    is_ramrom_flag = 0;
    fake_camera_mode = CAMERAMODE_FP;
    fake_camera_set_calls = 0;
    fake_front_menu = MENU_INVALID;
    fake_front_reload = 0;
    fake_selected_stage = -1;
    fake_selected_difficulty = -1;
    fake_pressed_buttons = 0;
    fake_cont_data_index = -1;
    fake_frame_counter_update = -1;
    fake_record_callback = NULL;
    fake_playback_callback = NULL;
    fake_playback_controller_count = -1;
    fake_record_callback_calls = 0;
    fake_playback_callback_calls = 0;
    fake_file_exists_remaining = 0;
    fake_file_size = 0;
    fake_ramrom_load_calls = 0;
    fake_saved_file_calls = 0;
    fake_saved_file_size = 0;
    fake_saved_file_data = NULL;
    memset(fake_saved_file_name, 0, sizeof(fake_saved_file_name));
    fake_rom_write_calls = 0;
    memset(fake_indy_ramrom, 0, sizeof(fake_indy_ramrom));
    speedgraphframes = 1;
    g_ClockTimer = 0;
    g_GlobalTimer = 0;
    dword_CODE_bss_8008C5F8 = 0;
    prev_keypresses = 0;
}

s8 joyGetControllerCount(void)
{
    return (s8)fake_controller_count;
}

LEVEL_SOLO_SEQUENCE fileGetHighestStageUnlockedAnyFolder(void)
{
    return fake_highest_unlocked_stage;
}

u32 randomGetNext(void)
{
    return fake_random_value;
}

int geCoopResumeMode(int saved_mode, int controller_count,
                     int solo_mode, int multi_mode, int coop_mode)
{
    (void)saved_mode;
    (void)controller_count;
    (void)solo_mode;
    (void)multi_mode;
    (void)coop_mode;
    return fake_resume_mode;
}

s32 getMPWeaponSet(void)
{
    return fake_weapon_set;
}

void setMPWeaponSet(s32 weapon_set)
{
    fake_weapon_set = weapon_set;
}

s32 get_players_team_or_scenario_item_flag(s32 player)
{
    return fake_flags[player];
}

void set_players_team_or_scenario_item_flag(s32 player, s32 flag)
{
    fake_flags[player] = flag;
}

void *romCopyAligned(void *target, void *source, s32 length)
{
    if ((uintptr_t)source == (uintptr_t)INDY_RAMROM_DEMO_ADDRESS)
    {
        source = fake_indy_ramrom;
    }
    memcpy(target, source, (size_t)length);
    return target;
}

void romWrite(void *source, void *target, u32 size)
{
    (void)source;
    (void)target;
    (void)size;
    fake_rom_write_calls++;
}

u8 *indycommHostCheckFileExists(char *name, s32 *size)
{
    (void)name;
    *size = fake_file_size;

    if (fake_file_exists_remaining > 0)
    {
        fake_file_exists_remaining--;
        return (u8 *)(uintptr_t)1;
    }

    return NULL;
}

void indycommHostRamRomLoad(char *filename, u8 *target, s32 size)
{
    (void)filename;
    (void)target;
    (void)size;
    fake_ramrom_load_calls++;
}

void indycommHostSaveFile(char *filename, s32 size, u8 *data)
{
    fake_saved_file_calls++;
    strncpy(fake_saved_file_name, filename, sizeof(fake_saved_file_name) - 1);
    fake_saved_file_size = size;
    fake_saved_file_data = data;
}

void set_solo_and_ptr_briefing(LEVELID stage)
{
    fake_selected_stage = stage;
}

void set_selected_difficulty(DIFFICULTY difficulty)
{
    fake_selected_difficulty = difficulty;
}

void frontChangeMenu(MENU menu, s32 reload)
{
    fake_front_menu = menu;
    fake_front_reload = reload;
}

u32 bondviewGetCameraMode(void)
{
    return (u32)fake_camera_mode;
}

void bondviewSetCameraMode(s32 mode)
{
    fake_camera_mode = mode;
    fake_camera_set_calls++;
}

u16 joyGetButtonsPressedThisFrame(s8 controller, u16 mask)
{
    (void)controller;
    return (u16)fake_pressed_buttons & mask;
}

void joySetContDataIndex(s32 index)
{
    fake_cont_data_index = index;
}

void updateFrameCounters(s32 frames)
{
    fake_frame_counter_update = frames;
}

void sub_GAME_7F01D61C(struct save_data *savefile)
{
    memset(savefile, 0x5a, sizeof(*savefile));
}

void joySetRecordFunc(contrecordfunc func)
{
    fake_record_callback = func;
    fake_record_callback_calls++;
}

void joySetPlaybackFunc(contplaybackfunc func, s32 controller_count)
{
    fake_playback_callback = func;
    fake_playback_controller_count = controller_count;
    fake_playback_callback_calls++;
}

void set_selected_foldernum_and_copy_demo_eeprom(struct save_data *savefile)
{
    (void)savefile;
}

static void put_be32(u8 *bytes, size_t offset, u32 value)
{
    bytes[offset + 0] = (u8)(value >> 24);
    bytes[offset + 1] = (u8)(value >> 16);
    bytes[offset + 2] = (u8)(value >> 8);
    bytes[offset + 3] = (u8)value;
}

static void put_be64(u8 *bytes, size_t offset, u64 value)
{
    put_be32(bytes, offset, (u32)(value >> 32));
    put_be32(bytes, offset + 4, (u32)value);
}

static void write_replay_asset(u32 *asset, u32 stage, u32 difficulty)
{
    memset(asset, 0, RAMROM_ASSET_WORDS * sizeof(*asset));
    put_be32((u8 *)asset, offsetof(ramromfilestructure, stagenum), stage);
    put_be32((u8 *)asset, offsetof(ramromfilestructure, difficulty), difficulty);
}

static void GivenIndyReplayHeader_WhenReplayStarts_ThenTheSavedStageIsQueued(void)
{
    /* Arrange */
    reset_fixture();
    put_be32(fake_indy_ramrom, offsetof(ramromfilestructure, stagenum), 19);
    put_be32(fake_indy_ramrom, offsetof(ramromfilestructure, difficulty),
             DIFFICULTY_AGENT);

    /* Act */
    replay_recorded_ramrom_from_indy();

    /* Assert */
    expect_true(ptr_active_demofile != NULL,
                "the Indy replay entry point copies a replay header");
    expect_s32(fake_selected_stage, 19,
               "the Indy replay entry point selects the saved stage");
    expect_s32(fake_selected_difficulty, DIFFICULTY_AGENT,
               "the Indy replay entry point selects the saved difficulty");
    expect_s32(g_ramromPlayBackFlag, 1,
               "the Indy replay entry point arms playback");
    expect_s32(fake_front_menu, MENU_RUN_STAGE,
               "the Indy replay entry point routes to the run-stage menu");
}

static void GivenAttractReplayAssets_WhenAnAttractDemoIsSelected_ThenAReplayIsQueued(void)
{
    /* Arrange */
    reset_fixture();
    write_replay_asset(ramrom_Dam_1, 100, DIFFICULTY_00);
    write_replay_asset(ramrom_Dam_2, 101, DIFFICULTY_00);
    write_replay_asset(ramrom_Facility_1, 102, DIFFICULTY_00);
    write_replay_asset(ramrom_Facility_2, 103, DIFFICULTY_00);
    write_replay_asset(ramrom_Facility_3, 104, DIFFICULTY_00);
    write_replay_asset(ramrom_Runway_1, 105, DIFFICULTY_00);
    write_replay_asset(ramrom_Runway_2, 106, DIFFICULTY_00);
    write_replay_asset(ramrom_BunkerI_1, 107, DIFFICULTY_00);
    write_replay_asset(ramrom_BunkerI_2, 108, DIFFICULTY_00);
    write_replay_asset(ramrom_Silo_1, 109, DIFFICULTY_00);
    write_replay_asset(ramrom_Silo_2, 110, DIFFICULTY_00);
    write_replay_asset(ramrom_Frigate_1, 111, DIFFICULTY_00);
    write_replay_asset(ramrom_Frigate_2, 112, DIFFICULTY_00);
    write_replay_asset(ramrom_Train, 113, DIFFICULTY_00);
    fake_highest_unlocked_stage = (LEVEL_SOLO_SEQUENCE)113;
    fake_random_value = 13;

    /* Act */
    select_ramrom_to_play();

    /* Assert */
    expect_true(fake_selected_stage >= 100 && fake_selected_stage <= 113,
                "attract-demo selection queues one of the available demos");
    expect_s32(fake_selected_stage, 113,
               "attract-demo selection uses the selected random demo");
    expect_s32(fake_selected_difficulty, DIFFICULTY_00,
               "attract-demo selection carries the demo difficulty");
    expect_s32(g_ramromPlayBackFlag, 1,
               "attract-demo selection arms playback");
    expect_s32(fake_front_menu, MENU_RUN_STAGE,
               "attract-demo selection routes to the run-stage menu");
}

static void test_recorded_coop_falls_back_to_one_player_with_one_controller(void)
{
    ramromfilestructure state;

    reset_fixture();
    memset(&state, 0, sizeof(state));
    state.mode = GAMEMODE_COOP;
    state.numplayers = GE_COOP_MAX_PLAYERS;
    fake_controller_count = 1;
    fake_resume_mode = GAMEMODE_SOLO;

    copy_recorded_ramrom_registers_to_proper_place_ingame(&state);

    expect_true(gamemode == GAMEMODE_SOLO,
                "one-controller co-op resume falls back to solo mode");
    expect_s32(selected_num_players, 1,
               "one-controller co-op resume selects one player");
}

static void test_recorded_coop_keeps_two_players_when_two_controllers_exist(void)
{
    ramromfilestructure state;

    reset_fixture();
    memset(&state, 0, sizeof(state));
    state.mode = GAMEMODE_COOP;
    state.numplayers = 1;
    fake_controller_count = GE_COOP_MAX_PLAYERS;
    fake_resume_mode = GAMEMODE_COOP;

    copy_recorded_ramrom_registers_to_proper_place_ingame(&state);

    expect_true(gamemode == GAMEMODE_COOP,
                "two-controller co-op resume keeps co-op mode");
    expect_s32(selected_num_players, GE_COOP_MAX_PLAYERS,
               "co-op resume enforces its two-player selection");
}

static void test_resume_restores_the_saved_options_and_random_state(void)
{
    ramromfilestructure state;
    s32 i;

    reset_fixture();
    memset(&state, 0, sizeof(state));
    state.randomseed = UINT64_C(0x1122334455667788);
    state.randomizer = UINT64_C(0x8877665544332211);
    state.mode = GAMEMODE_MULTI;
    state.numplayers = 3;
    state.scenario = SCENARIO_2v2;
    state.mpstage_sel = 7;
    state.gamelength = 4;
    state.mp_weapon_set = 12;
    state.aim_option = 2;
    for (i = 0; i < MAX_MULTIPLAYER; i++)
    {
        state.mp_char[i] = 100 + (u32)i;
        state.mp_handi[i] = 200 + (u32)i;
        state.mp_contstyle[i] = 300 + (u32)i;
        state.mp_flags[i] = 400 + (u32)i;
    }
    fake_resume_mode = GAMEMODE_MULTI;

    copy_recorded_ramrom_registers_to_proper_place_ingame(&state);

    expect_u64(g_randomSeed, state.randomseed,
               "resume restores the gameplay random seed");
    expect_u64(g_chrObjRandomSeed, state.randomizer,
               "resume restores the actor random seed");
    expect_s32(gamemode, GAMEMODE_MULTI,
               "resume restores deathmatch mode");
    expect_s32(selected_num_players, 3,
               "resume preserves the saved deathmatch player count");
    expect_s32(scenario, SCENARIO_2v2,
               "resume restores the multiplayer scenario");
    expect_s32(MP_stage_selected, 7,
               "resume restores the multiplayer stage selection");
    expect_s32(game_length, 4,
               "resume restores the game length");
    expect_s32(fake_weapon_set, 12,
               "resume restores the weapon set");
    expect_s32(aim_sight_adjustment, 2,
               "resume restores the aim option");

    for (i = 0; i < MAX_MULTIPLAYER; i++)
    {
        expect_s32(player_char[i], 100 + i,
                   "resume restores each multiplayer character");
        expect_s32(player_handicap[i], 200 + i,
                   "resume restores each multiplayer handicap");
        expect_s32(controlstyle_player[i], 300 + i,
                   "resume restores each controller style");
        expect_s32(fake_flags[i], 400 + i,
                   "resume restores each team or item flag");
    }
}

static void test_current_options_are_serialized_for_resume(void)
{
    ramromfilestructure state;
    s32 i;

    reset_fixture();
    memset(&state, 0, sizeof(state));
    g_randomSeed = UINT64_C(0x0102030405060708);
    g_chrObjRandomSeed = UINT64_C(0x0807060504030201);
    gamemode = GAMEMODE_COOP;
    selected_num_players = 2;
    scenario = SCENARIO_TLD;
    MP_stage_selected = 5;
    game_length = 6;
    aim_sight_adjustment = 3;
    fake_weapon_set = 9;
    for (i = 0; i < MAX_MULTIPLAYER; i++)
    {
        player_char[i] = 10 + i;
        player_handicap[i] = 20 + i;
        controlstyle_player[i] = 30 + i;
        fake_flags[i] = 40 + i;
    }

    copy_current_ingame_registers_before_ramrom_playback(&state);

    expect_u64(state.randomseed, g_randomSeed,
               "save captures the gameplay random seed");
    expect_u64(state.randomizer, g_chrObjRandomSeed,
               "save captures the actor random seed");
    expect_s32(state.mode, GAMEMODE_COOP,
               "save captures the current game mode");
    expect_s32(state.numplayers, 2,
               "save captures the current player count");
    expect_s32(state.scenario, SCENARIO_TLD,
               "save captures the current scenario");
    expect_s32(state.mpstage_sel, 5,
               "save captures the current stage selection");
    expect_s32(state.gamelength, 6,
               "save captures the current game length");
    expect_s32(state.mp_weapon_set, 9,
               "save captures the current weapon set");
    expect_s32(state.aim_option, 3,
               "save captures the current aim option");
    for (i = 0; i < MAX_MULTIPLAYER; i++)
    {
        expect_s32(state.mp_char[i], 10 + i,
                   "save captures each multiplayer character");
        expect_s32(state.mp_handi[i], 20 + i,
                   "save captures each multiplayer handicap");
        expect_s32(state.mp_contstyle[i], 30 + i,
                   "save captures each controller style");
        expect_s32(state.mp_flags[i], 40 + i,
                   "save captures each team or item flag");
    }
}

static void test_replay_flags_and_block_headings_reset_cleanly(void)
{
    reset_fixture();

    setRamRomRecordSlot(3);
    expect_s32(g_ramromRecordFlag, 1,
               "setting a replay slot arms recording");
    expect_s32(get_recording_ramrom_flag(), 0,
               "arming recording does not claim an active recording yet");

    g_ramromPlayBackFlag = 1;
    expect_s32(interface_menu0B_runstage(), 1,
               "replay interface reports a pending playback");

    clear_ramrom_block_buffer_heading_ptrs();
    expect_true(ptr_active_demofile == NULL,
                "clearing replay headings clears the active demo pointer");
}

static void test_replay_header_is_byte_swapped_before_menu_transition(void)
{
    u8 encoded[sizeof(ramromfilestructure)];
    ramromfilestructure *raw = (ramromfilestructure *)encoded;

    reset_fixture();
    memset(encoded, 0, sizeof(encoded));
    put_be64(encoded, offsetof(ramromfilestructure, randomseed),
             UINT64_C(0x1122334455667788));
    put_be64(encoded, offsetof(ramromfilestructure, randomizer),
             UINT64_C(0x8877665544332211));
    put_be32(encoded, offsetof(ramromfilestructure, stagenum), 17);
    put_be32(encoded, offsetof(ramromfilestructure, difficulty), DIFFICULTY_00);
    put_be32(encoded, offsetof(ramromfilestructure, size_cmds), 2);
    put_be32(encoded, offsetof(ramromfilestructure, mode), GAMEMODE_MULTI);
    put_be32(encoded, offsetof(ramromfilestructure, slotnum), 3);
    put_be32(encoded, offsetof(ramromfilestructure, numplayers), 2);
    put_be32(encoded, offsetof(ramromfilestructure, scenario), SCENARIO_TLD);

    replay_recorded_ramrom_at_address(raw);

    expect_u64(ptr_active_demofile->randomseed, UINT64_C(0x1122334455667788),
               "replay header restores the big-endian gameplay seed");
    expect_u64(ptr_active_demofile->randomizer, UINT64_C(0x8877665544332211),
               "replay header restores the big-endian actor seed");
    expect_s32(ptr_active_demofile->stagenum, 17,
               "replay header restores the stage number");
    expect_s32(ptr_active_demofile->difficulty, DIFFICULTY_00,
               "replay header restores the difficulty");
    expect_s32(ptr_active_demofile->size_cmds, 2,
               "replay header restores the controller count");
    expect_s32(ptr_active_demofile->mode, GAMEMODE_MULTI,
               "replay header restores the saved mode");
    expect_s32(ptr_active_demofile->numplayers, 2,
               "replay header restores the saved player count");
    expect_s32(ptr_active_demofile->scenario, SCENARIO_TLD,
               "replay header restores the saved scenario");
    expect_s32(g_ramromPlayBackFlag, 1,
               "replay header arms playback before running the stage");
    expect_s32(fake_selected_stage, 17,
               "replay header selects the saved stage briefing");
    expect_s32(fake_selected_difficulty, DIFFICULTY_00,
               "replay header selects the saved difficulty");
    expect_s32(fake_front_menu, MENU_RUN_STAGE,
               "replay header routes to the run-stage menu");
    expect_s32(fake_front_reload, 1,
               "replay header requests a run-stage reload");
}

static void test_replay_fade_request_is_idempotent(void)
{
    reset_fixture();

    ramromFadeToTitle();
    expect_s32(fake_camera_mode, CAMERAMODE_FADE_TO_TITLE,
               "replay failure requests a fade to title");
    expect_s32(fake_camera_set_calls, 1,
               "replay failure changes the camera mode once");

    ramromFadeToTitle();
    expect_s32(fake_camera_set_calls, 1,
               "repeated replay failure does not resubmit the fade transition");
}

static void test_replay_activity_flag_reports_the_active_slot(void)
{
    ramromfilestructure state;

    reset_fixture();
    memset(&state, 0, sizeof(state));
    state.slotnum = 9;
    ptr_active_demofile = &state;

    expect_s32(check_ramrom_flags(), 0,
               "inactive replay reports no slot");

    is_ramrom_flag = 1;
    expect_s32(check_ramrom_flags(), 9,
               "playback activity reports the active replay slot");

    is_ramrom_flag = 0;
    recording_ramrom_flag = 1;
    expect_s32(check_ramrom_flags(), 9,
               "recording activity reports the active replay slot");
}

struct test_ramrom_seed
{
    u8 speedframes;
    u8 count;
    u8 randseed;
    u8 check;
};

struct test_ramrom_block
{
    s8 stick_x;
    s8 stick_y;
    u8 button_low;
    u8 button_high;
};

static struct test_ramrom_seed *recorded_seed_storage(void)
{
    uintptr_t address = (uintptr_t)(ramrom_data_target + 0x1f8);
    address = (address + 0xfu) & ~(uintptr_t)0xfu;
    return (struct test_ramrom_seed *)address;
}

static void test_record_and_playback_round_trip_controller_packets(void)
{
    struct contsample input;
    struct contsample output;
    ramromfilestructure state;
    u8 demo_storage[64];
    struct test_ramrom_seed *seed;
    struct test_ramrom_block *blocks;

    reset_fixture();
    memset(&input, 0, sizeof(input));
    memset(&output, 0, sizeof(output));
    memset(&state, 0, sizeof(state));
    memset(demo_storage, 0, sizeof(demo_storage));
    state.size_cmds = 2;
    ptr_active_demofile = &state;
    address_demo_loaded = demo_storage;
    g_randomSeed = 5;
    speedgraphframes = 3;

    input.pads[4].stick_x = 12;
    input.pads[4].stick_y = -7;
    input.pads[4].button = 0x1234;
    input.pads[5].stick_x = -9;
    input.pads[5].stick_y = 8;
    input.pads[5].button = 0x00a5;

    record_player_input_as_packet(&input, 0, 1);

    seed = recorded_seed_storage();
    blocks = (struct test_ramrom_block *)(seed + 1);
    expect_s32(seed->count, 1,
               "recording stores one packet for one input interval");
    expect_s32(seed->speedframes, 3,
               "recording stores the current frame step");
    expect_s32(seed->randseed, 5,
               "recording stores the current random seed byte");
    expect_s32(blocks[0].stick_x, 12,
               "recording stores controller one horizontal input");
    expect_s32(blocks[0].stick_y, -7,
               "recording stores controller one vertical input");
    expect_s32((blocks[0].button_high << 8) | blocks[0].button_low, 0x1234,
               "recording stores controller one button word");
    expect_s32((blocks[1].button_high << 8) | blocks[1].button_low, 0x00a5,
               "recording stores controller two button word");

    fake_pressed_buttons = 0;
    expect_s32(ramrom_replay_handler(&output, 0), 1,
               "playback advances to the recorded sample slot");
    expect_s32(output.pads[4].stick_x, 12,
               "playback restores controller one horizontal input");
    expect_s32(output.pads[4].stick_y, -7,
               "playback restores controller one vertical input");
    expect_s32(output.pads[4].button, 0x1234,
               "playback restores controller one button word");
    expect_s32(output.pads[5].button, 0x00a5,
               "playback restores controller two button word");
    expect_s32(output.pads[6].button, 0,
               "playback clears an unused controller slot");
    expect_s32(fake_cont_data_index, 1,
               "playback leaves the second controller data index selected");

    fake_pressed_buttons = 1;
    fake_camera_mode = CAMERAMODE_FP;
    prev_keypresses = 0;
    ramrom_replay_handler(&output, 0);
    expect_s32(fake_camera_mode, CAMERAMODE_FADE_TO_TITLE,
               "user input exits replay to the title screen");
    expect_s32(prev_keypresses, 1,
               "user input during replay records the previous keypress");

    fake_pressed_buttons = 0;
    g_randomSeed = 9;
    fake_camera_mode = CAMERAMODE_FP;
    ramrom_replay_handler(&output, 0);
    expect_s32(fake_camera_mode, CAMERAMODE_FADE_TO_TITLE,
               "a changed random seed invalidates replay playback");

    g_randomSeed = 5;
    seed->check ^= 1;
    fake_camera_mode = CAMERAMODE_FP;
    ramrom_replay_handler(&output, 0);
    expect_s32(fake_camera_mode, CAMERAMODE_FADE_TO_TITLE,
               "a changed replay checksum invalidates playback");

    record_player_input_as_packet(&input, 0, 3);
    expect_true(recorded_seed_storage()->count > 1,
               "recording can consume two input intervals before wrapping");
    record_player_input_as_packet(&input, 1, 1);
    expect_s32(recorded_seed_storage()->count, 0,
               "an empty input interval records no packets");
}

static void test_iterating_replay_entries_copies_packets_and_handles_empty_entries(void)
{
    u8 demo_storage[32];
    struct test_ramrom_seed *seed = (struct test_ramrom_seed *)demo_storage;
    struct test_ramrom_block *blocks =
        (struct test_ramrom_block *)(demo_storage + sizeof(*seed));
    ramromfilestructure state;
    u8 *end_of_first_entry;

    reset_fixture();
    memset(demo_storage, 0, sizeof(demo_storage));
    memset(&state, 0, sizeof(state));
    state.size_cmds = 2;
    state.totaltime_ms = 100;
    ptr_active_demofile = &state;
    address_demo_loaded = demo_storage;
    g_GlobalTimer = 0;
    g_ClockTimer = 0;
    seed->count = 1;
    seed->speedframes = 4;
    seed->randseed = 7;
    blocks[0].stick_x = 3;
    blocks[0].button_low = 0x45;
    blocks[1].stick_y = -4;
    blocks[1].button_high = 0x12;
    seed->check = (u8)(seed->speedframes + seed->count + seed->randseed
                       + (u8)blocks[0].stick_x + (u8)blocks[0].stick_y
                       + blocks[0].button_low + blocks[0].button_high
                       + (u8)blocks[1].stick_x + (u8)blocks[1].stick_y
                       + blocks[1].button_low + blocks[1].button_high);
    end_of_first_entry = address_demo_loaded + 12;

    iterate_ramrom_entries_handle_camera_out();

    expect_s32(((struct test_ramrom_seed *)(ramrom_data_target + 0x1f8))->count,
               1, "replay iteration copies the packet count");
    expect_s32(((struct test_ramrom_block *)(ramrom_data_target + 0x21e))[0].button_low,
               0x45, "replay iteration copies controller one packet data");
    expect_s32(fake_frame_counter_update, 4,
               "replay iteration updates the frame counter step");
    expect_true(address_demo_loaded == end_of_first_entry,
                "replay iteration advances by the aligned entry size");

    reset_fixture();
    memset(demo_storage, 0, sizeof(demo_storage));
    memset(&state, 0, sizeof(state));
    state.size_cmds = 2;
    state.totaltime_ms = 100;
    ptr_active_demofile = &state;
    address_demo_loaded = demo_storage;
    seed = (struct test_ramrom_seed *)demo_storage;
    seed->count = 0;
    seed->speedframes = 0;
    iterate_ramrom_entries_handle_camera_out();
    expect_s32(fake_camera_mode, CAMERAMODE_FADE_TO_TITLE,
               "empty replay entry with no frame step fades to title");

    reset_fixture();
    memset(demo_storage, 0, sizeof(demo_storage));
    memset(&state, 0, sizeof(state));
    state.size_cmds = 2;
    state.totaltime_ms = 100;
    ptr_active_demofile = &state;
    address_demo_loaded = demo_storage;
    seed = (struct test_ramrom_seed *)demo_storage;
    seed->count = 0;
    seed->speedframes = 3;
    iterate_ramrom_entries_handle_camera_out();
    expect_s32(fake_camera_mode, CAMERAMODE_FP,
               "empty replay entry with a frame step does not fade to title");
    expect_s32(fake_frame_counter_update, 3,
               "empty replay entry still updates its frame step");
    expect_s32((s32)(address_demo_loaded - demo_storage), 4,
               "empty replay entry advances by its aligned header size");

    reset_fixture();
    memset(demo_storage, 0, sizeof(demo_storage));
    memset(&state, 0, sizeof(state));
    state.size_cmds = 2;
    state.totaltime_ms = 100;
    ptr_active_demofile = &state;
    address_demo_loaded = demo_storage;
    seed = (struct test_ramrom_seed *)demo_storage;
    seed->count = 1;
    seed->speedframes = 2;
    seed->randseed = 1;
    seed->check = 4;
    g_GlobalTimer = 40;
    g_ClockTimer = 1;
    iterate_ramrom_entries_handle_camera_out();
    expect_s32(fake_camera_mode, CAMERAMODE_FADE_TO_TITLE,
               "replay timing at the saved end point fades to title");
}

static void test_devtool_and_recording_stop_contracts(void)
{
    ramromfilestructure state;

    reset_fixture();
    memset(&state, 0, sizeof(state));
    state.filesize = 0x1234;
    ptr_active_demofile = &state;
    fake_file_exists_remaining = 2;
    save_ramrom_to_devtool();
    expect_s32(fake_saved_file_calls, 1,
               "replay save writes exactly one non-conflicting devtool file");
    expect_true(strcmp(fake_saved_file_name, "replay/demo.3") == 0,
                "replay save advances past existing devtool files");
    expect_s32(fake_saved_file_size, state.filesize,
               "replay save passes the demo size as the file size");
    expect_true(fake_saved_file_data == (u8 *)INDY_RAMROM_DEMO_POINTER,
                "replay save passes the demo address as the file data");

    reset_fixture();
    fake_file_exists_remaining = 1;
    fake_file_size = 0x40;
    fake_indy_ramrom[0] = 0x5a;
    load_ramrom_from_devtool();
    expect_s32(fake_ramrom_load_calls, 1,
               "replay load requests the available devtool demo");
    expect_true(ptr_active_demofile != NULL && ramrom_data_target[0] == 0x5a,
                "replay load copies the devtool header into the active buffer");

    reset_fixture();
    address_demo_loaded = (u8 *)(uintptr_t)INDY_RAMROM_DEMO_ADDRESS;
    ramrom_demo_related_6 = 1;
    recording_ramrom_flag = 1;
    stop_recording_ramrom();
    expect_s32(ramrom_demo_related_6, 0,
               "stopping an active recording clears its finalization flag");
    expect_s32(recording_ramrom_flag, 0,
               "stopping an active recording clears the recording state");
    expect_true(fake_record_callback == NULL,
                "stopping an active recording unregisters its callback");
    expect_true(ptr_active_demofile != NULL && fake_rom_write_calls == 2,
                "recording finalization writes the terminator and refreshed header");
}

static void GivenDevtoolReplayIsMissing_WhenReplayLoadIsRequested_ThenPlaybackStateStaysUntouched(void)
{
    /* Arrange */
    reset_fixture();

    /* Act */
    load_ramrom_from_devtool();

    /* Assert */
    expect_s32(fake_ramrom_load_calls, 0,
               "a missing devtool replay is not loaded");
    expect_true(ptr_active_demofile == NULL,
                "a missing devtool replay does not create an active demo");
}

static void test_stage_load_sets_up_recording_and_playback_callbacks(void)
{
    ramromfilestructure playback_state;

    reset_fixture();
    g_ramromRecordFlag = 1;
    record_slot_num = 6;
    test_if_recording_demos_this_stage_load((LEVELID)12, DIFFICULTY_AGENT);

    expect_s32(recording_ramrom_flag, 1,
               "stage load arms recording when the record flag is set");
    expect_s32(g_ramromRecordFlag, 0,
               "stage load consumes the record request");
    expect_s32(ptr_active_demofile->stagenum, 12,
               "recording header stores the stage number");
    expect_s32(ptr_active_demofile->difficulty, DIFFICULTY_AGENT,
               "recording header stores the difficulty");
    expect_s32(ptr_active_demofile->size_cmds, fake_controller_count,
               "recording header stores the connected controller count");
    expect_s32(ptr_active_demofile->slotnum, 6,
               "recording header stores the selected replay slot");
    expect_true(fake_record_callback != NULL && fake_record_callback_calls == 1,
                "stage load installs the recording callback exactly once");

    reset_fixture();
    memset(&playback_state, 0, sizeof(playback_state));
    playback_state.mode = GAMEMODE_MULTI;
    playback_state.numplayers = 3;
    playback_state.stagenum = (LEVELID)21;
    playback_state.difficulty = DIFFICULTY_007;
    playback_state.size_cmds = 2;
    ptr_active_demofile = &playback_state;
    g_ramromPlayBackFlag = 1;
    test_if_recording_demos_this_stage_load((LEVELID)0, DIFFICULTY_AGENT);

    expect_s32(g_ramromPlayBackFlag, 0,
               "stage load consumes the playback request");
    expect_s32(is_ramrom_flag, 1,
               "stage load marks playback as active");
    expect_s32(ramrom_demo_related_3, 1,
               "stage load marks the active playback state");
    expect_true(fake_playback_callback != NULL && fake_playback_callback_calls == 1,
                "stage load installs the playback callback exactly once");
    expect_s32(fake_playback_controller_count, 2,
               "playback callback receives the recorded controller count");
    expect_s32(fake_cont_data_index, 1,
               "playback starts from the secondary controller data index");
}

static void test_stop_demo_playback_restores_state_and_clears_callbacks(void)
{
    ramromfilestructure *saved_state;

    reset_fixture();
    stop_recording_ramrom();
    expect_s32(recording_ramrom_flag, 0,
               "stopping an inactive recording leaves recording state clear");
    stop_demo_playback();

    expect_s32(ramrom_demo_related_3, 0,
               "stopping an inactive replay leaves playback state clear");

    reset_fixture();
    saved_state = (ramromfilestructure *)(ramrom_data_target + 0x110);
    memset(saved_state, 0, sizeof(*saved_state));
    saved_state->mode = GAMEMODE_MULTI;
    saved_state->numplayers = 3;
    saved_state->mp_weapon_set = 4;
    fake_resume_mode = GAMEMODE_MULTI;
    ramrom_demo_related_3 = 1;
    is_ramrom_flag = 1;

    stop_demo_playback();

    expect_s32(ramrom_demo_related_3, 0,
               "stopping playback clears the active playback state");
    expect_s32(is_ramrom_flag, 0,
               "stopping playback clears the replay flag");
    expect_true(fake_playback_callback == NULL &&
                    fake_playback_controller_count == -1,
                "stopping playback unregisters the playback callback");
    expect_s32(fake_cont_data_index, 0,
               "stopping playback returns to live controller data");
}

static void GivenRecordingIsActive_WhenStopDemoPlaybackIsRequested_ThenRecordingIsFinalized(void)
{
    /* Arrange */
    reset_fixture();
    address_demo_loaded = (u8 *)(uintptr_t)INDY_RAMROM_DEMO_ADDRESS;
    ramrom_demo_related_6 = 1;
    ramrom_demo_related_3 = 1;
    recording_ramrom_flag = 1;

    /* Act */
    stop_demo_playback();

    /* Assert */
    expect_s32(ramrom_demo_related_6, 0,
               "an active recording is finalized before playback shutdown");
    expect_s32(recording_ramrom_flag, 0,
               "playback shutdown clears the active recording state");
    expect_true(fake_record_callback == NULL,
                "playback shutdown unregisters the recording callback");
    expect_s32(ramrom_demo_related_3, 1,
               "playback state is left untouched when recording owns the device");
}

int main(void)
{
    test_recorded_coop_falls_back_to_one_player_with_one_controller();
    test_recorded_coop_keeps_two_players_when_two_controllers_exist();
    test_resume_restores_the_saved_options_and_random_state();
    test_current_options_are_serialized_for_resume();
    test_replay_flags_and_block_headings_reset_cleanly();
    test_replay_header_is_byte_swapped_before_menu_transition();
    GivenIndyReplayHeader_WhenReplayStarts_ThenTheSavedStageIsQueued();
    GivenAttractReplayAssets_WhenAnAttractDemoIsSelected_ThenAReplayIsQueued();
    test_replay_fade_request_is_idempotent();
    test_replay_activity_flag_reports_the_active_slot();
    test_record_and_playback_round_trip_controller_packets();
    test_iterating_replay_entries_copies_packets_and_handles_empty_entries();
    test_devtool_and_recording_stop_contracts();
    GivenDevtoolReplayIsMissing_WhenReplayLoadIsRequested_ThenPlaybackStateStaysUntouched();
    test_stage_load_sets_up_recording_and_playback_callbacks();
    test_stop_demo_playback_restores_state_and_clears_callbacks();
    GivenRecordingIsActive_WhenStopDemoPlaybackIsRequested_ThenRecordingIsFinalized();

    expect_true(failures == 0, "all RAM/ROM resume tests pass");
    return failures == 0 ? 0 : 1;
}
