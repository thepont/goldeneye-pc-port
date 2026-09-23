#include <ultra64.h>
#include <bondconstants.h>
#include <memp.h>
#include "alloc_window_pieces.h"
#include "glass.h"



void alloc_shattered_window_pieces(void)
{
    s32 i;
    s32 level = lvlGetCurrentStageToLoad();
    s32 player_count = getPlayerCount();

    /* Stage allocation can run while the player roster is still being
     * assembled. Treat an empty roster as the solo case instead of dividing
     * by zero and aborting the process. */
    if (player_count <= 0)
    {
        player_count = 1;
    }

    SHATTERED_WINDOW_PIECES_BUFFER_LEN = (200 / player_count);
    if ((level == LEVELID_STREETS) || (level == LEVELID_DEPOT))
    {
        SHATTERED_WINDOW_PIECES_BUFFER_LEN = (SHATTERED_WINDOW_PIECES_BUFFER_LEN >> 1);
    }
#ifdef DEBUG
    osSyncPrintf("Allocating %d bytes for glass data (%d bits)\n", SHATTERED_WINDOW_PIECES_BUFFER_LEN * sizeof(s_shattered_window_piece), SHATTERED_WINDOW_PIECES_BUFFER_LEN);
#endif

    ptr_shattered_window_pieces = mempAllocBytesInBank(((SHATTERED_WINDOW_PIECES_BUFFER_LEN * sizeof(s_shattered_window_piece)) + 0xF) & ~0xF, MEMPOOL_STAGE);

    for(i=0; i<SHATTERED_WINDOW_PIECES_BUFFER_LEN; i++)
    {
        ptr_shattered_window_pieces[i].active = 0;
    }

    g_NextShardNum = 0;
}
