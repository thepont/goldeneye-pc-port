#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <ultra64.h>
#include "model.h"

#define MODEL_VMA 0x05000000u
#define DATA_OFFSET 0x80u
#define GROUP_OFFSET 0xA0u
#define VERTEX_OFFSET 0xC0u
#define NEXT_NODE_OFFSET 0xE0u
#define NEXT_DATA_OFFSET 0x120u
#define GDL_OFFSET 0x140u

static int failures;

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void *encoded_address(u32 offset)
{
    return (void *)(uintptr_t)(MODEL_VMA + offset);
}

static void test_model_pointer_relocation_keeps_host_width(void)
{
    unsigned char image[256] __attribute__((aligned(16)));
    ModelNode *node;
    union ModelRoData *data;

    memset(image, 0, sizeof(image));
    node = (ModelNode *)image;
    data = (union ModelRoData *)(image + DATA_OFFSET);

    /* An unknown opcode avoids dereferencing the relocated record.  This
     * isolates the contract under test: every serialized pointer must become
     * an address inside the host allocation, even when that allocation lives
     * above 4 GiB. */
    node->Opcode = 0;
    node->Data = (union ModelRoData *)encoded_address(DATA_OFFSET);

    expect_true((uintptr_t)image > UINT32_MAX,
                "the fixture exercises a genuinely 64-bit host address");

    modelPromoteNodeOffsetsToPointers(node, MODEL_VMA, (uintptr_t)image);

    expect_true(node->Data == data,
                "model relocation preserves the full host allocation pointer");
}

static void test_header_record_and_next_node_are_relocated(void)
{
    unsigned char image[512] __attribute__((aligned(16)));
    ModelNode *node;
    ModelNode *next;
    union ModelRoData *data;
    union ModelRoData *next_data;

    memset(image, 0, sizeof(image));
    node = (ModelNode *)image;
    next = (ModelNode *)(image + NEXT_NODE_OFFSET);
    data = (union ModelRoData *)(image + DATA_OFFSET);
    next_data = (union ModelRoData *)(image + NEXT_DATA_OFFSET);

    node->Opcode = MODELNODE_OPCODE_HEADER;
    node->Data = (union ModelRoData *)encoded_address(DATA_OFFSET);
    node->Next = (ModelNode *)encoded_address(NEXT_NODE_OFFSET);
    data->Header.FirstGroup =
        (ModelRoData_GroupRecord *)encoded_address(GROUP_OFFSET);

    next->Opcode = 0;
    next->Data = (union ModelRoData *)encoded_address(NEXT_DATA_OFFSET);

    modelPromoteNodeOffsetsToPointers(node, MODEL_VMA, (uintptr_t)image);

    expect_true(node->Data == data,
                "header node data points into the loaded model image");
    expect_true(data->Header.FirstGroup ==
                    (ModelRoData_GroupRecord *)(image + GROUP_OFFSET),
                "header first-group pointer is relocated from its model VMA");
    expect_true(node->Next == next,
                "node traversal relocates the following node pointer");
    expect_true(next->Data == next_data,
                "node traversal relocates the following node data pointer");
}

static void test_display_list_relocates_payload_and_base(void)
{
    unsigned char image[512] __attribute__((aligned(16)));
    ModelNode *node;
    union ModelRoData *data;
    Gfx *raw_primary;

    memset(image, 0, sizeof(image));
    node = (ModelNode *)image;
    data = (union ModelRoData *)(image + DATA_OFFSET);
    raw_primary = (Gfx *)encoded_address(GDL_OFFSET);

    node->Opcode = MODELNODE_OPCODE_DL;
    node->Data = (union ModelRoData *)encoded_address(DATA_OFFSET);
    data->DisplayList.Primary = raw_primary;
    data->DisplayList.Vertices = (Vertex *)encoded_address(VERTEX_OFFSET);

    modelPromoteNodeOffsetsToPointers(node, MODEL_VMA, (uintptr_t)image);

    expect_true(data->DisplayList.Vertices == (Vertex *)(image + VERTEX_OFFSET),
                "display-list vertex pointer is relocated to the host image");
    expect_true(data->DisplayList.BaseAddr == image,
                "display-list base retains the full host allocation pointer");
    expect_true(data->DisplayList.Primary == raw_primary,
                "display-list primary remains a segmented raw model VMA");
}

static void test_switch_array_uses_full_relocation_delta(void)
{
    unsigned char image[256] __attribute__((aligned(16)));
    ModelFileHeader header;
    ModelNode *switches[2];
    ModelNode *root;
    union ModelRoData *data;

    memset(image, 0, sizeof(image));
    memset(&header, 0, sizeof(header));
    memset(switches, 0, sizeof(switches));
    root = (ModelNode *)image;
    data = (union ModelRoData *)(image + DATA_OFFSET);

    header.RootNode = root;
    header.Switches = switches;
    header.numSwitches = 2;
    switches[0] = (ModelNode *)encoded_address(DATA_OFFSET);
    switches[1] = (ModelNode *)encoded_address(GROUP_OFFSET);
    root->Data = (union ModelRoData *)encoded_address(DATA_OFFSET);

    sub_GAME_7F075A90(&header, MODEL_VMA, (uintptr_t)image);

    expect_true(switches[0] == (ModelNode *)((u8 *)image + DATA_OFFSET),
                "first model switch pointer is relocated in place");
    expect_true(switches[1] == (ModelNode *)((u8 *)image + GROUP_OFFSET),
                "second model switch pointer is relocated in place");
    expect_true(root->Data == data,
                "sub_GAME_7F075A90 forwards the full host address to the root walk");
}

int main(void)
{
    test_model_pointer_relocation_keeps_host_width();
    test_header_record_and_next_node_are_relocated();
    test_display_list_relocates_payload_and_base();
    test_switch_array_uses_full_relocation_delta();

    if (failures != 0)
    {
        fprintf(stderr, "%d model pointer-promotion contract(s) failed\n",
                failures);
        return 1;
    }

    puts("model pointer-promotion suite passed");
    return 0;
}
