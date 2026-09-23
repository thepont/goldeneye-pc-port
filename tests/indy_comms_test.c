#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "indy_commands.h"
#include "indy_comms.h"

void indycomm_removed(void);
void indycommHost7F0D0124(void);
void indycommHostCloseConnection(void);

s32 indy_ready;

static int failures;
static int init_calls;
static int load_calls;
static int receive_file_calls;
static int dump_calls;
static int dump_ack_calls;
static int ramrom_load_calls;
static int ramrom_receive_calls;
static int export_calls;
static int export_ack_calls;
static int check_file_calls;
static int check_file_ack_calls;
static int host_command_calls;
static int host_command_ack_calls;
static int rmon_calls;
static char *last_filename;
static u8 *last_data;
static u8 *last_target;
static u8 *last_load_target;
static u8 *last_ramrom_target;
static u32 last_size;
static u8 *last_response;
static s32 last_response_size;
static char last_host_command[128];
static u8 response_byte;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void reset_fakes(void)
{
    init_calls = 0;
    load_calls = 0;
    receive_file_calls = 0;
    dump_calls = 0;
    dump_ack_calls = 0;
    ramrom_load_calls = 0;
    ramrom_receive_calls = 0;
    export_calls = 0;
    export_ack_calls = 0;
    check_file_calls = 0;
    check_file_ack_calls = 0;
    host_command_calls = 0;
    host_command_ack_calls = 0;
    rmon_calls = 0;
    last_filename = NULL;
    last_data = NULL;
    last_target = NULL;
    last_load_target = NULL;
    last_ramrom_target = NULL;
    last_size = 0;
    last_response = NULL;
    last_response_size = 0;
    memset(last_host_command, 0, sizeof(last_host_command));
}

s32 indycmdSendInitPacket(void)
{
    init_calls++;
    return 0;
}

s32 indycmdSendLoadFile(u8 *filename, u32 size)
{
    load_calls++;
    last_filename = (char *)filename;
    last_size = size;
    return 0;
}

s32 indycmdReceiveFile(u8 *response1, u8 *response2, u32 childsize, u8 *child)
{
    (void)response1;
    (void)response2;
    (void)childsize;
    receive_file_calls++;
    last_load_target = child;
    return 0;
}

s32 indycmdSendDump(char *filename, u32 size, struct indy_resource_entry_header *data)
{
    dump_calls++;
    last_filename = filename;
    last_size = size;
    last_data = (u8 *)data;
    return 0;
}

s32 indycmdAckSendDump(u8 *response)
{
    dump_ack_calls++;
    response[0] = response_byte;
    return 0;
}

s32 indycmdSendRamRomLoad(char *filename, u32 target, u32 filesize)
{
    ramrom_load_calls++;
    last_filename = filename;
    last_ramrom_target = (u8 *)(uintptr_t)target;
    last_size = filesize;
    return 0;
}

s32 indycmdReceiveRamRom(u32 *response1, u32 *response2, u32 *response3)
{
    ramrom_receive_calls++;
    *response1 = 1;
    *response2 = 2;
    *response3 = 3;
    return 0;
}

s32 indycmdSendHostExportFile(char *filename, u8 *data, u32 size)
{
    export_calls++;
    last_filename = filename;
    last_data = data;
    last_size = size;
    return 0;
}

s32 indycmdAckHostExportFile(u8 *response)
{
    export_ack_calls++;
    response[0] = response_byte;
    return 0;
}

s32 indycmdSendHostCheckFileExists(char *filename)
{
    check_file_calls++;
    last_filename = filename;
    return 0;
}

s32 indycmdAckHostCheckFileExists(u8 *response, u8 *response_size)
{
    check_file_ack_calls++;
    *(u8 **)response = last_response;
    *(s32 *)response_size = last_response_size;
    return 0;
}

s32 indycmdSendHostCmdPacket(char *command)
{
    host_command_calls++;
    strncpy(last_host_command, command, sizeof(last_host_command) - 1);
    return 0;
}

s32 indycmdAckHostCmdPacket(u8 *response)
{
    host_command_ack_calls++;
    *(u8 **)response = last_response;
    return 0;
}

s32 rmonStatus(void)
{
    rmon_calls++;
    return 0;
}

