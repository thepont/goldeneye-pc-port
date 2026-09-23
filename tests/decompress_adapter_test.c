#include <stdio.h>
#include <stdint.h>

#include <ultra64.h>
#include "zlib.h"

u32 decompressdata(u8 *src, u8 *dst, struct huft *huffman_table);
s32 rzipGetSomething(void);

u8 *rz_inbuf;
u8 *rz_outbuf;
u32 rz_inptr;
u32 rz_wp;
struct huft *rz_hlist;
static int inflate_calls;
static int failures;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

void zlib_inflate(void)
{
    inflate_calls++;
    rz_wp = 37;
    rz_inptr = 5;
}

static void test_matching_header_initializes_adapter(void)
{
    u8 input[8] = {0, 0, 0x11, 0x72, 0, 0, 0, 0};
    u8 output[64] = {0};
    struct huft table;

    inflate_calls = 0;
    expect(decompressdata(&input[2], output, &table) == 37,
           "decompression returns the inflate write position");
    expect(inflate_calls == 1 && rz_inbuf == &input[4] && rz_outbuf == output &&
               rz_hlist == &table && rz_inptr == 5,
           "decompression advances the input adapter and preserves its ports");
}

static void test_mismatched_header_checks_both_legacy_signatures(void)
{
    u8 input[8] = {0x11, 0x00, 0x00, 0x00, 0, 0, 0, 0};
    u8 output[16] = {0};
    struct huft table;

    inflate_calls = 0;
    decompressdata(&input[2], output, &table);
    expect(inflate_calls == 1 && rz_wp == 37,
           "decompression still invokes inflate after a legacy header mismatch");
    (void)rzipGetSomething();
    expect(rz_inbuf == &input[4],
           "decompression exposes the post-header input position");
}

int main(void)
{
    test_matching_header_initializes_adapter();
    test_mismatched_header_checks_both_legacy_signatures();

    if (failures != 0)
    {
        fprintf(stderr, "%d decompression-adapter contract(s) failed\n", failures);
        return 1;
    }

    puts("decompression-adapter suite passed");
    return 0;
}
