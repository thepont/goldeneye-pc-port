#include <stdio.h>
#include <string.h>

#include <ultra64.h>

#include "bondconstants.h"
#include "bondview.h"
#include "language.h"
#include "mpmenu.h"
#include "player.h"
#include "assets/obseg/text/LmpmenuE.h"

struct player *g_CurrentPlayer;
struct player *g_playerPointers[MAX_MULTIPLAYER];
struct player_data g_playerPlayerData[MAX_MULTIPLAYER];
struct player_data *g_playerPerm;

static struct player current_player;
static u32 random_values[32];
static int random_index;
static s32 fake_player_count;
static MPSCENARIOS fake_scenario;
static s32 fake_stick_x;
static u16 fake_buttons;
static s32 fake_controls_locked;
static int failures;

extern s32 g_pausedFlag;

extern s32 mpwatchIsPlayerPressingRight(s32 player);
extern s32 mpwatchIsPlayerPressingLeft(s32 player);
extern void pauseAndLockControls(void);
extern s32 get_points_for_mp_player(s32 playernum);
extern void write_playerrank_to_buffer(char *buffer, s32 playernum);
extern s32 mpwatchShouldDisplayRank(s32 param_1);
extern s32 mpwatchShouldDisplayScore(s32 param_1);

static void expect_s32(s32 actual, s32 expected, const char *message)
{
    if (actual != expected)
    {
        fprintf(stderr, "FAIL: %s (actual %d, expected %d)\n",
                message, actual, expected);
        failures++;
    }
}

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

u32 randomGetNext(void)
{
    u32 value = random_values[random_index < 32 ? random_index : 31];
    random_index++;
    return value;
}

static void reset_fixture(void)
{
    memset(&current_player, 0, sizeof(current_player));
    memset(g_playerPointers, 0, sizeof(g_playerPointers));
    memset(g_playerPlayerData, 0, sizeof(g_playerPlayerData));
    memset(random_values, 0, sizeof(random_values));
    random_index = 0;
    fake_player_count = 1;
    fake_scenario = SCENARIO_NORMAL;
    fake_stick_x = 0;
    fake_buttons = 0;
    fake_controls_locked = 0;
    g_CurrentPlayer = &current_player;
    g_playerPerm = &g_playerPlayerData[0];
    g_stopPlayFlag = 0;
    g_gameOverFlag = 0;
    g_pausedFlag = 0;
}

s32 getPlayerCount(void)
{
    return fake_player_count;
}

MPSCENARIOS get_scenario(void)
{
    return fake_scenario;
}

s32 joyGetStickXInRange(s8 player, s32 rangemin, s32 rangemax)
{
    (void)player;
    (void)rangemin;
    (void)rangemax;
    return fake_stick_x;
}

u16 joyGetButtonsPressedThisFrame(s8 player, u16 mask)
{
    (void)player;
    return fake_buttons & mask;
}

void lvlSetControlsLockedFlag(s32 locked)
{
    fake_controls_locked = locked;
}

u8 *langGet(s32 slot)
{
    static char rank_first[] = "Rank: 1st";
    static char rank_second[] = "Rank: 2nd";
    static char rank_third[] = "Rank: 3rd";
    static char rank_fourth[] = "Rank: 4th";
    u32 text_slot = (u32)slot & LANGUAGE_TEXT_SLOT_MASK;

    switch (text_slot)
    {
        case MPMENU_STR_11_RANK1ST:
            return (u8 *)rank_first;
        case MPMENU_STR_12_RANK2ND:
            return (u8 *)rank_second;
        case MPMENU_STR_13_RANK3RD:
            return (u8 *)rank_third;
        case MPMENU_STR_14_RANK4TH:
            return (u8 *)rank_fourth;
        default:
            return (u8 *)"unused";
    }
}

