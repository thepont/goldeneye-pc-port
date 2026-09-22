#include "coop.h"

#include <float.h>

static int geCoopStringEquals(const char *left, const char *right)
{
    if (left == 0 || right == 0)
    {
        return 0;
    }

    while (*left != '\0' && *right != '\0')
    {
        if (*left != *right)
        {
            return 0;
        }

        left++;
        right++;
    }

    return *left == '\0' && *right == '\0';
}

static int geCoopFinite(float value)
{
    return value == value && value <= FLT_MAX && value >= -FLT_MAX;
}

int geCoopParseMode(const char *value,
                    int solo_mode,
                    int multi_mode,
                    int coop_mode,
                    int fallback_mode)
{
    if (geCoopStringEquals(value, "solo"))
    {
        return solo_mode;
    }

    if (geCoopStringEquals(value, "multi"))
    {
        return multi_mode;
    }

    if (geCoopStringEquals(value, "coop"))
    {
        return coop_mode;
    }

    return fallback_mode;
}

int geCoopPlayerCount(int controller_count)
{
    return controller_count >= GE_COOP_MAX_PLAYERS ? GE_COOP_MAX_PLAYERS : 0;
}

int geCoopResumeMode(int saved_mode,
                     int controller_count,
                     int solo_mode,
                     int multi_mode,
                     int coop_mode)
{
    if (saved_mode == coop_mode)
    {
        return controller_count >= GE_COOP_MAX_PLAYERS ? coop_mode : solo_mode;
    }

    if (saved_mode == solo_mode || saved_mode == multi_mode)
    {
        return saved_mode;
    }

    return solo_mode;
}

int geCoopSpawnPadNeedsFallback(int player_count, int authored_start_pad_count)
{
    return player_count >= GE_COOP_MAX_PLAYERS && authored_start_pad_count < GE_COOP_MAX_PLAYERS;
}

int geCoopSpawnPadIsDistinct(float delta_x,
                             float delta_z,
                             float minimum_distance)
{
    float distance_squared;

    if (minimum_distance <= 0.0f ||
        !geCoopFinite(delta_x) || !geCoopFinite(delta_z) ||
        !geCoopFinite(minimum_distance))
    {
        return 0;
    }

    distance_squared = (delta_x * delta_x) + (delta_z * delta_z);
    if (!geCoopFinite(distance_squared) ||
        !geCoopFinite(minimum_distance * minimum_distance))
    {
        return 0;
    }
    return distance_squared >= (minimum_distance * minimum_distance);
}

int geCoopShouldUseDeathmatchSetup(int player_count,
                                   int game_mode,
                                   int deathmatch_mode)
{
    return player_count >= GE_COOP_MAX_PLAYERS && game_mode == deathmatch_mode;
}

int geCoopCanSelectCharacter(const int *selected_characters,
                             int selected_count,
                             int candidate)
{
    int i;

    if (candidate < 0 || candidate >= GE_COOP_CHARACTER_COUNT ||
        selected_count < 0 || selected_count > GE_COOP_MAX_PLAYERS ||
        (selected_count > 0 && selected_characters == 0))
    {
        return 0;
    }

    for (i = 0; i < selected_count; i++)
    {
        if (selected_characters[i] < 0 ||
            selected_characters[i] >= GE_COOP_CHARACTER_COUNT ||
            selected_characters[i] == candidate)
        {
            return 0;
        }
    }

    return 1;
}

int geCoopShouldMissionFail(int player_count, int dead_count)
{
    return player_count == GE_COOP_MAX_PLAYERS && dead_count >= player_count;
}
