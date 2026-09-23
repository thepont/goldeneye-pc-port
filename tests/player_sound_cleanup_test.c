#include <stdio.h>
#include <string.h>

struct ALSoundState_s;
typedef struct ALSoundState_s ALSoundState;

#include <snd.h>
#include "player.h"

void cleanupplayersoundrelated(void);

struct player *g_playerPointers[4];

static int configured_players;
static ALSoundState *deactivated[8];
static int deactivated_count;
static int failures;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

s32 getPlayerCount(void)
{
    return configured_players;
}

u8 sndGetPlayingState(ALSoundState *state)
{
    return state->playingState;
}

void sndDeactivate(ALSoundState *state)
{
    deactivated[deactivated_count++] = state;
    state->playingState = AL_STOPPED;
}

int main(void)
{
    struct player players[2] = {};
    ALSoundState stopped = {.playingState = AL_STOPPED};
    ALSoundState playing = {.playingState = AL_PLAYING};
    ALSoundState stopping = {.playingState = AL_STOPPING};

    g_playerPointers[0] = &players[0];
    g_playerPointers[1] = &players[1];
    players[0].hands[0].audioHandle = NULL;
    players[0].hands[1].audioHandle = &stopped;
    players[1].hands[0].audioHandle = &playing;
    players[1].hands[1].audioHandle = &stopping;
    configured_players = 2;
    deactivated_count = 0;

    cleanupplayersoundrelated();

    expect(deactivated_count == 2 && deactivated[0] == &playing &&
               deactivated[1] == &stopping,
           "player sound cleanup deactivates every non-stopped hand sound");
    expect(stopped.playingState == AL_STOPPED &&
               playing.playingState == AL_STOPPED &&
               stopping.playingState == AL_STOPPED,
           "player sound cleanup leaves all visited states stopped");

    configured_players = 0;
    deactivated_count = 0;
    cleanupplayersoundrelated();
    expect(deactivated_count == 0,
           "player sound cleanup is a no-op with no active players");

    if (failures != 0)
    {
        fprintf(stderr, "%d player-sound cleanup contract(s) failed\n", failures);
        return 1;
    }

    puts("player-sound cleanup suite passed");
    return 0;
}
