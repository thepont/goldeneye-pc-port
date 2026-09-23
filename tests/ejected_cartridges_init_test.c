#include <stdio.h>
#include <stdint.h>

#include <ultra64.h>
#include "bondtypes.h"
#include "gun.h"

void init_ejected_cartridges(void);

ALSoundState *g_CasingSfxState;
ALSoundState *g_UnusedSfxState;
ALSoundState *g_ImpactSfxStates[NUM_IMPACT_SFX_STATES];
u32 cartridges_eject;
u32 g_gunDebKeyframeIndex;
static ModelFileHeader cartridge_headers[2];
CartridgeModelFileRecord ejected_cartridge[3] = {
    {&cartridge_headers[0], "first"},
    {&cartridge_headers[1], "second"},
    {NULL, "sentinel"}
};
static int file_load_calls;
static ModelFileHeader *loaded_headers[2];
static char *loaded_names[2];
static int failures;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

void fileLoad(ModelFileHeader *header, char *name)
{
    if (file_load_calls < 2)
    {
        loaded_headers[file_load_calls] = header;
        loaded_names[file_load_calls] = name;
    }
    file_load_calls++;
}

int main(void)
{
    int i;

    g_CasingSfxState = (ALSoundState *)(uintptr_t)0x1;
    for (i = 0; i < NUM_IMPACT_SFX_STATES; ++i)
    {
        g_ImpactSfxStates[i] = (ALSoundState *)(uintptr_t)(i + 2);
    }
    cartridges_eject = 7;
    g_gunDebKeyframeIndex = 8;
    file_load_calls = 0;

    init_ejected_cartridges();

    expect(g_CasingSfxState == NULL && cartridges_eject == 0 &&
               g_gunDebKeyframeIndex == 0,
           "cartridge initialization resets eject state and casing sound state");
    for (i = 0; i < NUM_IMPACT_SFX_STATES; ++i)
    {
        expect(g_ImpactSfxStates[i] == NULL,
               "cartridge initialization clears every impact sound state");
    }
    expect(file_load_calls == 2 && loaded_headers[0] == &cartridge_headers[0] &&
               loaded_headers[1] == &cartridge_headers[1] &&
               loaded_names[0][0] == 'f' && loaded_names[1][0] == 's',
           "cartridge initialization loads every entry before its sentinel");

    if (failures != 0)
    {
        fprintf(stderr, "%d cartridge initialization contract(s) failed\n", failures);
        return 1;
    }

    puts("ejected-cartridge initialization suite passed");
    return 0;
}
