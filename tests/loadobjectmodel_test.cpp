/* Deterministic tests for prop-definition sizes and setup-stream lookups. */
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" {

struct PropDefHeaderRecord
{
    std::uint16_t extrascale;
    std::uint8_t state;
    std::uint8_t type;
};

struct coord3d
{
    float f[3];
};

struct StandTile
{
    std::uint8_t id[3];
    std::uint8_t room;
};

struct ObjectRecord;
struct PropRecord;
struct Mtxf
{
    float m[4][4];
};

struct ModelRoData_BoundingBoxRecord;
struct ModelFileHeader
{
    void *RootNode;
};
struct Model;

struct ItemModelFileRecord
{
    ModelFileHeader *header;
    char *filename;
    float scale;
};

struct stagesetup
{
    void *pathwaypoints;
    void *waypointgroups;
    std::int32_t *intro;
    PropDefHeaderRecord *propDefs;
    void *patrolpaths;
    void *ailists;
    void *pads;
    void *boundpads;
    void *padnames;
    void *boundpadnames;
};

stagesetup g_CurrentSetup{};
ItemModelFileRecord PitemZ_entries[2]{};

std::int32_t sizepropdef(PropDefHeaderRecord *pdef);
std::int32_t getposstan(coord3d *pos, StandTile *stan, float radius,
                        coord3d *posReturn, StandTile **stanReturn);
ObjectRecord *setupGetPtrToCommandByIndex(std::int32_t index);
std::int32_t tagGetCommandIndex(ObjectRecord *tag);
std::int32_t setupGetCommandIndexByProp(PropRecord *prop);
ObjectRecord *setupCommandGetObject(std::int32_t stageID, std::int32_t index);
std::int32_t modelLoad(std::int32_t modelid);
void setupUpdateObjectRoomPosition(ObjectRecord *obj);
ObjectRecord *setupFindObjForReuse(std::int32_t wanttype,
                                   ObjectRecord **offscreenobjptr,
                                   ObjectRecord **anyobjptr,
                                   bool musthaveprop,
                                   bool musthavemodel,
                                   ModelFileHeader *modeldef);
bool modelmgrCanSlotFitRwdata(Model *modelslot, ModelFileHeader *modeldef);
std::int32_t stanTestVolume(StandTile **tile, float posX, float posZ,
                            float radius, std::int32_t cdtypes,
                            float float1, float float2);
void fileLoad(ModelFileHeader *header, char *name);
void modelCalculateRwDataLen(ModelFileHeader *header);
void chrpropDeregisterRooms(PropRecord *prop);
void chrpropRegisterRooms(PropRecord *prop);
ModelRoData_BoundingBoxRecord *chrobjGetBboxFromObjectRecord(
    ObjectRecord *obj);
float chrpropSumMatrixPosX(ModelRoData_BoundingBoxRecord *bbox, Mtxf *matrix);
float chrpropSumMatrixPosY(ModelRoData_BoundingBoxRecord *bbox, Mtxf *matrix);
float chrpropSumMatrixPosZ(ModelRoData_BoundingBoxRecord *bbox, Mtxf *matrix);
float chrpropSumMatrixNegX(ModelRoData_BoundingBoxRecord *bbox, Mtxf *matrix);
float chrpropSumMatrixNegY(ModelRoData_BoundingBoxRecord *bbox, Mtxf *matrix);
float chrpropSumMatrixNegZ(ModelRoData_BoundingBoxRecord *bbox, Mtxf *matrix);
void chrpropUpdateRoomList(PropRecord *prop, coord3d *bbmin,
                           coord3d *bbmax, float radius);

}

