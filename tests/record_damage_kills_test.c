/*
 * TDD regression for the PC co-op crash reported in record_damage_kills.
 *
 * AI/environmental damage can pass playerid == -1. The regression protects
 * the multiplayer death path from forwarding that sentinel to
 * set_cur_player(), which would treat it as an array index before
 * increment_num_kills_display_text_in_MP() runs.
 * This harness calls the real function with only deterministic fake ports;
 * it does not start the game or load a level. set_cur_player() traps the
 * invalid index so the red test reports the contract violation instead of
 * relying on an incidental host crash.
 */
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include <bondview.h>
#include <glass.h>
#include <gun.h>
#include <music.h>
#include <player.h>
#include <snd.h>

void record_damage_kills(f32 damage_amount,
                         f32 vectorx,
                         f32 vectorz,
                         s32 playerid,
                         s32 affects_armor);

struct player *g_playerPointers[4];
struct player_data g_playerPlayerData[4];
struct player *g_CurrentPlayer;
struct player_data *g_playerPerm;

s32 g_PlayerIsInTank;
struct PropRecord *g_PlayerTankProp;
bool g_PlayerInvincible;
s32 g_stopPlayFlag;
s32 g_gameOverFlag;
ALBank *g_musicSfxBufferPtr;
f32 watch_transition_time = 0.90909088f;
s32 g_isBondKIA;
s32 g_ExplodeTankOnDeathFlag;

static jmp_buf invalid_player_jump;
static int invalid_player_id_seen;
static int kill_display_updates;
static int suicide_display_updates;

Gfx *buildGaugeBarDL(Gfx *gdl, uintptr_t vertex_address, s32 vertex_count)
{
    (void)vertex_address;
    (void)vertex_count;
    return gdl;
}

void sub_GAME_7F0A69A8(void)
{
}

struct WatchVertex *setup_watch_rectangles(struct WatchVertex *vertices,
                                           s32 start_x,
                                           s32 start_z,
                                           s32 width,
                                           s32 height,
                                           s32 horizontal_offset,
                                           s32 vertical_offset)
{
    (void)start_x;
    (void)start_z;
    (void)width;
    (void)height;
    (void)horizontal_offset;
    (void)vertical_offset;
    return vertices;
}

Gfx *sub_GAME_7F0A3B40(Gfx *gdl, s32 *vertex_address)
{
    (void)vertex_address;
    return gdl;
}

void deleteCurrentSelectedFolder(void)
{
}

void sub_GAME_7F0C1340(void)
{
}

s32 Gun_hand_without_item(enum GUNHAND hand)
{
    (void)hand;
    return ITEM_UNARMED;
}

s32 currentPlayerEquipWeaponWrapper(enum GUNHAND hand, s32 next_weapon)
{
    (void)hand;
    (void)next_weapon;
    return 0;
}

s32 cur_player_get_control_type(void)
{
    return 0;
}

s32 getPlayerCount(void)
{
    return 2;
}

s32 get_cur_playernum(void)
{
    return 1;
}

void set_cur_player(s32 playernum)
{
    if (playernum < 0 || playernum >= 2)
    {
        invalid_player_id_seen = playernum;
        longjmp(invalid_player_jump, 1);
    }
    g_CurrentPlayer = g_playerPointers[playernum];
    g_playerPerm = &g_playerPlayerData[playernum];
}

MPSCENARIOS get_scenario(void)
{
    return 0;
}

bool bondinvHasGoldenGun(void)
{
    return FALSE;
}

void joyRumblePakStart(s32 controller, f32 duration)
{
    (void)controller;
    (void)duration;
}

void hudMakeDamageSegments(struct damage_display_val *segments,
                           s32 segment_count,
                           s32 is_armour,
                           f32 health)
{
    (void)segments;
    (void)segment_count;
    (void)is_armour;
    (void)health;
}

void drop_inventory(void)
{
}

void increment_num_deaths(void)
{
}

