#include <ultra64.h>
#include <memp.h>
#include "image.h"
#include "initmttex.h"

void set_mt_tex_alloc(void)
{  
    const char *mt_size;

    g_TexCacheCount = 0;

    mt_size = tokenFind(1, "-mt");
    if (mt_size)
    {
        char *end;
        long kilobytes = strtol(mt_size, &end, 0);

        /* Keep the built-in/default pool size for malformed, negative, or
         * overflowing command-line values.  The old unsigned assignment
         * turned "-1" into a multi-gigabyte allocation request. */
        if (end != mt_size && *end == '\0' && kilobytes > 0 &&
            (unsigned long)kilobytes <= (0xFFFFFFFFUL / 1024UL))
        {
            bytes = (u32)kilobytes * 1024U;
        }
    }

    texInitPool(&ptr_texture_alloc_start, mempAllocBytesInBank(bytes, MEMPOOL_STAGE), bytes);
}
