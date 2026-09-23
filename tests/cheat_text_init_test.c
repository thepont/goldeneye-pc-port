#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "cheat.h"
#include "player.h"

void initCheatTextBuffer(void);
void disableOnscreenCheatText(void);

u8 g_CheatPlayerTextRelated[CHEAT_INVALID + 1];
struct player *g_CurrentPlayer;

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
    struct player player = {};
    s32 i;

    memset(g_CheatPlayerTextRelated, 0xA5, sizeof(g_CheatPlayerTextRelated));
    initCheatTextBuffer();
    for (i = 0; i <= CHEAT_INVALID; ++i)
    {
        expect(g_CheatPlayerTextRelated[i] == 0,
               "cheat-text initialization clears every allocated status byte");
    }

    g_CurrentPlayer = &player;
    player.cheatInputBufferIndex = 19;
    player.cheatInputCount = 20;
    disableOnscreenCheatText();
    expect(player.cheatInputBufferIndex == 0 && player.cheatInputCount == 0,
           "disabling cheat text resets both input counters");

    if (failures != 0)
    {
        fprintf(stderr, "%d cheat-text initialization contract(s) failed\n", failures);
        return 1;
    }

    puts("cheat-text initialization suite passed");
    return 0;
}
