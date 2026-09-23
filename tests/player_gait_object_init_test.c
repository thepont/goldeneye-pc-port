#include <stdio.h>

#include <ultra64.h>
#include "chrobjdata.h"

void init_player_gait_object(void);

struct ModelNode player_gait_hdr;
struct ModelFileHeader player_gait_object_header;

int main(void)
{
    player_gait_object_header.RootNode = NULL;
    init_player_gait_object();

    if (player_gait_object_header.RootNode != &player_gait_hdr)
    {
        fprintf(stderr, "FAIL: gait object keeps the full-width root-node pointer\n");
        return 1;
    }

    puts("player-gait object initialization suite passed");
    return 0;
}
