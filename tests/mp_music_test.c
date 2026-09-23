#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include <bondconstants.h>
#include <music.h>

#include "mp_music.h"

extern void sub_GAME_7F0C1288(void);
extern void sub_GAME_7F0C12CC(void);
extern void sub_GAME_7F0C1310(void);
extern void set_missionstate_zero(void);
extern void sub_GAME_7F0C1364(void);
extern u16 sub_GAME_7F0C0C10(void);

extern s32 stageMusicID;
extern s32 dword_CODE_bss_8008C604;
extern s32 music_slot_active_0[4];
extern s32 music_slot_minutes_0[4];
extern s32 music_slot_seconds_0[4];
extern s32 mission_state;
extern s32 g_ClockTimer;

s32 g_musicXTrack1Fade;
s32 g_musicXTrack2Fade;
s32 g_musicXTrack3Fade;
s32 g_ClockTimer;

static int failures;
static u16 fake_track2_volume = 0x2345;
static u16 fake_sfx_volume = 0x3456;
static s32 fake_background_track = -1;
static s32 fake_x_track = 0x2a;
static s32 fake_track1 = 0x16;

struct music_call_log
{
    int track1_stop;
    int track2_stop;
    int track3_stop;
    int track1_play;
    int track2_play;
    int track3_play;
    s32 track1_play_track;
    s32 track2_play_track;
    s32 track3_play_track;
    int track1_apply;
    int track2_apply;
    int track3_apply;
    u16 track1_apply_volume;
    u16 track2_apply_volume;
    u16 track3_apply_volume;
    int track1_fade_out;
    int track2_fade_out;
    int track3_fade_out;
    f32 track1_fade_out_time;
    f32 track2_fade_out_time;
    f32 track3_fade_out_time;
    int track1_fade_in;
    int track2_fade_in;
    int track3_fade_in;
    f32 track1_fade_in_time;
    f32 track2_fade_in_time;
    f32 track3_fade_in_time;
    u16 track1_fade_in_volume;
    u16 track2_fade_in_volume;
    u16 track3_fade_in_volume;
    int background_queries;
    int x_track_queries;
    int track1_queries;
    s32 last_background_stage;
    s32 last_x_stage;
    s32 last_track1_stage;
};

static struct music_call_log calls;

static void expect_s32(s32 actual, s32 expected, const char *message)
{
    if (actual != expected)
    {
        fprintf(stderr, "FAIL: %s (got %d, expected %d)\n",
                message, (int)actual, (int)expected);
        failures++;
    }
}

static void expect_u16(u16 actual, u16 expected, const char *message)
{
    if (actual != expected)
    {
        fprintf(stderr, "FAIL: %s (got %u, expected %u)\n",
                message, (unsigned)actual, (unsigned)expected);
        failures++;
    }
}

static void expect_f32(f32 actual, f32 expected, const char *message)
{
    if (actual != expected)
    {
        fprintf(stderr, "FAIL: %s (got %.3f, expected %.3f)\n",
                message, (double)actual, (double)expected);
        failures++;
    }
}

static void reset_call_log(void)
{
    memset(&calls, 0, sizeof(calls));
    g_musicXTrack1Fade = 91;
    g_musicXTrack2Fade = 92;
    g_musicXTrack3Fade = 93;
}

u16 get_mTrack2Vol(void)
{
    return fake_track2_volume;
}

u16 call_sndGetSfxSlotFirstNaturalVolume(void)
{
    return fake_sfx_volume;
}

void geTracePrintf(const char *path, const char *format, ...)
{
    (void)path;
    (void)format;
}

void musicTrack1Stop(void)
{
    calls.track1_stop++;
}

void musicTrack2Stop(void)
{
    calls.track2_stop++;
}

void musicTrack3Stop(void)
{
    calls.track3_stop++;
}

void musicTrack1Play(s32 track)
{
    calls.track1_play++;
    calls.track1_play_track = track;
}

void musicTrack2Play(s32 track)
{
    calls.track2_play++;
    calls.track2_play_track = track;
}

void musicTrack3Play(s32 track)
{
    calls.track3_play++;
    calls.track3_play_track = track;
}

void musicTrack1ApplySeqpVol(u16 volume)
{
    calls.track1_apply++;
    calls.track1_apply_volume = volume;
}

void musicTrack2ApplySeqpVol(u16 volume)
{
    calls.track2_apply++;
    calls.track2_apply_volume = volume;
}

