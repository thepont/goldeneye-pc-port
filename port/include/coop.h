#ifndef GE007_COOP_H
#define GE007_COOP_H

#define GE_COOP_MAX_PLAYERS 2
#define GE_COOP_CHARACTER_COUNT 64

#ifdef __cplusplus
extern "C" {
#endif

/* Parse the deterministic PC resume harness value. Unknown values return the
 * caller-provided fallback and never create a new game mode. */
int geCoopParseMode(const char *value,
                    int solo_mode,
                    int multi_mode,
                    int coop_mode,
                    int fallback_mode);

/* Local co-op is deliberately a two-player mode even when more controllers
 * are connected. */
int geCoopPlayerCount(int controller_count);

/* Restore a recorded session without trapping a co-op save on a machine with
 * only one controller. Solo and deathmatch retain their recorded modes. */
int geCoopResumeMode(int saved_mode,
                     int controller_count,
                     int solo_mode,
                     int multi_mode,
                     int coop_mode);

/* Co-op must not reuse a single authored intro spawn for both players. */
int geCoopSpawnPadNeedsFallback(int player_count, int authored_start_pad_count);

/* The fallback pad must be separated from the first player's spawn. */
int geCoopSpawnPadIsDistinct(float delta_x,
                             float delta_z,
                             float minimum_distance);

/* Co-op missions use the solo setup even though they have two viewports. */
int geCoopShouldUseDeathmatchSetup(int player_count,
                                   int game_mode,
                                   int deathmatch_mode);

/* Return non-zero when candidate is a valid, not-yet-selected MP character. */
int geCoopCanSelectCharacter(const int *selected_characters,
                             int selected_count,
                             int candidate);

/* Return non-zero only when both players in the two-player mode are dead. */
int geCoopShouldMissionFail(int player_count, int dead_count);

#ifdef __cplusplus
}
#endif

#endif /* GE007_COOP_H */
