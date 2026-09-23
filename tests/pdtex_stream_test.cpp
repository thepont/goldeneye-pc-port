#include "../tools/mktex/src/libpdtex/pdtex.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>

static void geTestRequire(bool condition)
{
    if (!condition) {
        std::abort();
    }
}

#undef assert
#define assert(condition) geTestRequire((condition))

extern "C" int reader_read(FILE *, struct pd_tex *)
{
    return 1;
}

extern "C" int writer_write(FILE *, struct pd_image *)
{
    return 1;
}

int main()
{
    struct pd_tex texture = {};
    FILE *temporary = std::tmpfile();

    assert(pdtex_read_stream(nullptr, nullptr) == 1);
    assert(pdtex_read_stream(&texture, nullptr) == 1);
    assert(pdtex_read_stream(nullptr, temporary) == 1);

    if (temporary != nullptr) {
        std::fclose(temporary);
    }
    return 0;
}