namespace
{

constexpr std::size_t kPropTypeCount = 49;
constexpr std::size_t kObjectPropOffset = 0x10;
constexpr std::size_t kObjectFlags2Offset = 0x0c;
constexpr std::size_t kObjectModelOffset = 0x18;
constexpr std::size_t kObjectRuntimeFlagsOffset = 0x6c;
constexpr std::size_t kPropFlagsOffset = 0x01;
constexpr std::size_t kPropStanOffset = 0x20;
constexpr std::size_t kPropParentOffset = 0x30;
constexpr std::size_t kPropRoomsOffset = 0x50;
constexpr std::uint32_t kRuntimeHasProjectile = 0x00000080U;
constexpr std::uint32_t kPropStateRespawn = 0x04U;
constexpr std::uint8_t kPropFlagOnscreen = 0x02U;

int g_stan_test_calls = 0;
std::int32_t g_stan_test_result = -1;
StandTile *g_stan_replacement = nullptr;
int g_file_load_calls = 0;
int g_model_length_calls = 0;
int g_deregister_calls = 0;
int g_register_calls = 0;
int g_room_update_calls = 0;
bool g_model_fit = true;
bool g_bbox_available = false;
float g_bbox_pos_x = 0.0f;
float g_bbox_pos_y = 0.0f;
float g_bbox_pos_z = 0.0f;
float g_bbox_neg_x = 0.0f;
float g_bbox_neg_y = 0.0f;
float g_bbox_neg_z = 0.0f;
coord3d g_last_room_min{{0.0f, 0.0f, 0.0f}};
coord3d g_last_room_max{{0.0f, 0.0f, 0.0f}};
float g_last_room_radius = 0.0f;

/* This is the PC-side stride contract from tools_pc/d88_propdefs.py.  The
 * four types not emitted by the converter (0, 15, 16, 41) use the production
 * fallback header stride of one word. */
constexpr std::array<std::int32_t, kPropTypeCount> kExpectedWords = {
    1, 74, 2, 36, 38, 36, 68, 38, 40, 8,
    72, 166, 36, 62, 8, 1, 1, 36, 3, 8,
    50, 38, 6, 4, 1, 2, 2, 2, 2, 2,
    6, 1, 6, 6, 3, 6, 36, 12, 8, 52,
    52, 1, 36, 36, 10, 62, 7, 42, 1};

struct TestState
{
    int failures = 0;

    void expect(bool condition, const std::string &message)
    {
        if (!condition)
        {
            ++failures;
            std::fprintf(stderr, "FAIL: %s\n", message.c_str());
        }
    }
};

template <typename T>
void write_value(std::uint8_t *address, T value)
{
    std::memcpy(address, &value, sizeof(value));
}

void write_pointer(std::uint8_t *address, const void *value)
{
    std::memcpy(address, &value, sizeof(value));
}

void set_object_runtime_flags(PropDefHeaderRecord *header,
                              std::uint32_t flags)
{
    write_value(reinterpret_cast<std::uint8_t *>(header) +
                    kObjectRuntimeFlagsOffset,
                flags);
}

void set_object_model(PropDefHeaderRecord *header,
                      Model *model)
{
    write_pointer(reinterpret_cast<std::uint8_t *>(header) +
                      kObjectModelOffset,
                  model);
}

void set_prop_flags(std::array<std::uint8_t, 96> &prop, std::uint8_t flags)
{
    prop[kPropFlagsOffset] = flags;
}

void set_prop_parent(std::array<std::uint8_t, 96> &prop, PropRecord *parent)
{
    write_pointer(prop.data() + kPropParentOffset, parent);
}

struct SetupFixture
{
    std::vector<std::uint32_t> words;
    std::vector<std::size_t> offsets;

    explicit SetupFixture(const std::vector<std::uint8_t> &types)
    {
        std::size_t wordOffset = 0;
        offsets.reserve(types.size());
        for (std::uint8_t type : types)
        {
            offsets.push_back(wordOffset);
            wordOffset += static_cast<std::size_t>(kExpectedWords[type]);
        }

        words.assign(wordOffset, 0);
        for (std::size_t i = 0; i < types.size(); ++i)
        {
            auto *header = reinterpret_cast<PropDefHeaderRecord *>(
                words.data() + offsets[i]);
            header->type = types[i];
        }
    }

    PropDefHeaderRecord *header(std::size_t index)
    {
        return reinterpret_cast<PropDefHeaderRecord *>(
            words.data() + offsets[index]);
    }

    ObjectRecord *object(std::size_t index)
    {
        return reinterpret_cast<ObjectRecord *>(header(index));
    }