static void test_disabled_connection_is_quiet(void)
{
    char filename[] = "file.bin";
    u8 data[8] = {0};
    s32 size = 0;

    reset_fakes();
    indy_ready = 0;

    indycommHostinit();
    indycommHostLoadFile(filename, data);
    indycommHostSendDump(filename, data, sizeof(data));
    indycommHostRamRomLoad(filename, data, sizeof(data));
    indycommHostSaveFile(filename, sizeof(data), data);
    expect(indycommHostCheckFileExists(filename, &size) == NULL,
           "disabled file-existence checks return no response");
    expect(indycommHostSendCmd(data) == NULL,
           "disabled host commands return no response");
    indycommHost7F0D0124();

    expect(init_calls == 0 && load_calls == 0 && receive_file_calls == 0 &&
               dump_calls == 0 && dump_ack_calls == 0 &&
               ramrom_load_calls == 0 && ramrom_receive_calls == 0 &&
               export_calls == 0 && export_ack_calls == 0 &&
               check_file_calls == 0 && check_file_ack_calls == 0 &&
               host_command_calls == 0 && host_command_ack_calls == 0 &&
               rmon_calls == 0,
           "disabled Indy wrappers do not issue host-side commands");

    indycomm_removed();
}

static void test_initialization_only_sends_once(void)
{
    reset_fakes();
    indy_ready = 0;

    indycommInit();
    expect(indy_ready == 1 && init_calls == 1,
           "Indy initialization marks the connection ready and sends its init packet");

    indycommInit();
    expect(init_calls == 1,
           "repeated Indy initialization does not resend the init packet");
}

static void test_enabled_wrappers_preserve_arguments_and_responses(void)
{
    char filename[] = "level.bin";
    u8 data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    u8 target[8] = {0};
    u8 *ramrom_target = (u8 *)(uintptr_t)0x12345678U;
    s32 size = 0;
    u8 *response;

    reset_fakes();
    indy_ready = 1;
    response_byte = 0x5a;
    last_response = data;
    last_response_size = 0x1234;

    indycommHostinit();
    indycommHostLoadFile(filename, target);
    indycommHostSendDump(filename, data, sizeof(data));
    indycommHostRamRomLoad(filename, ramrom_target, 0x4567);
    indycommHostSaveFile(filename, sizeof(data), data);
    response = indycommHostCheckFileExists(filename, &size);

    expect(init_calls == 1,
           "enabled host initialization sends exactly one init packet");
    expect(load_calls == 1 && last_size == sizeof(data),
           "host file loading forwards the requested load command");
    expect(receive_file_calls == 1 && last_load_target == target,
           "host file loading forwards the destination buffer");
    expect(dump_calls == 1 && dump_ack_calls == 1 && last_data == data,
           "host dump waits for its acknowledgement after forwarding data");
    expect(ramrom_load_calls == 1 && ramrom_receive_calls == 1 &&
               last_ramrom_target == ramrom_target,
           "RAM/ROM loading waits for the host response after forwarding its target");
    expect(export_calls == 1 && export_ack_calls == 1,
           "host export waits for its acknowledgement");
    expect(check_file_calls == 1 && check_file_ack_calls == 1 &&
               response == data && size == 0x1234,
           "file-existence checks return the host response and size");
}

static void test_host_command_and_rmon_routing(void)
{
    u8 command[] = "status";
    u8 response[] = {0};

    reset_fakes();
    indy_ready = 1;
    last_response = response;

    expect(indycommHostSendCmd(command) == response,
           "enabled host commands return their acknowledgement payload");
    indycommHost7F0D0124();
    indycommHostCloseConnection();

    expect(host_command_calls == 2 && host_command_ack_calls == 2 &&
               strcmp(last_host_command, "sleep 5; /etc/killall ghost gload") == 0,
           "host command routing also services the close-connection command");
    expect(rmon_calls == 1,
           "enabled Indy status requests reach the monitor service");
}

int main(void)
{
    test_disabled_connection_is_quiet();
    test_initialization_only_sends_once();
    test_enabled_wrappers_preserve_arguments_and_responses();
    test_host_command_and_rmon_routing();

    if (failures != 0)
    {
        return 1;
    }

    puts("Indy communications suite passed");
    return 0;
}
