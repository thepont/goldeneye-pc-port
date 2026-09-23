#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <sys/mman.h>

#include <ultra64.h>

#include "cheat.h"
#include "chr_b.h"
#include "chrobjdata.h"
#include "model.h"
#include "objecthandler.h"

struct ChrModelFileRecord c_item_entries[2];

static ModelFileHeader body_header;
static ModelFileHeader head_header;
static ModelNode body_switch_nodes[5];
static ModelNode *body_switches[5];
static ModelNode *wide_body_switch_node;
static ModelNode head_switch_node;
static ModelNode *head_switches[1];
static ModelNode body_root;
static ModelNode head_root;
static Model fake_model;
static union ModelRwData fake_rwdata;
static char body_filename[] = "body.z";
static char head_filename[] = "head.z";
static int fake_dk_mode;
static int file_load_calls;
static ModelFileHeader *last_file_load_header;
static char *last_file_load_name;
static int model_length_calls;
static int instantiate_calls;
static int scale_calls;
static f32 last_scale;
static int translation_scale_calls;
static f32 last_translation_scale;
static int attach_calls;
static Model *last_attached_model;
static ModelNode *last_attached_node;
static ModelFileHeader *last_attached_header;
static int node_rwdata_calls;
static ModelNode *last_rwdata_node;
static int failures;

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void expect_float(f32 actual, f32 expected, const char *message)
{
    f32 difference = actual - expected;

    if (difference < -0.0001f || difference > 0.0001f)
    {
        fprintf(stderr, "FAIL: %s (actual %.6f expected %.6f)\n",
                message, actual, expected);
        failures++;
    }
}

