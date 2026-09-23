#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include <memp.h>
#include "deb.h"
#include "game_debug.h"
#include "ob.h"

static int failures;
static int deb_calls;
static void *deb_data;
static const char *deb_name;
static int blank_bank5_calls;
static int reset_bank_calls;
static int blank_loaded_calls;
static int null_next_calls;
static u8 last_bank;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

void debTryAdd(void *data, const char *name)
{
    deb_calls++;
    deb_data = data;
    deb_name = name;
}

void obBlankResourcesInBank5(void)
{
    blank_bank5_calls++;
}

void mempResetBank(u8 bank)
{
    reset_bank_calls++;
    last_bank = bank;
}

void obBlankResourcesLoadedInBank(u8 bank)
{
    blank_loaded_calls++;
    last_bank = bank;
}

void mempNullNextEntryInBank(u8 bank)
{
    null_next_calls++;
    last_bank = bank;
}

int main(void)
{
    deb_calls = 0;
    gameInit();
    expect(deb_calls == 1 && deb_data != NULL &&
               strcmp(deb_name, "game_c_debug") == 0,
           "game debug initialization registers its debug data exactly once");

    reset_mem_bank_5();
    expect(blank_bank5_calls == 1 && reset_bank_calls == 1 &&
               blank_loaded_calls == 1 && last_bank == MEMPOOL_ME,
           "game debug bank reset blanks and resets the ME resource bank");

    sub_GAME_7F0D1A7C();
    expect(null_next_calls == 1 && blank_loaded_calls == 2 &&
               last_bank == MEMPOOL_ME,
           "game debug cleanup nulls the next ME allocation and reload state");

    if (failures != 0)
    {
        fprintf(stderr, "%d game-debug contract(s) failed\n", failures);
        return 1;
    }

    puts("game-debug suite passed");
    return 0;
}