    void set_object_prop(std::size_t index, PropRecord *prop)
    {
        std::memcpy(reinterpret_cast<std::uint8_t *>(header(index)) +
                        kObjectPropOffset,
                    &prop, sizeof(prop));
    }
};

void test_every_prop_definition_stride(TestState &tests)
{
    for (std::size_t type = 0; type < kPropTypeCount; ++type)
    {
        PropDefHeaderRecord header{};
        header.type = static_cast<std::uint8_t>(type);
        const std::int32_t actual = sizepropdef(&header);
        tests.expect(actual == kExpectedWords[type],
                     "sizepropdef type " + std::to_string(type) +
                         " returns the converter stride (actual=" +
                         std::to_string(actual) + ", expected=" +
                         std::to_string(kExpectedWords[type]) + ")");
    }
}

void test_mixed_width_setup_walk(TestState &tests)
{
    const std::vector<std::uint8_t> types = {
        3, 14, 30, 39, 44, 46, 47, 48};
    SetupFixture fixture(types);
    g_CurrentSetup = {};
    g_CurrentSetup.propDefs = fixture.header(0);

    for (std::size_t i = 0; i < types.size() - 1; ++i)
    {
        tests.expect(setupGetPtrToCommandByIndex(static_cast<std::int32_t>(i)) ==
                         fixture.object(i),
                     "setup index resolves record " + std::to_string(i));
        tests.expect(tagGetCommandIndex(fixture.object(i)) ==
                         static_cast<std::int32_t>(i),
                     "tag lookup resolves record " + std::to_string(i));
    }

    tests.expect(setupGetPtrToCommandByIndex(-1) == nullptr,
                 "negative setup index is rejected");
    tests.expect(setupGetPtrToCommandByIndex(7) == nullptr,
                 "the end sentinel is not returned as a command");
    tests.expect(setupGetPtrToCommandByIndex(8) == nullptr,
                 "index after the end sentinel is rejected");
    tests.expect(tagGetCommandIndex(reinterpret_cast<ObjectRecord *>(
                         reinterpret_cast<std::uint8_t *>(fixture.header(0)) +
                         fixture.words.size() * sizeof(std::uint32_t))) == -1,
                 "tag lookup rejects a pointer outside the stream");
}

void test_prop_lookup_and_object_filter(TestState &tests)
{
    const std::vector<std::uint8_t> types = {3, 14, 30, 39, 44, 46, 47, 48};
    SetupFixture fixture(types);
    PropRecord *firstProp = reinterpret_cast<PropRecord *>(0x1111);
    PropRecord *secondProp = reinterpret_cast<PropRecord *>(0x2222);
    PropRecord *thirdProp = reinterpret_cast<PropRecord *>(0x3333);
    fixture.set_object_prop(0, firstProp);
    fixture.set_object_prop(3, secondProp);
    fixture.set_object_prop(6, thirdProp);

    g_CurrentSetup = {};
    g_CurrentSetup.propDefs = fixture.header(0);

    tests.expect(setupGetCommandIndexByProp(firstProp) == 0,
                 "prop lookup finds the first object record");
    tests.expect(setupGetCommandIndexByProp(secondProp) == 3,
                 "prop lookup crosses widened records");
    tests.expect(setupGetCommandIndexByProp(thirdProp) == 6,
                 "prop lookup finds the final object record");
    PropRecord *missingProp = reinterpret_cast<PropRecord *>(0x4444);
    tests.expect(setupGetCommandIndexByProp(missingProp) == -1,
                 "prop lookup returns -1 for an absent prop");

    for (std::size_t i : {std::size_t(0), std::size_t(3), std::size_t(6)})
    {
        tests.expect(setupCommandGetObject(0, static_cast<std::int32_t>(i)) ==
                         fixture.object(i),
                     "object lookup returns object type at index " +
                         std::to_string(i));
    }
    for (std::size_t i : {std::size_t(1), std::size_t(2), std::size_t(4),
                          std::size_t(5)})
    {
        tests.expect(setupCommandGetObject(0, static_cast<std::int32_t>(i)) ==
                         nullptr,
                     "object lookup rejects non-object type at index " +
                         std::to_string(i));
    }
    tests.expect(setupCommandGetObject(0, -1) == nullptr,
                 "object lookup rejects a negative index");

    g_CurrentSetup.propDefs = nullptr;
    tests.expect(setupGetPtrToCommandByIndex(0) == nullptr,
                 "setup lookup handles an absent prop-definition stream");
    tests.expect(tagGetCommandIndex(nullptr) == -1,
                 "tag lookup handles an absent prop-definition stream");
    tests.expect(setupGetCommandIndexByProp(firstProp) == -1,
                 "prop lookup handles an absent prop-definition stream");
}

void test_getposstan_contract(TestState &tests)
{
    coord3d input{{10.0f, 20.0f, 30.0f}};
    coord3d output{{0.0f, 0.0f, 0.0f}};
    StandTile tile{{0, 0, 0}, 7};
    StandTile replacement{{0, 0, 0}, 9};
    StandTile *returned = &replacement;

    g_stan_test_calls = 0;
    g_stan_replacement = nullptr;
    tests.expect(getposstan(&input, nullptr, 25.0f, &output, &returned) == 0,
                 "getposstan rejects a missing stand tile");
    tests.expect(output.f[0] == input.f[0] && output.f[1] == input.f[1] &&
                     output.f[2] == input.f[2],
                 "getposstan always copies the requested position");
    tests.expect(returned == nullptr,
                 "getposstan clears the returned tile for a missing tile");
    tests.expect(g_stan_test_calls == 0,
                 "getposstan does not collision-test a missing tile");

    returned = nullptr;
    tests.expect(getposstan(&input, &tile, 0.0f, &output, &returned) == 1,
                 "getposstan accepts zero-radius movement");
    tests.expect(returned == &tile,
                 "getposstan preserves the input tile without a radius test");
    tests.expect(g_stan_test_calls == 0,
                 "zero-radius movement skips stanTestVolume");

    g_stan_test_result = 0;
    g_stan_replacement = &replacement;
    returned = nullptr;
    tests.expect(getposstan(&input, &tile, 5.0f, &output, &returned) == 0,
                 "getposstan rejects a colliding positive-radius position");
    tests.expect(g_stan_test_calls == 1,
                 "positive-radius movement calls stanTestVolume once");
    tests.expect(returned == &replacement,
                 "getposstan preserves a collision helper tile update");

    g_stan_test_result = -1;
    g_stan_replacement = nullptr;
    returned = nullptr;
    tests.expect(getposstan(&input, &tile, 5.0f, &output, &returned) == 1,
                 "getposstan accepts a clear positive-radius position");
    tests.expect(g_stan_test_calls == 2,
                 "each positive-radius request collision-tests once");
    tests.expect(returned == &tile,
                 "a clear test retains the original tile");
}

void test_model_load_contract(TestState &tests)
{
    ModelFileHeader header{};
    char filename[] = "test-model.z";
    PitemZ_entries[0].header = &header;
    PitemZ_entries[0].filename = filename;

    g_file_load_calls = 0;
    g_model_length_calls = 0;
    header.RootNode = nullptr;
    tests.expect(modelLoad(0) == 1,
                 "modelLoad loads a model whose root is not resident");
    tests.expect(g_file_load_calls == 1 && g_model_length_calls == 1,
                 "modelLoad performs file load and runtime-length calculation");

    header.RootNode = reinterpret_cast<void *>(0x1234);
    tests.expect(modelLoad(0) == 0,
                 "modelLoad does not reload an already resident model");
    tests.expect(g_file_load_calls == 1 && g_model_length_calls == 1,
                 "resident model loading has no side effects");

    tests.expect(modelLoad(-1) == 0,
                 "modelLoad rejects a negative model index before table access");
    tests.expect(modelLoad(341) == 0,
                 "modelLoad rejects an index past the converted model table");
}

void test_room_position_update_contract(TestState &tests)
{
    std::array<std::uint8_t, 160> object{};
    std::array<std::uint8_t, 96> prop{};
    StandTile tile{{0, 0, 0}, 23};
    write_pointer(object.data() + kObjectPropOffset, prop.data());
    write_value(object.data() + kObjectFlags2Offset, 0x00020000U);
    write_pointer(prop.data() + kPropStanOffset, &tile);
    prop[kPropRoomsOffset] = 11;
    prop[kPropRoomsOffset + 1] = 12;

    g_deregister_calls = 0;
    g_register_calls = 0;
    setupUpdateObjectRoomPosition(reinterpret_cast<ObjectRecord *>(
        object.data()));
    tests.expect(g_deregister_calls == 1 && g_register_calls == 1,
                 "room update deregisters and re-registers the prop");
    tests.expect(prop[kPropRoomsOffset] == 23 &&
                     prop[kPropRoomsOffset + 1] == 0xff,
                 "room update copies the stand room and clears the second room");

    write_pointer(prop.data() + kPropStanOffset, nullptr);
    prop[kPropRoomsOffset] = 7;
    prop[kPropRoomsOffset + 1] = 8;
    setupUpdateObjectRoomPosition(reinterpret_cast<ObjectRecord *>(
        object.data()));
    tests.expect(prop[kPropRoomsOffset] == 0xff,
                 "room update uses the invalid-room marker without a stand");
    tests.expect(prop[kPropRoomsOffset + 1] == 8,
                 "room update preserves the second room without a stand");
}

void test_room_position_update_uses_bounding_box_radius(TestState &tests)
{
    std::array<std::uint8_t, 160> object{};
    std::array<std::uint8_t, 96> prop{};
    write_pointer(object.data() + kObjectPropOffset, prop.data());
    write_value(object.data() + kObjectFlags2Offset, 0U);
    write_pointer(prop.data() + kPropStanOffset, nullptr);

    g_bbox_available = true;
    g_bbox_pos_x = 0.0f;
    g_bbox_pos_y = 10.0f;
    g_bbox_pos_z = -10.0f;
    g_bbox_neg_x = 60.0f;
    g_bbox_neg_y = -20.0f;
    g_bbox_neg_z = 100.0f;
    g_deregister_calls = 0;
    g_register_calls = 0;
    g_room_update_calls = 0;
    g_last_room_min = {{0.0f, 0.0f, 0.0f}};
    g_last_room_max = {{0.0f, 0.0f, 0.0f}};
    g_last_room_radius = 0.0f;

    setupUpdateObjectRoomPosition(reinterpret_cast<ObjectRecord *>(
        object.data()));

    tests.expect(g_deregister_calls == 1 && g_register_calls == 1,
                 "bounding-box room update still brackets the room rewrite");
    tests.expect(g_room_update_calls == 1,
                 "non-stand objects update rooms from their model bounds");
    tests.expect(g_last_room_radius == 130.0f,
                 "room update radius uses the largest horizontal bound");
    tests.expect(g_last_room_min.f[0] == -30.0f &&
                     g_last_room_min.f[1] == 10.0f &&
                     g_last_room_min.f[2] == -40.0f &&
                     g_last_room_max.f[0] == 90.0f &&
                     g_last_room_max.f[1] == -20.0f &&
                     g_last_room_max.f[2] == 130.0f,
                 "room update forwards translated bounding-box corners");

    g_bbox_available = false;
}

void test_object_reuse_filters_runtime_state(TestState &tests)
{
    const std::vector<std::uint8_t> types = {3, 3, 3, 3, 3, 3, 48};
    SetupFixture fixture(types);
    std::array<std::array<std::uint8_t, 96>, 6> props{};
    std::array<PropRecord *, 6> propPointers{};
    for (std::size_t i = 0; i < props.size(); ++i)
    {
        propPointers[i] = reinterpret_cast<PropRecord *>(props[i].data());
        fixture.set_object_prop(i, propPointers[i]);
    }

    /* 0: projectile, 1: respawning, 2: parented, 3: valid/on-screen,
     * 4: valid/off-screen, 5: valid but model-fit is rejected. */
    set_object_runtime_flags(fixture.header(0), kRuntimeHasProjectile);
    fixture.header(1)->state = static_cast<std::uint8_t>(kPropStateRespawn);
    set_prop_parent(props[2], propPointers[0]);
    set_prop_flags(props[3], kPropFlagOnscreen);
    set_prop_flags(props[4], 0);
    set_prop_flags(props[5], 0);
    set_object_model(fixture.header(5), reinterpret_cast<Model *>(0x7777));

    g_CurrentSetup = {};
    g_CurrentSetup.propDefs = fixture.header(0);
    ObjectRecord *offscreen = nullptr;
    ObjectRecord *any = nullptr;
    g_model_fit = true;
    tests.expect(setupFindObjForReuse(3, &offscreen, &any, true, false,
                                      nullptr) == nullptr,
                 "object reuse reports no direct result when scanning props");
    tests.expect(offscreen == fixture.object(4),
                 "object reuse selects the first valid off-screen object");
    tests.expect(any == fixture.object(3),
                 "object reuse selects the first valid object overall");

    offscreen = nullptr;
    any = nullptr;
    g_model_fit = false;
    tests.expect(setupFindObjForReuse(3, &offscreen, &any, true, true,
                                      reinterpret_cast<ModelFileHeader *>(
                                          0x8888)) == nullptr,
                 "object reuse rejects candidates that cannot fit the model");
    tests.expect(offscreen == nullptr && any == nullptr,
                 "model-fit rejection leaves both output candidates empty");
    g_model_fit = true;
}

void test_object_reuse_reads_the_type_byte(TestState &tests)
{
    /* A normal PC-converted header has its type in byte 3.  The low byte of
     * the first u32 is extrascale, so walking it as `(u8)cmd[0]` cannot find
     * an object and cannot recognize the real end sentinel.  The guard word
     * makes the pre-fix walk terminate deterministically instead of reading
     * beyond the fixture. */
    SetupFixture fixture({3, 48});
    fixture.words.push_back(0);
    fixture.header(0)->extrascale = 0x0100;
    fixture.header(1)->extrascale = 0x0100;
    auto *guard = fixture.header(1) + kExpectedWords[48];
    guard->extrascale = 48;
    guard->type = 0;

    g_CurrentSetup = {};
    g_CurrentSetup.propDefs = fixture.header(0);
    ObjectRecord *offscreen = nullptr;
    ObjectRecord *any = nullptr;
    ObjectRecord *reused = setupFindObjForReuse(
        3, &offscreen, &any, false, false, nullptr);

    tests.expect(reused == fixture.object(0),
                 "object reuse finds a PC-native record by prop-definition type");
}

} // namespace