void increment_num_suicides_display_MP(void)
{
    ++suicide_display_updates;
}

void increment_num_kills_display_text_in_MP(void)
{
    ++kill_display_updates;
}

void increment_num_times_killed_MwtGC(void)
{
}

ALSoundState *sndPlaySfx(struct ALBankAlt_s *sound_bank,
                         s16 sound_index,
                         ALSoundState *pending_state)
{
    (void)sound_bank;
    (void)sound_index;
    (void)pending_state;
    return NULL;
}

/* record_damage_kills() reaches this after its attribution work. Keep the
 * test focused on the invalid killer index rather than pulling in the full
 * death-animation implementation from bondview2.c. */
void __wrap_bondviewKillCurrentPlayer(void)
{
}

static void reset_fixture(struct player *fake_players)
{
    memset(fake_players, 0, sizeof(*fake_players) * 2);
    memset(g_playerPlayerData, 0, sizeof(g_playerPlayerData));

    g_playerPointers[0] = &fake_players[0];
    g_playerPointers[1] = &fake_players[1];
    g_CurrentPlayer = &fake_players[1];
    g_playerPerm = &g_playerPlayerData[1];
    g_PlayerIsInTank = 0;
    g_PlayerTankProp = NULL;
    g_PlayerInvincible = FALSE;
    g_stopPlayFlag = 0;
    g_gameOverFlag = 0;

    fake_players[1].watch_animation_state = WATCH_ANIMATION_0x0;
    fake_players[1].bonddead = FALSE;
    fake_players[1].cheatBondInvincible = FALSE;
    fake_players[1].damageshowtime = -1;
    fake_players[1].bondhealth = 1.0f;
    fake_players[1].actual_health = 1.0f;
    fake_players[1].bondarmour = 0.0f;
    fake_players[1].actual_armor = 1.0f;
    fake_players[1].vv_theta = 0.0f;
    g_playerPlayerData[1].handicap = 1.0f;
}

static int run_case(s32 playerid)
{
    struct player fake_players[2];
    const int invalid = playerid < 0 || playerid >= 2;

    reset_fixture(fake_players);
    invalid_player_id_seen = -999;
    kill_display_updates = 0;
    suicide_display_updates = 0;

    /* vectorz=-1 keeps the backside-stat branch out of this reproduction;
     * the lethal damage then reaches the multiplayer death attribution. */
    if (setjmp(invalid_player_jump) == 0)
    {
        record_damage_kills(2.0f, 0.0f, -1.0f, playerid, 0);
    }

    if (invalid)
    {
        if (invalid_player_id_seen == playerid)
        {
            fprintf(stderr,
                    "RED: record_damage_kills passed playerid %d to set_cur_player\n",
                    playerid);
            return 1;
        }
        if (kill_display_updates != 0 || suicide_display_updates != 0)
        {
            fprintf(stderr,
                    "RED: invalid playerid %d changed multiplayer attribution\n",
                    playerid);
            return 1;
        }
        return 0;
    }

    if (invalid_player_id_seen != -999)
    {
        fprintf(stderr,
                "RED: valid playerid %d was rejected by attribution\n",
                playerid);
        return 1;
    }
    if (playerid == 0 && kill_display_updates != 1)
    {
        fprintf(stderr, "RED: valid playerid 0 did not count a player kill\n");
        return 1;
    }
    if (playerid == 1 && suicide_display_updates != 1)
    {
        fprintf(stderr, "RED: valid playerid 1 did not count a suicide\n");
        return 1;
    }
    return 0;
}

int main(void)
{
    /* The first two are the sentinels used by environmental/AI damage. The
     * latter two pin the valid killer and suicide paths while this regression
     * is fixed. */
    const s32 cases[] = {-1, 2, 0, 1};
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        if (run_case(cases[i]) != 0)
        {
            return 1;
        }
    }

    fprintf(stderr, "record_damage_kills invalid-attribution contract passed\n");
    return 0;
}