static void test_float_award_ranking_preserves_fractional_values(void)
{
    reset_fixture();

    /* The 2.9 player remains ahead of 2.5 after player two is considered. */
    expect_s32(mpFindMaxFloat(3, 1.0f, 2.9f, 2.5f, 0.0f), 1,
               "maximum award ranking compares the complete player-one float");
    expect_s32(mpFindMaxFloat(4, 1.0f, 3.9f, 3.5f, 3.2f), 1,
               "maximum award ranking keeps the complete running maximum for four players");

    /* The -0.9 player remains below -0.5 after player two is considered. */
    expect_s32(mpFindMinFloat(3, 0.0f, -0.9f, -0.5f, 0.0f), 1,
               "minimum award ranking compares the complete player-one float");
    expect_s32(mpFindMinFloat(4, 0.0f, -2.9f, -2.5f, -2.2f), 1,
               "minimum award ranking keeps the complete running minimum for four players");

    /* Player counts are boundaries: ignored slots must not affect the result. */
    expect_s32(mpFindMaxFloat(2, 1.2f, 1.1f, 99.0f, 100.0f), 0,
               "maximum ranking ignores unused player slots");
    expect_s32(mpFindMinFloat(2, 1.2f, 1.3f, -99.0f, -100.0f), 0,
               "minimum ranking ignores unused player slots");
}

static void test_ranking_ties_use_the_deterministic_random_tiebreaker(void)
{
    reset_fixture();
    memset(random_values, 1, sizeof(random_values));

    expect_s32(mpFindMaxInt(4, 7, 7, 7, 7), 3,
               "maximum integer ties advance when the random tiebreaker is set");
    expect_s32(mpFindMinInt(4, 7, 7, 7, 7), 3,
               "minimum integer ties advance when the random tiebreaker is set");
    expect_s32(mpFindMaxFloat(4, 7.5f, 7.5f, 7.5f, 7.5f), 3,
               "maximum float ties use the same tiebreaker");
    expect_s32(mpFindMinFloat(4, 7.5f, 7.5f, 7.5f, 7.5f), 3,
               "minimum float ties use the same tiebreaker");

    reset_fixture();
    memset(random_values, 0, sizeof(random_values));
    expect_s32(mpFindMaxInt(4, 7, 7, 7, 7), 0,
               "maximum integer ties retain the first player when the tiebreaker is clear");
    expect_s32(mpFindMinInt(4, 7, 7, 7, 7), 0,
               "minimum integer ties retain the first player when the tiebreaker is clear");
}

static void test_menu_navigation_boundaries(void)
{
    reset_fixture();

    current_player.mpmenumode = MENU_GOWOC;
    expect_s32(mpwatchMenuCanGoRight(), 1,
               "results menu can move right from weapon-of-choice");
    expect_s32(mpwatchMenuCanGoLeft(), 0,
               "results menu cannot move left from weapon-of-choice");

    current_player.mpmenumode = MENU_LOSSES;
    expect_s32(mpwatchMenuCanGoRight(), 1,
               "results menu can move right from losses");
    g_gameOverFlag = 0;
    expect_s32(mpwatchMenuCanGoLeft(), 0,
               "losses cannot move left before game over");
    g_gameOverFlag = 1;
    expect_s32(mpwatchMenuCanGoLeft(), 1,
               "losses can move left after game over");

    current_player.mpmenumode = MENU_KILLS;
    expect_s32(mpwatchMenuCanGoRight(), 1,
               "results menu can move right from kills");
    expect_s32(mpwatchMenuCanGoLeft(), 1,
               "results menu can move left from kills");

    current_player.mpmenumode = MENU_SCORES;
    g_gameOverFlag = 0;
    expect_s32(mpwatchMenuCanGoRight(), 1,
               "scores can move right while the game is active");
    g_gameOverFlag = 1;
    expect_s32(mpwatchMenuCanGoRight(), 0,
               "scores cannot move right once the game is over");
    expect_s32(mpwatchMenuCanGoLeft(), 1,
               "scores can always move left");

    current_player.mpmenumode = MENU_PAUSE;
    expect_s32(mpwatchMenuCanGoRight(), 1,
               "pause can move right");
    expect_s32(mpwatchMenuCanGoLeft(), 1,
               "pause can move left");

    current_player.mpmenumode = MENU_EXIT;
    expect_s32(mpwatchMenuCanGoRight(), 0,
               "exit cannot move right");
    expect_s32(mpwatchMenuCanGoLeft(), 1,
               "exit can move left");

    current_player.mpmenumode = MENU_EXIT_CONFIRM;
    expect_s32(mpwatchMenuCanGoRight(), 0,
               "exit confirmation cannot move right");
    expect_s32(mpwatchMenuCanGoLeft(), 0,
               "exit confirmation cannot move left");

    current_player.mpmenumode = MENU_FINISHED;
    expect_s32(mpwatchMenuCanGoRight(), 0,
               "finished results cannot move right");
    expect_s32(mpwatchMenuCanGoLeft(), 0,
               "finished results cannot move left");

    current_player.mpmenumode = MENU_INVALID;
    expect_s32(mpwatchMenuCanGoRight(), 0,
               "invalid menu mode cannot move right");
    expect_s32(mpwatchMenuCanGoLeft(), 0,
               "invalid menu mode cannot move left");
}