extern "C" std::int32_t stanTestVolume(StandTile **tile, float posX,
                                         float posZ, float radius,
                                         std::int32_t cdtypes, float float1,
                                         float float2)
{
    (void)posX;
    (void)posZ;
    (void)radius;
    (void)cdtypes;
    (void)float1;
    (void)float2;
    ++g_stan_test_calls;
    if (g_stan_replacement != nullptr)
    {
        *tile = g_stan_replacement;
    }
    return g_stan_test_result;
}

extern "C" void fileLoad(ModelFileHeader *, char *)
{
    ++g_file_load_calls;
}

extern "C" void modelCalculateRwDataLen(ModelFileHeader *)
{
    ++g_model_length_calls;
}

extern "C" bool modelmgrCanSlotFitRwdata(Model *, ModelFileHeader *)
{
    return g_model_fit;
}

extern "C" void chrpropDeregisterRooms(PropRecord *)
{
    ++g_deregister_calls;
}

extern "C" void chrpropRegisterRooms(PropRecord *)
{
    ++g_register_calls;
}

extern "C" ModelRoData_BoundingBoxRecord *chrobjGetBboxFromObjectRecord(
    ObjectRecord *)
{
    return g_bbox_available
               ? reinterpret_cast<ModelRoData_BoundingBoxRecord *>(0x1)
               : nullptr;
}

