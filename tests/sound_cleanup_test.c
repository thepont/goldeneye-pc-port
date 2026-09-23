#include <stdio.h>
#include <string.h>

struct ALSoundState_s;
typedef struct ALSoundState_s ALSoundState;

#include <snd.h>
#include "bondtypes.h"
#include "chrai.h"

void cleanupObjectSounds(void);
void cleanupSFXRelated(void);

sfxRecord sfx_related[SFX_RELATED_LEN];
ALSoundState *g_TankSfxState[2];

static ALSoundState *deactivated[16];
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

u8 sndGetPlayingState(ALSoundState *state)
{
    return state->playingState;
}

void sndDeactivate(ALSoundState *state)
{
    deactivated[deactivated_count++] = state;
    state->playingState = AL_STOPPED;
}

static void reset_audio_state(void)
{
    memset(sfx_related, 0, sizeof(sfx_related));
    g_TankSfxState[0] = NULL;
    g_TankSfxState[1] = NULL;
    memset(deactivated, 0, sizeof(deactivated));
    deactivated_count = 0;
}

static void test_object_sound_cleanup(void)
{
    ALSoundState states[SFX_RELATED_LEN] = {};
    int i;

    reset_audio_state();
    for (i = 0; i < SFX_RELATED_LEN; ++i)
    {
        states[i].playingState = i % 3 == 0 ? AL_PLAYING : AL_STOPPED;
        sfx_related[i].state = &states[i];
    }
    sfx_related[1].state = NULL;

    cleanupObjectSounds();

    expect(deactivated_count == 3,
           "object sound cleanup deactivates each playing slot once");
    expect(deactivated[0] == &states[0] && deactivated[1] == &states[3] &&
               deactivated[2] == &states[6],
           "object sound cleanup preserves the source slot scan order");
    expect(states[0].playingState == AL_STOPPED &&
               states[3].playingState == AL_STOPPED &&
               states[6].playingState == AL_STOPPED,
           "object sound cleanup stops every active state");
}

static void test_tank_sound_cleanup(void)
{
    ALSoundState stopped = {.playingState = AL_STOPPED};
    ALSoundState playing = {.playingState = AL_PLAYING};

    reset_audio_state();
    g_TankSfxState[0] = &stopped;
    g_TankSfxState[1] = &playing;

    cleanupSFXRelated();

    expect(deactivated_count == 1 && deactivated[0] == &playing,
           "tank sound cleanup leaves stopped state and deactivates playing state");

    reset_audio_state();
    cleanupSFXRelated();
    expect(deactivated_count == 0,
           "tank sound cleanup safely ignores absent tank states");
}

int main(void)
{
    test_object_sound_cleanup();
    test_tank_sound_cleanup();

    if (failures != 0)
    {
        fprintf(stderr, "%d sound-cleanup contract(s) failed\n", failures);
        return 1;
    }

    puts("sound-cleanup suite passed");
    return 0;
}