static void test_unpause_clears_all_pause_state(void)
{
    extern s32 g_pausedFlag;

    reset_fixture();
    g_stopPlayFlag = 1;
    g_gameOverFlag = 4;
    g_pausedFlag = 1;

    mpwatchUnpauseGame();

    expect_s32(g_stopPlayFlag, 0, "unpause clears the stop-play flag");
    expect_s32(g_gameOverFlag, 0, "unpause clears the game-over countdown");
    expect_s32(g_pausedFlag, 0, "unpause clears the paused flag");
}

static void test_controller_edges_require_a_centered_stick_or_button_press(void)
{
    reset_fixture();

    expect_s32(mpwatchIsPlayerPressingRight(0), 0,
               "neutral stick does not navigate right");
    expect_s32(mpwatchIsPlayerPressingLeft(0), 0,
               "neutral stick does not navigate left");

    fake_stick_x = 1;
    current_player.mpjoywascentre = 1;
    expect_s32(mpwatchIsPlayerPressingRight(0), 1,
               "right stick edge navigates after leaving center");

    current_player.mpjoywascentre = 0;
    expect_s32(mpwatchIsPlayerPressingRight(0), 0,
               "right stick edge does not repeat without a center latch");

    fake_stick_x = -2;
    current_player.mpjoywascentre = 1;
    expect_s32(mpwatchIsPlayerPressingLeft(0), 1,
               "left stick edge navigates after leaving center");

    fake_stick_x = -1;
    expect_s32(mpwatchIsPlayerPressingLeft(0), 0,
               "the left deadband boundary does not navigate");

    fake_stick_x = 0;
    fake_buttons = R_JPAD;
    current_player.mpjoywascentre = 0;
    expect_s32(mpwatchIsPlayerPressingRight(0), 1,
               "right digital input navigates without a stick latch");

    fake_buttons = L_JPAD;
    expect_s32(mpwatchIsPlayerPressingLeft(0), 1,
               "left digital input navigates without a stick latch");
}

static void test_pause_and_action_lock_policy(void)
{
    reset_fixture();

    fake_player_count = 1;
    g_stopPlayFlag = 1;
    current_player.mpmenuon = 1;
    expect_s32(disablePlayerActionsWhenPausedOrInMpMenu(), 1,
               "solo play keeps player actions enabled while the menu is up");

    fake_player_count = 2;
    expect_s32(disablePlayerActionsWhenPausedOrInMpMenu(), 0,
               "multiplayer stop-play state disables player actions");

    g_stopPlayFlag = 0;
    expect_s32(disablePlayerActionsWhenPausedOrInMpMenu(), 0,
               "multiplayer menu state disables player actions");

    current_player.mpmenuon = 0;
    expect_s32(disablePlayerActionsWhenPausedOrInMpMenu(), 1,
               "active multiplayer play leaves player actions enabled");

    pauseAndLockControls();
    expect_s32(g_pausedFlag, 1, "pause policy sets the paused flag");
    expect_s32(fake_controls_locked, 1,
               "pause policy locks controls through the level boundary");

    mpwatchSetStopPlayFlag();
    expect_s32(g_stopPlayFlag, 1, "stop-play policy sets its global flag");
    expect_s32(checkGamePaused(), 1, "paused state is observable through its query");
}

