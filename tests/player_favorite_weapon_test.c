#include <stdio.h>

typedef int s32;

void set_favorite_weapon_for_every_player(void);

static int current_player;
static int set_player_calls[8];
static int set_player_count;
static int stored_right[8];
static int stored_left[8];
static int stored_count;
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
    return 3;
}

s32 get_cur_playernum(void)
{
    return current_player;
}

void set_cur_player(s32 player)
{
    set_player_calls[set_player_count++] = player;
    current_player = player;
}

void bondinvGetWeaponOfChoice(int *right_hand, int *left_hand)
{
    *right_hand = current_player + 10;
    *left_hand = current_player + 20;
}

void store_favorite_weapon_current_player(int right_hand, int left_hand)
{
    stored_right[stored_count] = right_hand;
    stored_left[stored_count] = left_hand;
    stored_count++;
}

int main(void)
{
    current_player = 3;
    set_player_count = 0;
    stored_count = 0;

    set_favorite_weapon_for_every_player();

    expect(stored_count == 3,
           "favorite weapon initialization visits every player exactly once");
    expect(stored_right[0] == 10 && stored_left[0] == 20 &&
               stored_right[1] == 11 && stored_left[1] == 21 &&
               stored_right[2] == 12 && stored_left[2] == 22,
           "favorite weapon initialization stores each player’s selected pair");
    expect(set_player_count == 4 && set_player_calls[0] == 0 &&
               set_player_calls[1] == 1 && set_player_calls[2] == 2 &&
               set_player_calls[3] == 3 && current_player == 3,
           "favorite weapon initialization restores the caller’s player");

    if (failures != 0)
    {
        fprintf(stderr, "%d favorite-weapon contract(s) failed\n", failures);
        return 1;
    }

    puts("favorite-weapon suite passed");
    return 0;
}