void musicTrack3ApplySeqpVol(u16 volume)
{
    calls.track3_apply++;
    calls.track3_apply_volume = volume;
}

void musicTrack1FadeOut(f32 fade_time)
{
    calls.track1_fade_out++;
    calls.track1_fade_out_time = fade_time;
}

void musicTrack2FadeOut(f32 fade_time)
{
    calls.track2_fade_out++;
    calls.track2_fade_out_time = fade_time;
}

void musicTrack3FadeOut(f32 fade_time)
{
    calls.track3_fade_out++;
    calls.track3_fade_out_time = fade_time;
}

void musicTrack1FadeIn(f32 fade_time, u16 volume)
{
    calls.track1_fade_in++;
    calls.track1_fade_in_time = fade_time;
    calls.track1_fade_in_volume = volume;
}

void musicTrack2FadeIn(f32 fade_time, u16 volume)
{
    calls.track2_fade_in++;
    calls.track2_fade_in_time = fade_time;
    calls.track2_fade_in_volume = volume;
}

void musicTrack3FadeIn(f32 fade_time, u16 volume)
{
    calls.track3_fade_in++;
    calls.track3_fade_in_time = fade_time;
    calls.track3_fade_in_volume = volume;
}

s32 musicGetBgTrackForStage(s32 stage)
{
    calls.background_queries++;
    calls.last_background_stage = stage;
    return fake_background_track;
}

s32 musicGetXTrackForStage(s32 stage)
{
    calls.x_track_queries++;
    calls.last_x_stage = stage;
    return fake_x_track;
}

s32 getmusictrack_or_randomtrack(s32 stage)
{
    calls.track1_queries++;
    calls.last_track1_stage = stage;
    return fake_track1;
}

static void test_volume_wrappers(void)
{
    fake_track2_volume = 0x2345;
    fake_sfx_volume = 0x3456;
    expect_u16(sub_GAME_7F0C0BF0(), fake_track2_volume,
               "track-two volume wrapper returns the audio volume");
    expect_u16(sub_GAME_7F0C0C10(), fake_sfx_volume,
               "SFX volume wrapper returns the natural slot volume");
}

static void test_music_slot_lifecycle(void)
{
    s32 i;

    sub_GAME_7F0C1364();
    for (i = 0; i < 4; ++i)
    {
        expect_s32(music_slot_active_0[i], 0,
                   "slot reset clears active state");
        expect_s32(music_slot_minutes_0[i], 0,
                   "slot reset clears minutes");
        expect_s32(music_slot_seconds_0[i], 0,
                   "slot reset clears seconds");
    }

    musicPlaySlot(2, 2, 3);
    expect_s32(music_slot_active_0[2], 1,
               "playing a free slot marks it active");
    expect_s32(music_slot_minutes_0[2], 120,
               "NTSC slot minutes are converted to frames");
    expect_s32(music_slot_seconds_0[2], 180,
               "NTSC slot seconds are converted to frames");

    musicPlaySlot(2, 9, 9);
    expect_s32(music_slot_minutes_0[2], 120,
               "playing an active slot does not overwrite its countdown");
    expect_s32(music_slot_seconds_0[2], 180,
               "playing an active slot preserves its seconds");

    musicStopSlot(2);
    expect_s32(music_slot_active_0[2], 0,
               "stopping one slot clears only its active flag");
    expect_s32(music_slot_minutes_0[2], 120,
               "stopping one slot preserves its countdown data");

    musicPlaySlot(0, 1, 1);
    musicPlaySlot(1, 1, 1);
    musicPlaySlot(3, 1, 1);
    musicStopSlot(-1);
    for (i = 0; i < 4; ++i)
    {
        expect_s32(music_slot_active_0[i], 0,
                   "stopping all slots clears every active flag");
        expect_s32(music_slot_minutes_0[i], 0,
                   "stopping all slots clears every minute countdown");
        expect_s32(music_slot_seconds_0[i], 0,
                   "stopping all slots clears every second countdown");
    }
}