static void test_gauge_visibility_is_suppressed_after_game_over(void)
{
    reset_fixture();

    expect_s32(mpwatchShouldDisplayGauges(), 0,
               "closed multiplayer menu with no health timer hides gauges");

    current_player.mpmenuon = 1;
    expect_s32(mpwatchShouldDisplayGauges(), 1,
               "open multiplayer menu keeps gauges visible");

    current_player.mpmenuon = 0;
    current_player.healthdisplaytime = 1;
    expect_s32(mpwatchShouldDisplayGauges(), 1,
               "active health display timer keeps gauges visible");

    g_gameOverFlag = 1;
    expect_s32(mpwatchShouldDisplayGauges(), 0,
               "game-over state suppresses gauges even with a display timer");
}

static void test_points_follow_each_multiplayer_scenario(void)
{
    reset_fixture();
    fake_player_count = 2;
    g_playerPlayerData[0].kill_counts[0] = 2;
    g_playerPlayerData[0].kill_counts[1] = 5;
    g_playerPlayerData[0].killed_gg_owner_count = 1;
    fake_scenario = SCENARIO_NORMAL;
    expect_s32(get_points_for_mp_player(0), 3,
               "normal scoring counts enemy kills and subtracts suicides");

    fake_scenario = SCENARIO_MWTGG;
    expect_s32(get_points_for_mp_player(0), 3,
               "golden-gun scoring uses the normal kill formula");

    fake_scenario = SCENARIO_LTK;
    expect_s32(get_points_for_mp_player(0), 3,
               "license-to-kill scoring uses the normal kill formula");

    g_playerPlayerData[0].order_out_in_yolt = 3;
    fake_scenario = SCENARIO_YOLT;
    expect_s32(get_points_for_mp_player(0), 1,
               "you-only-live-twice scoring converts elimination order to points");

    g_playerPlayerData[0].flag_counter = 7;
    fake_scenario = SCENARIO_TLD;
    expect_s32(get_points_for_mp_player(0), 7,
               "flag-tag scoring reports the collected flag counter");

    fake_player_count = 4;
    g_playerPlayerData[0].have_token_or_goldengun = 0;
    g_playerPlayerData[1].have_token_or_goldengun = 0;
    g_playerPlayerData[2].have_token_or_goldengun = 1;
    g_playerPlayerData[3].have_token_or_goldengun = 1;
    g_playerPlayerData[0].kill_counts[0] = 1;
    g_playerPlayerData[0].kill_counts[1] = 2;
    g_playerPlayerData[0].kill_counts[2] = 3;
    g_playerPlayerData[0].kill_counts[3] = 4;
    g_playerPlayerData[1].kill_counts[0] = 5;
    g_playerPlayerData[1].kill_counts[1] = 6;
    g_playerPlayerData[1].kill_counts[2] = 7;
    g_playerPlayerData[1].kill_counts[3] = 8;
    fake_scenario = SCENARIO_2v2;
    expect_s32(get_points_for_mp_player(0), 8,
               "team scoring adds enemy kills and subtracts team kills");

    fake_scenario = (MPSCENARIOS)99;
    expect_s32(get_points_for_mp_player(0), 0,
               "unknown scoring scenarios have a safe zero policy");
}