extern "C" float chrpropSumMatrixPosX(ModelRoData_BoundingBoxRecord *, Mtxf *)
{
    return g_bbox_pos_x;
}

extern "C" float chrpropSumMatrixPosY(ModelRoData_BoundingBoxRecord *, Mtxf *)
{
    return g_bbox_pos_y;
}

extern "C" float chrpropSumMatrixPosZ(ModelRoData_BoundingBoxRecord *, Mtxf *)
{
    return g_bbox_pos_z;
}

extern "C" float chrpropSumMatrixNegX(ModelRoData_BoundingBoxRecord *, Mtxf *)
{
    return g_bbox_neg_x;
}

extern "C" float chrpropSumMatrixNegY(ModelRoData_BoundingBoxRecord *, Mtxf *)
{
    return g_bbox_neg_y;
}

extern "C" float chrpropSumMatrixNegZ(ModelRoData_BoundingBoxRecord *, Mtxf *)
{
    return g_bbox_neg_z;
}

extern "C" void chrpropUpdateRoomList(PropRecord *, coord3d *min,
                                        coord3d *max, float radius)
{
    ++g_room_update_calls;
    g_last_room_min = *min;
    g_last_room_max = *max;
    g_last_room_radius = radius;
}

int main()
{
    TestState tests;
    test_every_prop_definition_stride(tests);
    test_mixed_width_setup_walk(tests);
    test_prop_lookup_and_object_filter(tests);
    test_getposstan_contract(tests);
    test_model_load_contract(tests);
    test_room_position_update_contract(tests);
    test_room_position_update_uses_bounding_box_radius(tests);
    test_object_reuse_filters_runtime_state(tests);
    test_object_reuse_reads_the_type_byte(tests);

    if (tests.failures != 0)
    {
        std::fprintf(stderr, "%d loadobjectmodel contract(s) failed\n",
                     tests.failures);
        return 1;
    }
    std::puts("Load-object-model unit suite passed");
    return 0;
}