static void test_state_audio_actions(void)
{
    mission_state = MISSION_STATE_0;
    stageMusicID = 7;
    fake_background_track = 0x31;
    fake_x_track = 0x32;
    fake_track1 = 0x33;

    reset_call_log();
    set_missionstate(MISSION_STATE_1);
    expect_s32(get_mission_state(), MISSION_STATE_1,
               "state one transition updates mission state");
    expect_s32(calls.track1_apply, 1,
                "state one applies track-one volume");
    expect_u16(calls.track1_apply_volume, fake_track2_volume,
               "state one uses the returned track-two volume");
    expect_s32(calls.track1_play_track, fake_track1,
               "state one starts the selected track-one sequence");
    expect_s32(calls.last_track1_stage, stageMusicID,
               "state one selects music using the current stage");

    mission_state = MISSION_STATE_0;
    reset_call_log();
    set_missionstate(MISSION_STATE_4);
    expect_s32(calls.track1_apply_volume, fake_track2_volume,
               "state four keeps the track-one volume source");
    expect_s32(calls.track3_apply_volume, fake_sfx_volume,
               "state four applies the natural SFX volume to track three");
    expect_s32(calls.track3_play_track, fake_background_track,
               "state four starts the stage background track");

    mission_state = MISSION_STATE_1;
    reset_call_log();
    set_missionstate(MISSION_STATE_2);
    expect_s32(calls.track2_apply_volume, fake_track2_volume,
               "state one to two applies the track-two volume");
    expect_s32(calls.track2_play_track, fake_x_track,
               "state one to two starts the stage X track");
    expect_f32(calls.track1_fade_out_time, 0.5f,
               "state one to two fades out track one over half a second");

    mission_state = MISSION_STATE_1;
    reset_call_log();
    set_missionstate(MISSION_STATE_3);
    expect_s32(calls.track2_play_track, 0x18,
               "state one to three starts the alert track");
    expect_f32(calls.track1_fade_out_time, 0.5f,
               "state one to three fades out track one");

    mission_state = MISSION_STATE_1;
    reset_call_log();
    set_missionstate(MISSION_STATE_6);
    expect_s32(calls.track2_play_track, 0x3a,
               "state one to six starts the death track");
    expect_f32(calls.track1_fade_out_time, 0.02f,
               "state one to six performs the short track-one fade");

    mission_state = MISSION_STATE_2;
    reset_call_log();
    set_missionstate(MISSION_STATE_1);
    expect_f32(calls.track1_fade_in_time, 0.5f,
               "state two to one fades track one in");
    expect_u16(calls.track1_fade_in_volume, fake_track2_volume,
               "state two to one uses the track-one volume wrapper");
    expect_f32(calls.track2_fade_out_time, 0.5f,
               "state two to one fades track two out");

    mission_state = MISSION_STATE_2;
    reset_call_log();
    set_missionstate(MISSION_STATE_3);
    expect_s32(calls.track2_play_track, 0x18,
               "state two to three starts the alert track");

    mission_state = MISSION_STATE_3;
    reset_call_log();
    set_missionstate(MISSION_STATE_4);
    expect_f32(calls.track1_fade_in_time, 1.0f,
               "state three to four fades track one in");
    expect_f32(calls.track3_fade_in_time, 1.0f,
               "state three to four fades track three in");
    expect_f32(calls.track2_fade_out_time, 1.0f,
               "state three to four fades track two out");

    mission_state = MISSION_STATE_3;
    reset_call_log();
    set_missionstate(MISSION_STATE_5);
    expect_s32(calls.track2_play_track, fake_x_track,
               "state three to five starts the stage X track");
    expect_u16(calls.track3_fade_in_volume, fake_sfx_volume,
               "state three to five fades track three to the SFX volume");

    mission_state = MISSION_STATE_4;
    reset_call_log();
    set_missionstate(MISSION_STATE_3);
    expect_s32(calls.track2_play_track, 0x18,
               "state four to three starts the alert track");
    expect_f32(calls.track3_fade_out_time, 0.5f,
               "state four to three fades track three out");

    mission_state = MISSION_STATE_4;
    reset_call_log();
    set_missionstate(MISSION_STATE_5);
    expect_s32(calls.track2_play_track, fake_x_track,
               "state four to five starts the stage X track");
    expect_f32(calls.track1_fade_out_time, 0.5f,
               "state four to five fades track one out");

    mission_state = MISSION_STATE_5;
    reset_call_log();
    set_missionstate(MISSION_STATE_3);
    expect_s32(calls.track2_play_track, 0x18,
               "state five to three starts the alert track");
    expect_f32(calls.track3_fade_out_time, 0.5f,
               "state five to three fades track three out");

    mission_state = MISSION_STATE_5;
    reset_call_log();
    set_missionstate(MISSION_STATE_4);
    expect_f32(calls.track1_fade_in_time, 0.5f,
               "state five to four fades track one in");
    expect_f32(calls.track2_fade_out_time, 0.5f,
               "state five to four fades track two out");

    mission_state = MISSION_STATE_6;
    reset_call_log();
    set_missionstate(MISSION_STATE_1);
    expect_f32(calls.track1_fade_in_time, 2.0f,
               "state six to one uses the long track-one fade");
    expect_f32(calls.track2_fade_out_time, 2.0f,
               "state six to one uses the long track-two fade");

    mission_state = MISSION_STATE_6;
    reset_call_log();
    set_missionstate(MISSION_STATE_6);
    expect_s32(calls.track2_play_track, 0x3a,
               "state six remains on the death track");
}