static void test_rank_and_score_visibility_matrix(void)
{
    static const MPSCENARIOS scenarios_with_rank[] = {
        SCENARIO_NORMAL, SCENARIO_TLD, SCENARIO_MWTGG, SCENARIO_LTK,
        SCENARIO_2v2, SCENARIO_3v1, SCENARIO_2v1
    };
    static const MPSCENARIOS scenarios_with_score[] = {
        SCENARIO_NORMAL, SCENARIO_MWTGG, SCENARIO_LTK,
        SCENARIO_2v2, SCENARIO_3v1, SCENARIO_2v1
    };
    size_t i;

    reset_fixture();

    for (i = 0; i < sizeof(scenarios_with_rank) / sizeof(scenarios_with_rank[0]); i++)
    {
        fake_scenario = scenarios_with_rank[i];
        expect_s32(mpwatchShouldDisplayRank(0), 1,
                   "supported multiplayer scenario displays rank");
        expect_s32(mpwatchShouldDisplayRank(1), 1,
                   "supported multiplayer scenario displays rank in active play");
    }

    fake_scenario = SCENARIO_YOLT;
    expect_s32(mpwatchShouldDisplayRank(0), 1,
               "YOLT displays rank on its end-state page");
    expect_s32(mpwatchShouldDisplayRank(1), 0,
               "YOLT hides rank during active play");

    for (i = 0; i < sizeof(scenarios_with_score) / sizeof(scenarios_with_score[0]); i++)
    {
        fake_scenario = scenarios_with_score[i];
        expect_s32(mpwatchShouldDisplayScore(0), 1,
                   "score-supported scenario displays score");
        expect_s32(mpwatchShouldDisplayScore(1), 1,
                   "score-supported scenario keeps score during active play");
    }

    fake_scenario = SCENARIO_YOLT;
    expect_s32(mpwatchShouldDisplayScore(0), 0,
               "YOLT hides the score page");
    fake_scenario = SCENARIO_TLD;
    expect_s32(mpwatchShouldDisplayScore(0), 0,
               "flag-tag hides the score page");

    fake_scenario = (MPSCENARIOS)99;
    expect_s32(mpwatchShouldDisplayRank(0), 0,
               "invalid scenario safely hides rank instead of hanging");
    expect_s32(mpwatchShouldDisplayScore(0), 0,
               "invalid scenario safely hides score instead of hanging");
}

static void test_rank_buffer_uses_score_order_and_team_rank_rules(void)
{
    char buffer[32];

    reset_fixture();
    fake_player_count = 4;
    fake_scenario = SCENARIO_NORMAL;
    g_playerPlayerData[0].kill_counts[1] = 5;
    g_playerPlayerData[1].kill_counts[0] = 3;
    g_playerPlayerData[2].kill_counts[0] = 2;
    g_playerPlayerData[3].kill_counts[0] = 1;

    memset(buffer, 0, sizeof(buffer));
    write_playerrank_to_buffer(buffer, 0);
    expect_true(strcmp(buffer, "Rank: 1st") == 0,
                "rank buffer reports the highest score");

    memset(buffer, 0, sizeof(buffer));
    write_playerrank_to_buffer(buffer, 3);
    expect_true(strcmp(buffer, "Rank: 4th") == 0,
                "rank buffer reports the lowest score");

    memset(g_playerPlayerData, 0, sizeof(g_playerPlayerData));
    g_playerPlayerData[0].have_token_or_goldengun = 0;
    g_playerPlayerData[1].have_token_or_goldengun = 0;
    g_playerPlayerData[2].have_token_or_goldengun = 1;
    g_playerPlayerData[3].have_token_or_goldengun = 1;
    g_playerPlayerData[0].kill_counts[2] = 5;
    fake_scenario = SCENARIO_2v2;
    memset(buffer, 0, sizeof(buffer));
    write_playerrank_to_buffer(buffer, 2);
    expect_true(strcmp(buffer, "Rank: 2nd") == 0,
                "team scenario maps the third displayed row to second rank");
}

int main(void)
{
    test_float_award_ranking_preserves_fractional_values();
    test_ranking_ties_use_the_deterministic_random_tiebreaker();
    test_menu_navigation_boundaries();
    test_unpause_clears_all_pause_state();
    test_controller_edges_require_a_centered_stick_or_button_press();
    test_pause_and_action_lock_policy();
    test_gauge_visibility_is_suppressed_after_game_over();
    test_points_follow_each_multiplayer_scenario();
    test_rank_and_score_visibility_matrix();
    test_rank_buffer_uses_score_order_and_team_rank_rules();

    expect_true(failures == 0, "all multiplayer menu policy tests pass");
    return failures == 0 ? 0 : 1;
}
