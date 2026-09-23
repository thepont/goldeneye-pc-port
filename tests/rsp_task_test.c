#include <stdio.h>
#include <stdint.h>

#include "rsp.h"
#include <memp.h>

extern struct GfxInfo_s g_gfxTaskSettings[2];
extern uintptr_t g_gfxRdpOutputBufferStart;
extern uintptr_t g_gfxRdpOutputBufferEnd;

OSMesgQueue gfxFrameMsgQ;
OSMesgQueue *sched_cmdQ;

long long int rspbootTextStart[1];
long long int rspbootTextEnd[1];
long long int gsp3DTextStart[1];
long long int gsp3DDataStart[1];

static int failures;
static int writeback_calls;
static int send_calls;
static OSMesgQueue *sent_queue;
static OSMesg sent_message;
static void *fake_output_buffer;
static u32 fake_output_size;
static u8 fake_output_bank;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

void osWritebackDCacheAll(void)
{
    writeback_calls++;
}

s32 osSendMesg(OSMesgQueue *queue, OSMesg message, s32 flags)
{
    (void)flags;
    send_calls++;
    sent_queue = queue;
    sent_message = message;
    return 0;
}

void *mempAllocBytesInBank(u32 bytes, u8 bank)
{
    fake_output_size = bytes;
    fake_output_bank = bank;
    return fake_output_buffer;
}

static void reset_fixture(void)
{
    g_gfxTaskSettingsList = &g_gfxTaskSettings[0];
    g_gfxTaskSettings[0].cfb = 0;
    g_gfxTaskSettings[1].cfb = 0;
    g_gfxRdpOutputBufferStart = 0;
    g_gfxRdpOutputBufferEnd = 0;
    writeback_calls = 0;
    send_calls = 0;
    sent_queue = NULL;
    sent_message = NULL;
    sched_cmdQ = (OSMesgQueue *)(uintptr_t)0x1234;
}

static void test_output_buffer_allocation_preserves_host_address(void)
{
    fake_output_buffer = (void *)(uintptr_t)0x76543210fedcba98ULL;

    rspAllocateBuffers();

    expect(fake_output_size == 0xA000 && fake_output_bank == MEMPOOL_PERMANENT,
           "RSP output allocation requests the permanent graphics buffer");
    expect(g_gfxRdpOutputBufferStart == (uintptr_t)fake_output_buffer,
           "RSP output allocation preserves the full-width start pointer");
    expect(g_gfxRdpOutputBufferEnd == (uintptr_t)fake_output_buffer + 0xA000,
           "RSP output allocation computes a full-width end pointer");
}

static void test_framebuffer_pointer_survives_task_submission(void)
{
    Gfx display_list[3];
    void *framebuffer = (void *)(uintptr_t)0x123456789abcdef0ULL;

    reset_fixture();
    g_gfxTaskSettings[0].cfb = (uintptr_t)framebuffer;

    rspGfxTaskStart(display_list, display_list + 2, 0, (OSMesg)(uintptr_t)0x55);

    expect(g_gfxTaskSettings[0].task.framebuffer == framebuffer,
           "RSP submission preserves the full-width framebuffer pointer");
    expect(g_gfxTaskSettings[0].task.list.t.data_ptr == (u64 *)display_list,
           "RSP submission keeps the display-list data pointer");
    expect(g_gfxTaskSettings[0].task.list.t.data_size == 2 * sizeof(Gfx),
           "RSP submission computes display-list byte size");
    expect(writeback_calls == 1,
           "RSP submission flushes the task before scheduling it");
    expect(send_calls == 1 && sent_queue == sched_cmdQ &&
               sent_message == (OSMesg)&g_gfxTaskSettings[0].task,
           "RSP submission sends the active task to the scheduler");
    expect(g_gfxTaskSettingsList == &g_gfxTaskSettings[1],
           "RSP submission toggles to the alternate task slot");
}

static void test_task_slots_toggle_back_without_pointer_xor(void)
{
    Gfx display_list[1];

    reset_fixture();
    g_gfxTaskSettingsList = &g_gfxTaskSettings[1];

    rspGfxTaskStart(display_list, display_list, 0, NULL);

    expect(g_gfxTaskSettingsList == &g_gfxTaskSettings[0],
           "RSP task-slot selection toggles back from the second slot");
}

int main(void)
{
    test_output_buffer_allocation_preserves_host_address();
    test_framebuffer_pointer_survives_task_submission();
    test_task_slots_toggle_back_without_pointer_xor();

    if (failures != 0)
    {
        return 1;
    }

    puts("RSP task suite passed");
    return 0;
}