static void test_safe_state_transition_matrix(void)
{
    static const MISSION_STATE_ID safe_targets[7][7] = {
        { MISSION_STATE_0, MISSION_STATE_1, MISSION_STATE_2,
          MISSION_STATE_3, MISSION_STATE_4, MISSION_STATE_5,
          MISSION_STATE_6 },
        { MISSION_STATE_0, MISSION_STATE_1, MISSION_STATE_2,
          MISSION_STATE_3, MISSION_STATE_4, MISSION_STATE_5,
          MISSION_STATE_6 },
        { MISSION_STATE_0, MISSION_STATE_1, MISSION_STATE_2,
          MISSION_STATE_3, MISSION_STATE_4, MISSION_STATE_5,
          MISSION_STATE_6 },
        { MISSION_STATE_0, MISSION_STATE_1, MISSION_STATE_2,
          MISSION_STATE_3, MISSION_STATE_4, MISSION_STATE_5,
          MISSION_STATE_6 },
        { MISSION_STATE_0, MISSION_STATE_1, MISSION_STATE_3,
          MISSION_STATE_4, MISSION_STATE_5, MISSION_STATE_6,
          MISSION_STATE_0 },
        { MISSION_STATE_0, MISSION_STATE_1, MISSION_STATE_3,
          MISSION_STATE_4, MISSION_STATE_6, MISSION_STATE_0,
          MISSION_STATE_0 },
        { MISSION_STATE_0, MISSION_STATE_1, MISSION_STATE_6,
          MISSION_STATE_0, MISSION_STATE_0, MISSION_STATE_0,
          MISSION_STATE_0 }
    };
    static const int safe_targets_count[7] = { 7, 7, 7, 7, 6, 5, 3 };
    s32 from;
    s32 i;

    fake_background_track = 0x31;
    for (from = 0; from < 7; ++from)
    {
        for (i = 0; i < safe_targets_count[from]; ++i)
        {
            MISSION_STATE_ID target = safe_targets[from][i];
            mission_state = from;
            reset_call_log();
            set_missionstate(target);
            expect_s32(get_mission_state(), target,
                       "every documented safe mission-state transition settles");
        }
    }
}

static void test_stage_and_saved_state_dispatch(void)
{
    fake_background_track = -1;
    reset_call_log();
    sub_GAME_7F0C11FC(9);
    expect_s32(stageMusicID, 9,
               "stage dispatch stores the requested stage ID");
    expect_s32(calls.track1_stop, 1,
               "stage dispatch stops track one before starting music");
    expect_s32(calls.track2_stop, 1,
               "stage dispatch stops track two before starting music");
    expect_s32(calls.track3_stop, 1,
               "stage dispatch stops track three before starting music");
    expect_s32(get_mission_state(), MISSION_STATE_1,
               "stages without background music start in state one");

    fake_background_track = 0x31;
    reset_call_log();
    sub_GAME_7F0C11FC(10);
    expect_s32(get_mission_state(), MISSION_STATE_4,
               "stages with background music start in state four");
    expect_s32(calls.track3_play_track, fake_background_track,
               "stage dispatch starts the background track");

    fake_background_track = -1;
    mission_state = MISSION_STATE_0;
    reset_call_log();
    sub_GAME_7F0C1288();
    expect_s32(get_mission_state(), MISSION_STATE_2,
               "background-less stage dispatch enters state two");

    fake_background_track = 0x31;
    mission_state = MISSION_STATE_0;
    reset_call_log();
    sub_GAME_7F0C1288();
    expect_s32(get_mission_state(), MISSION_STATE_5,
               "background stage dispatch enters state five");

    fake_background_track = -1;
    mission_state = MISSION_STATE_0;
    sub_GAME_7F0C12CC();
    expect_s32(get_mission_state(), MISSION_STATE_1,
               "background-less resume dispatch enters state one");

    fake_background_track = 0x31;
    mission_state = MISSION_STATE_0;
    sub_GAME_7F0C12CC();
    expect_s32(get_mission_state(), MISSION_STATE_4,
               "background resume dispatch enters state four");

    mission_state = MISSION_STATE_5;
    reset_call_log();
    sub_GAME_7F0C1310();
    expect_s32(dword_CODE_bss_8008C604, MISSION_STATE_5,
               "state-three dispatch saves the previous mission state");
    expect_s32(get_mission_state(), MISSION_STATE_3,
               "state-three dispatch enters state three");
    sub_GAME_7F0C1340();
    expect_s32(get_mission_state(), MISSION_STATE_5,
               "state restore returns to the saved mission state");

    set_missionstate_zero();
    expect_s32(get_mission_state(), MISSION_STATE_0,
               "zero-state helper returns to the idle mission state");
}

