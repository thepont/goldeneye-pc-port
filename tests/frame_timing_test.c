#include <stdio.h>
#include <stdlib.h>

#include <ultra64.h>
#include "frametiming.h"

void store_osgetcount(void);
void updateFrameCounters(s32 delta_frames);
void waitForNextFrame(void);
void setFrameDelay(s32 delay);

static u32 clock_values[8];
static int clock_index;
static OSTime telemetry_times[4];
static int telemetry_index;
static int failures;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

u32 osGetCount(void)
{
    return clock_values[clock_index++];
}

OSTime osGetTime(void)
{
    return telemetry_times[telemetry_index++];
}

static void test_counter_bookkeeping(void)
{
    clock_index = 0;
    clock_values[0] = 0x12345678;
    store_osgetcount();
    expect(copy_of_osgetcount_value_0 == 0x12345678 &&
               copy_of_osgetcount_value_1 == 0x12345678,
           "frame timing stores the sampled OS count in both baselines");

    currentFrameCounter = 10;
    halfFrameCounter = 4;
    previousFrameCounter = -1;
    copy_of_osgetcount_value_1 = 0x12345678;
    clock_values[clock_index] = 0x12345690;
    updateFrameCounters(3);

    expect(copy_of_osgetcount_value_0 == 0x12345678 &&
               copy_of_osgetcount_value_1 == 0x12345690 &&
               lastFrameCounter == 10 && currentFrameCounter == 13 &&
               speedgraphframes == 3 && previousFrameCounter == 4 &&
               halfFrameCounter == 6 && isFrameCounterOdd == 1 &&
               halfMinusPreviousCounter == 2,
           "frame timing updates every counter consistently for a positive delta");
}

static void test_wait_clamps_hitches_and_preserves_normal_ticks(void)
{
    clock_index = 0;
    telemetry_index = 0;
    telemetry_times[0] = 100;
    telemetry_times[1] = 100;
    telemetry_times[2] = 1000100;
    copy_of_osgetcount_value_1 = 0;
    frameDelay = 1;
    currentFrameCounter = 0;
    halfFrameCounter = 0;
    clock_values[0] = 10u * 775875u;
    clock_values[1] = 0x20000000u;
    waitForNextFrame();

    expect(frameDelay == 1 && speedgraphframes == 6 && currentFrameCounter == 6,
           "frame timing clamps a large wall-clock hitch to the catch-up limit");

    frameDelay = 1;
    clock_values[2] = 0x20000000u + 775875u;
    clock_values[3] = 0x20000000u + 2u * 775875u;
    waitForNextFrame();

    expect(speedgraphframes == 1 && currentFrameCounter == 7,
           "frame timing preserves a normal one-tick interval");

    setFrameDelay(99);
#ifndef LEFTOVERDEBUG
    expect(frameDelay == 1,
           "frame-delay setter remains inert when leftover debug support is disabled");
#endif
}

int main(void)
{
    test_counter_bookkeeping();
    test_wait_clamps_hitches_and_preserves_normal_ticks();

    if (failures != 0)
    {
        fprintf(stderr, "%d frame-timing contract(s) failed\n", failures);
        return 1;
    }

    puts("frame-timing suite passed");
    return 0;
}