static void reset_fixture(void)
{
    memset(&body_header, 0, sizeof(body_header));
    memset(&head_header, 0, sizeof(head_header));
    memset(body_switch_nodes, 0, sizeof(body_switch_nodes));
    memset(body_switches, 0, sizeof(body_switches));
    memset(&head_switch_node, 0, sizeof(head_switch_node));
    memset(head_switches, 0, sizeof(head_switches));
    memset(&body_root, 0, sizeof(body_root));
    memset(&head_root, 0, sizeof(head_root));
    memset(&fake_model, 0, sizeof(fake_model));
    memset(&fake_rwdata, 0, sizeof(fake_rwdata));
    memset(c_item_entries, 0, sizeof(c_item_entries));

    if (wide_body_switch_node != NULL)
    {
        munmap(wide_body_switch_node, sizeof(*wide_body_switch_node));
    }
    wide_body_switch_node = mmap(NULL, sizeof(*wide_body_switch_node),
                                 PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (wide_body_switch_node == MAP_FAILED)
    {
        wide_body_switch_node = NULL;
    }
    expect_true(wide_body_switch_node != NULL,
                "the model test can allocate a switch node fixture");
    if (wide_body_switch_node == NULL)
    {
        return;
    }
    expect_true((uintptr_t)wide_body_switch_node > UINT32_MAX,
                "the switch fixture exercises a host pointer above 32 bits");
    memset(wide_body_switch_node, 0, sizeof(*wide_body_switch_node));

    body_header.RootNode = &body_root;
    body_header.Switches = body_switches;
    body_switches[4] = wide_body_switch_node;
    wide_body_switch_node->Opcode = 0x1234;
    wide_body_switch_node->Data = (union ModelRoData *)(uintptr_t)0x100000001ULL;
    head_header.Switches = head_switches;
    head_switches[0] = &head_switch_node;
    head_switch_node.Opcode = 0x5678;

    c_item_entries[0].header = &body_header;
    c_item_entries[0].filename = body_filename;
    c_item_entries[0].scale = 1.5f;
    c_item_entries[0].pov = 2.25f;
    c_item_entries[0].hasHead = 0;
    c_item_entries[1].header = &head_header;
    c_item_entries[1].filename = head_filename;
    c_item_entries[1].scale = 1.0f;

    body_header.numRecords = 10;
    head_header.numRecords = 3;
    head_header.numSwitches = 1;
    fake_dk_mode = 0;
    file_load_calls = 0;
    last_file_load_header = NULL;
    last_file_load_name = NULL;
    model_length_calls = 0;
    instantiate_calls = 0;
    scale_calls = 0;
    last_scale = 0.0f;
    translation_scale_calls = 0;
    last_translation_scale = 0.0f;
    attach_calls = 0;
    last_attached_model = NULL;
    last_attached_node = NULL;
    last_attached_header = NULL;
    node_rwdata_calls = 0;
    last_rwdata_node = NULL;
}

bool cheatIsActive(CHEAT_ID cheat)
{
    expect_true(cheat == CHEAT_DK_MODE,
                "character model scaling queries the dark-Knight cheat");
    return fake_dk_mode != 0;
}

void fileLoad(ModelFileHeader *header, char *name)
{
    file_load_calls++;
    last_file_load_header = header;
    last_file_load_name = name;
    header->RootNode = &body_root;
}

void modelCalculateRwDataLen(ModelFileHeader *header)
{
    (void)header;
    model_length_calls++;
}

Model *modelmgrInstantiateModelWithAnim(ModelFileHeader *header)
{
    (void)header;
    instantiate_calls++;
    return &fake_model;
}

void modelSetScale(Model *model, f32 scale)
{
    expect_true(model == &fake_model,
                "character model setup applies scale to the returned model");
    scale_calls++;
    last_scale = scale;
}

void modelSetAnimTranslationScale(Model *model, f32 scale)
{
    expect_true(model == &fake_model,
                "character model setup applies translation scale to the returned model");
    translation_scale_calls++;
    last_translation_scale = scale;
}

void modelAttachHead(Model *model, ModelNode *node, ModelFileHeader *header)
{
    attach_calls++;
    last_attached_model = model;
    last_attached_node = node;
    last_attached_header = header;
}

union ModelRwData *modelGetNodeRwData(Model *model, ModelNode *node)
{
    expect_true(model == &fake_model,
                "head visibility updates use the instantiated model");
    node_rwdata_calls++;
    last_rwdata_node = node;
    return &fake_rwdata;
}

static void test_unloaded_body_header_is_loaded_once(void)
{
    reset_fixture();
    body_header.RootNode = NULL;

    expect_true(load_body_head_if_not_loaded(0) == 1,
                "unloaded character bodies request their model file");
    expect_true(file_load_calls == 1 && last_file_load_header == &body_header &&
                    last_file_load_name == body_filename,
                "body loading passes the selected header and filename");

    file_load_calls = 0;
    expect_true(load_body_head_if_not_loaded(0) == 0 && file_load_calls == 0,
                "already-loaded character bodies do not reload their model file");
}

static void test_makeonebody_loads_head_and_preserves_model_record_count(void)
{
    Model *result;

    reset_fixture();
    head_header.RootNode = NULL;
    fake_rwdata.Switch.visible = TRUE;

    result = makeonebody(0, 1, &body_header, &head_header, 0, NULL);

    expect_true(result == &fake_model && instantiate_calls == 1,
                "character construction instantiates a missing model");
    expect_true(file_load_calls == 1 && last_file_load_header == &head_header &&
                    last_file_load_name == head_filename,
                "character construction loads a missing attached head");
    expect_true(model_length_calls == 2,
                "character construction sizes both body and attached head models");
    expect_true(body_header.numRecords == 10,
                "temporary head record accounting is restored after attachment");
    expect_true(scale_calls == 1 && translation_scale_calls == 1,
                "character construction applies both model scale parameters");
    expect_float(last_scale, 0.15000002f,
                 "character body scale converts the table value to model units");
    expect_float(last_translation_scale, 2.25f,
                 "character construction preserves the point-of-view scale");
    expect_true(attach_calls == 1 && last_attached_model == &fake_model &&
                    last_attached_header == &head_header,
                "character construction attaches the selected head to the model");
    expect_true(last_attached_node == wide_body_switch_node,
                "character construction passes the complete switch-node pointer");
    expect_true(node_rwdata_calls == 1 && last_rwdata_node == &head_switch_node &&
                    fake_rwdata.Switch.visible == FALSE,
                "character construction hides the head switch when sunglasses are absent");
}

static void test_existing_model_and_sunglasses_skip_optional_work(void)
{
    Model *result;

    reset_fixture();
    head_header.RootNode = &head_root;
    fake_dk_mode = 1;
    fake_rwdata.Switch.visible = TRUE;

    result = makeonebody(0, 1, &body_header, &head_header, 1, &fake_model);

    expect_true(result == &fake_model && instantiate_calls == 0 &&
                    file_load_calls == 0,
                "existing character models avoid redundant loading and instantiation");
    expect_float(last_scale, 0.12000002f,
                 "dark-Knight scaling applies only when the cheat is active");
    expect_true(attach_calls == 1 && node_rwdata_calls == 0 &&
                    fake_rwdata.Switch.visible == TRUE,
                "sunglasses preserve the attached head switch visibility");

    attach_calls = 0;
    result = makeonebody(0, -1, &body_header, NULL, 0, &fake_model);
    expect_true(result == &fake_model && attach_calls == 0,
                "a character without a head does not attach a null head header");
}

int main(void)
{
    test_unloaded_body_header_is_loaded_once();
    test_makeonebody_loads_head_and_preserves_model_record_count();
    test_existing_model_and_sunglasses_skip_optional_work();

    if (wide_body_switch_node != NULL)
    {
        munmap(wide_body_switch_node, sizeof(*wide_body_switch_node));
    }

    if (failures != 0)
    {
        fprintf(stderr, "%d character-model contract(s) failed\n", failures);
        return 1;
    }

    puts("character model suite passed");
    return 0;
}
