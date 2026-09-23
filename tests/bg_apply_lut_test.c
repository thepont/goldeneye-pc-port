#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include <bondconstants.h>

void bgApplyPrimaryFogLUTEntries(Gfx *start, Gfx *end);
void bgApplyDynamicCCRMLUT(Gfx *start, Gfx *end, enum CCRMLUT lutIndex);

Gfx DL_LUT_PRIMARY_ADDFOG[3];
Gfx *ptrDynamic_CC_RM_LUT[10];

static int failures;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static Gfx command(uintptr_t w0, uintptr_t w1)
{
    Gfx result;

    result.words.w0 = w0;
    result.words.w1 = w1;
    return result;
}

static void initialize_luts(void)
{
    int i;

    DL_LUT_PRIMARY_ADDFOG[0] = command(0x11000000U, 0x22000000U);
    DL_LUT_PRIMARY_ADDFOG[1] = command(0x33000000U, 0x44000000U);
    DL_LUT_PRIMARY_ADDFOG[2] = command(0, 0);

    for (i = 0; i < 10; ++i)
    {
        ptrDynamic_CC_RM_LUT[i] = DL_LUT_PRIMARY_ADDFOG;
    }
}

static void test_primary_range_is_half_open(void)
{
    Gfx commands[3];
    Gfx terminal = command(0x55000000U, 0x66000000U);

    commands[0] = DL_LUT_PRIMARY_ADDFOG[0];
    commands[1] = terminal;
    commands[2] = DL_LUT_PRIMARY_ADDFOG[0];

    bgApplyPrimaryFogLUTEntries(commands, commands + 2);

    expect(commands[0].words.w0 == 0x33000000U &&
               commands[0].words.w1 == 0x44000000U,
           "primary LUT replaces matching commands inside the range");
    expect(commands[1].words.w0 == terminal.words.w0 &&
               commands[1].words.w1 == terminal.words.w1,
           "primary LUT leaves non-matching commands unchanged");
    expect(commands[2].words.w0 == DL_LUT_PRIMARY_ADDFOG[0].words.w0,
           "primary LUT does not write past the half-open end pointer");
}

static void test_dynamic_explicit_range(void)
{
    Gfx commands[2];

    commands[0] = DL_LUT_PRIMARY_ADDFOG[0];
    commands[1] = DL_LUT_PRIMARY_ADDFOG[0];

    bgApplyDynamicCCRMLUT(commands, commands + 1, CCRMLUT_PRIMARY_ADDFOG);

    expect(commands[0].words.w0 == 0x33000000U &&
               commands[0].words.w1 == 0x44000000U,
           "dynamic LUT replaces commands in an explicit range");
    expect(commands[1].words.w0 == DL_LUT_PRIMARY_ADDFOG[0].words.w0,
           "dynamic LUT respects the explicit end pointer");
}

static void test_dynamic_null_end_stops_on_opcode_byte(void)
{
    Gfx commands[4];

    commands[0] = DL_LUT_PRIMARY_ADDFOG[0];
    commands[1] = command((uintptr_t)G_ENDDL << 24, 0);
    commands[2] = DL_LUT_PRIMARY_ADDFOG[0];
    commands[3] = command(0x000000B8U, 0);

    bgApplyDynamicCCRMLUT(commands, NULL, CCRMLUT_PRIMARY_ADDFOG);

    expect(commands[0].words.w0 == 0x33000000U,
           "dynamic LUT replaces commands before the display-list sentinel");
    expect(commands[2].words.w0 == DL_LUT_PRIMARY_ADDFOG[0].words.w0,
           "dynamic LUT does not scan past a G_ENDDL opcode");
}

static void test_dynamic_empty_pc_range_is_safe(void)
{
    bgApplyDynamicCCRMLUT(NULL, NULL, CCRMLUT_PRIMARY_ADDFOG);
    expect(1, "empty PC display-list range returns safely");
}

int main(void)
{
    initialize_luts();
    test_primary_range_is_half_open();
    test_dynamic_explicit_range();
    test_dynamic_null_end_stops_on_opcode_byte();
    test_dynamic_empty_pc_range_is_safe();

    if (failures != 0)
    {
        return 1;
    }

    puts("background LUT suite passed");
    return 0;
}