static void test_slot_countdown_boundaries(void)
{
    sub_GAME_7F0C1364();

    g_ClockTimer = 0;
    music_slot_active_0[0] = 1;
    music_slot_minutes_0[0] = 4;
    music_slot_seconds_0[0] = 8;
    mission_state = MISSION_STATE_1;
    reset_call_log();
    reset_all_music_slots();
    expect_s32(music_slot_minutes_0[0], 4,
               "zero clock tick leaves slot minutes unchanged");
    expect_s32(music_slot_seconds_0[0], 8,
               "zero clock tick leaves slot seconds unchanged");
    expect_s32(calls.background_queries, 0,
               "zero clock tick does not dispatch mission music");

    g_ClockTimer = 3;
    fake_background_track = -1;
    mission_state = MISSION_STATE_1;
    music_slot_active_0[0] = 1;
    music_slot_minutes_0[0] = 2;
    music_slot_seconds_0[0] = 10;
    reset_call_log();
    reset_all_music_slots();
    expect_s32(music_slot_minutes_0[0], 0,
               "countdown floors minutes at zero when the tick is larger");
    expect_s32(music_slot_seconds_0[0], 7,
               "countdown subtracts a full clock tick from seconds");
    expect_s32(get_mission_state(), MISSION_STATE_2,
               "an active slot moves state one to the X-track state");

    fake_background_track = 0x31;
    mission_state = MISSION_STATE_2;
    music_slot_active_0[0] = 1;
    music_slot_minutes_0[0] = 0;
    music_slot_seconds_0[0] = 2;
    reset_call_log();
    reset_all_music_slots();
    expect_s32(music_slot_active_0[0], 0,
               "expired seconds deactivate the slot");
    expect_s32(music_slot_seconds_0[0], 0,
               "expired seconds are cleared");
    expect_s32(get_mission_state(), MISSION_STATE_4,
               "an expired slot resumes the background state");

    fake_background_track = 0x31;
    mission_state = MISSION_STATE_1;
    music_slot_active_0[0] = 0;
    music_slot_minutes_0[0] = 5;
    music_slot_seconds_0[0] = 5;
    reset_call_log();
    reset_all_music_slots();
    expect_s32(music_slot_minutes_0[0], 2,
               "active countdown processing subtracts from minutes");
    expect_s32(music_slot_seconds_0[0], 2,
               "active countdown processing subtracts from seconds");
    expect_s32(get_mission_state(), MISSION_STATE_5,
               "a remaining inactive countdown enters the background X state");

    mission_state = MISSION_STATE_5;
    music_slot_active_0[0] = 1;
    music_slot_minutes_0[0] = 0;
    music_slot_seconds_0[0] = 0;
    reset_call_log();
    reset_all_music_slots();
    expect_s32(music_slot_active_0[0], 1,
               "a slot with no seconds remains active until its owner stops it");
    expect_s32(get_mission_state(), MISSION_STATE_4,
               "state five with no remaining slot resumes the background state");
}

int main(void)
{
    test_volume_wrappers();
    test_music_slot_lifecycle();
    test_state_audio_actions();
    test_safe_state_transition_matrix();
    test_stage_and_saved_state_dispatch();
    test_slot_countdown_boundaries();

    if (failures != 0)
    {
        fprintf(stderr, "%d mp_music test(s) failed\n", failures);
        return 1;
    }

    puts("mp_music tests passed");
    return 0;
}
