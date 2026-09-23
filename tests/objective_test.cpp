/* Deterministic tests for objective/tag list ownership and lookup. */
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{

constexpr int kObjectiveCount = 10;
constexpr int kLevelTitle = 90;

struct FakeTag
{
    std::uint32_t header = 0;
    std::uint16_t id = 0;
    std::int16_t offset = 0;
    FakeTag *next = nullptr;
    void *tagged_object = nullptr;
};

struct FakeBriefingText
{
    std::uint32_t first = 0;
    std::uint32_t second = 0;
    std::uint32_t third = 0;
    FakeBriefingText *next = nullptr;
};

struct FakeRoomCriteria
{
    std::uint32_t id = 0;
    std::uint32_t pad = 0;
    std::uint32_t status = 0;
    FakeRoomCriteria *next = nullptr;
};

struct FakeDepositCriteria
{
    std::int32_t first = 0;
    std::int32_t weapon = 0;
    std::int32_t pad = 0;
    std::int32_t flag = 0;
    FakeDepositCriteria *next = nullptr;
};

struct FakePictureCriteria
{
    std::int32_t first = 0;
    std::int32_t tag = 0;
    std::int32_t flag = 0;
    FakePictureCriteria *next = nullptr;
};

extern "C" {
extern void *ptr_last_tag_entry_type16;
}

struct FakeObjective
{
    std::uint32_t id = 0;
    std::int32_t menu = 0;
};

struct FakeObject
{
    // ObjectRecord.runtime_bitflags is 0x6c in the host build.  The original
    // N64 offset is 0x64; the widened host pointers in bondtypes.h account for
    // the eight-byte difference.
    std::array<std::uint8_t, 0x6c> prefix = {};
    std::uint32_t runtime_bitflags = 0;
};

/* These raw fixtures mirror the host layouts used by objective_status.c.
 * Keeping the fixtures byte-oriented makes the test independent from the
 * decomp's C-only `inherits` declarations while still exercising the real
 * production field offsets. */
struct RawProp
{
    std::array<std::uint8_t, 0x80> bytes{};
};

struct RawObject
{
    std::array<std::uint8_t, 0x100> bytes{};
};

struct FakeStand
{
    std::array<std::uint8_t, 3> id{};
    std::uint8_t room = 0;
};

struct FakePad
{
    std::array<std::uint8_t, 40> prefix{};
    void *plink = nullptr;
    FakeStand *stan = nullptr;
};

struct FakeStageSetup
{
    void *prefix[6]{};
    FakePad *pads = nullptr;
    FakePad *boundpads = nullptr;
    void *padnames = nullptr;
    void *boundpadnames = nullptr;
};

struct FakeObjectiveBuffer
{
    std::array<std::uint8_t, 24 * 16> bytes{};
};

struct FakeRect
{
    float left = 10.0f;
    float up = 10.0f;
    float right = 90.0f;
    float down = 90.0f;
};

struct ObjectiveRuntime
{
    bool stage_flag = false;
    bool keyanalyzer = false;
    bool debug_all_complete = false;
    bool healthy = true;
    bool inventory = false;
    bool bounds = true;
    int selected_difficulty = 0;
    float screen_left = 0.0f;
    float screen_top = 0.0f;
    float screen_width = 100.0f;
    float screen_height = 100.0f;
    FakeRect projected;
    int hud_count = 0;
    std::string last_hud;
};

ObjectiveRuntime g_objective_runtime;

constexpr std::size_t kObjectiveCriterionStride = 24;
constexpr std::uint32_t kNullTextID = 0x7fffffffU;

enum ObjectivePropType : std::uint8_t
{
    kObjectiveStart = 23,
    kObjectiveEnd = 24,
    kDestroyObject = 25,
    kCompleteCondition = 26,
    kFailCondition = 27,
    kCollectObject = 28,
    kDepositObject = 29,
    kPhotograph = 30,
    kObjectiveNull = 31,
    kEnterRoom = 32,
    kDepositObjectInRoom = 33,
    kCopyItem = 34
};

void write_u32(std::uint8_t *address, std::uint32_t value)
{
    std::memcpy(address, &value, sizeof(value));
}

void write_i32(std::uint8_t *address, std::int32_t value)
{
    std::memcpy(address, &value, sizeof(value));
}

void write_f32(std::uint8_t *address, float value)
{
    std::memcpy(address, &value, sizeof(value));
}

void write_ptr(std::uint8_t *address, const void *value)
{
    std::memcpy(address, &value, sizeof(value));
}

void set_criterion(FakeObjectiveBuffer &objective,
                   int index,
                   std::uint8_t type,
                   std::int32_t object_reference = 7,
                   std::uint32_t text_id = 1,
                   std::int32_t minimum_difficulty = 1)
{
    std::uint8_t *record = objective.bytes.data() + index * kObjectiveCriterionStride;
    record[3] = type;
    write_i32(record + 4, object_reference);
    write_u32(record + 8, text_id);
    write_i32(record + 12, minimum_difficulty);
}

void set_objective_difficulty(FakeObjectiveBuffer &objective, std::int8_t difficulty)
{
    objective.bytes[12] = static_cast<std::uint8_t>(difficulty);
}

void set_raw_object_prop(RawObject &object, RawProp *prop, bool active = true)
{
    write_ptr(object.bytes.data() + 0x10, prop == nullptr ? nullptr : prop->bytes.data());
    write_u32(object.bytes.data() + 0x6c, active ? 0x10U : 0U);
}

void set_raw_prop(RawProp &prop, std::uint8_t flags, float z_depth)
{
    prop.bytes[1] = flags;
    write_f32(prop.bytes.data() + 0x28, z_depth);
}

void install_tag(FakeTag &tag, RawObject *object, std::uint16_t id = 7)
{
    tag = {};
    tag.id = id;
    tag.tagged_object = object == nullptr ? nullptr : object->bytes.data();
    ptr_last_tag_entry_type16 = &tag;
}

struct PictureFixture
{
    FakePictureCriteria criteria{};
    FakeTag tag{};
    RawObject object{};
    RawProp prop{};
};

static_assert(offsetof(FakeTag, next) == 8);
static_assert(offsetof(FakeTag, tagged_object) == 16);
static_assert(offsetof(FakeBriefingText, next) == 16);
static_assert(offsetof(FakeRoomCriteria, next) == 16);
static_assert(offsetof(FakeDepositCriteria, next) == 16);
static_assert(offsetof(FakePictureCriteria, next) == 16);

struct TestState
{
    int failures = 0;

    void expect(bool condition, const char *message)
    {
        if (!condition)
        {
            ++failures;
            std::fprintf(stderr, "FAIL: %s\n", message);
        }
    }
};

} // namespace

extern "C" {
void something_with_stage_objectives();
void set_parent_cur_tag_entry(void *entry);
void setup_briefing_text_entry_parent(void *entry);
void set_parent_cur_obj_enter_room(void *entry);
void set_parent_cur_obj_deposited_in_room(void *entry);
void set_parent_cur_obj_photograph(void *entry);
void add_ptr_to_objective(void *objective);
void *sub_GAME_7F057080(std::int32_t tag_id);
void *objFindByTagId(std::int32_t tag_id);
std::int32_t objectiveGetCount();
unsigned char *get_ptr_text_for_watch_breifing_page(int page);
unsigned char *get_text_for_objective(int objective_index);
std::int32_t get_difficulty_for_objective(std::int32_t objective_index);
std::int32_t get_status_of_objective(std::int32_t objective_index);
std::int32_t objectiveIsAllComplete();
void display_objective_status_text_on_status_change();
void objectivestatusCheckRoomEntered(std::int32_t room_id);
void objectivestatusCheckDeposit(std::int32_t weapon_num, std::int32_t room_id);
void objectiveTakePictureHandler();

extern void *objective_ptrs[kObjectiveCount];
extern std::int32_t objectiveStatuses[kObjectiveCount];
extern void *ptr_last_tag_entry_type16;
extern void *ptr_last_briefing_setup_entry_type23;
extern void *ptr_last_enter_room_subobject_entry_type20;
extern void *ptr_last_deposit_in_room_subobject_entry_type21;
extern void *ptr_last_photo_obj_in_room_subobject_entry_type1E;
extern std::int32_t objective_count;
extern std::int32_t objective_status_display_disabled;
extern int g_savelogObjOnce;

FakeStageSetup g_CurrentSetup{};
std::int32_t j_text_trigger = 0;

std::int32_t sizepropdef(void *)
{
    return static_cast<std::int32_t>(kObjectiveCriterionStride / sizeof(std::uint32_t));
}

unsigned char *langGet(std::int32_t slot_id)
{
    static char direct_text[] = "direct text";
    static char briefing_text[] = "briefing text";
    static char error_text[] = "E R R O R\n";
    static char no_briefing_text[] = "no briefing for this mission\n";
    static char newline_text[] = "\n";
    static char objective_text[] = "objective";
    static char completed_text[] = "completed\n";
    static char incomplete_text[] = "incomplete\n";
    static char failed_text[] = "failed\n";

    if (static_cast<std::uint32_t>(slot_id) == kNullTextID)
    {
        return nullptr;
    }
    switch (slot_id & 0x3ff)
    {
        case 41: return reinterpret_cast<unsigned char *>(error_text);
        case 42: return reinterpret_cast<unsigned char *>(no_briefing_text);
        case 43: return reinterpret_cast<unsigned char *>(newline_text);
        case 44: return reinterpret_cast<unsigned char *>(objective_text);
        case 45: return reinterpret_cast<unsigned char *>(completed_text);
        case 46: return reinterpret_cast<unsigned char *>(incomplete_text);
        case 47: return reinterpret_cast<unsigned char *>(failed_text);
        case 77: return reinterpret_cast<unsigned char *>(briefing_text);
        default: return reinterpret_cast<unsigned char *>(direct_text);
    }
}

std::int32_t chrHasStageFlag(void *, std::int32_t)
{
    return g_objective_runtime.stage_flag ? 1 : 0;
}

std::int32_t bondinvHasPropInInv(void *)
{
    return g_objective_runtime.inventory ? 1 : 0;
}

std::int32_t get_keyanalyzer_flag()
{
    return g_objective_runtime.keyanalyzer ? 1 : 0;
}

std::int32_t get_debug_all_obj_complete_flag()
{
    return g_objective_runtime.debug_all_complete ? 1 : 0;
}

std::int32_t lvlGetSelectedDifficulty()
{
    return g_objective_runtime.selected_difficulty;
}

std::int32_t objIsHealthy(void *)
{
    return g_objective_runtime.healthy ? 1 : 0;
}

std::int32_t objGetOnscreenRenderBounds(void *, void *, void *, void *)
{
    return g_objective_runtime.bounds ? 1 : 0;
}

void projectRectCornersTo2D(void *, void *, void *, void *right, void *left)
{
    const FakeRect &rect = g_objective_runtime.projected;
    auto *right_floats = static_cast<float *>(right);
    auto *left_floats = static_cast<float *>(left);
    right_floats[0] = rect.right;
    right_floats[1] = rect.down;
    left_floats[0] = rect.left;
    left_floats[1] = rect.up;
}

float getPlayer_c_screenleft()
{
    return g_objective_runtime.screen_left;
}

float getPlayer_c_screentop()
{
    return g_objective_runtime.screen_top;
}

float getPlayer_c_screenwidth()
{
    return g_objective_runtime.screen_width;
}

float getPlayer_c_screenheight()
{
    return g_objective_runtime.screen_height;
}

void hudmsgBottomShow(char *message)
{
    ++g_objective_runtime.hud_count;
    g_objective_runtime.last_hud = message == nullptr ? "" : message;
}

std::int32_t osSyncPrintf(const char *, ...)
{
    return 0;
}

std::int32_t g_fake_stage = 0;
std::int32_t bossGetStageNum()
{
    return g_fake_stage;
}
}

namespace
{

void clear_objective_state()
{
    for (void *&entry : objective_ptrs)
    {
        entry = nullptr;
    }
    for (std::int32_t &status : objectiveStatuses)
    {
        status = 0;
    }
    ptr_last_tag_entry_type16 = nullptr;
    ptr_last_briefing_setup_entry_type23 = nullptr;
    ptr_last_enter_room_subobject_entry_type20 = nullptr;
    ptr_last_deposit_in_room_subobject_entry_type21 = nullptr;
    ptr_last_photo_obj_in_room_subobject_entry_type1E = nullptr;
    objective_count = -1;
    objective_status_display_disabled = 0;
    g_objective_runtime = {};
    g_CurrentSetup = {};
    j_text_trigger = 0;
}

void install_objective(FakeObjectiveBuffer &objective)
{
    objective_ptrs[0] = objective.bytes.data();
    objective_count = 0;
}

int status_for_criterion(std::uint8_t type,
                         std::uint32_t text_id = 1,
                         std::int32_t minimum_difficulty = 1,
                         bool with_object = false,
                         bool with_prop = true,
                         bool healthy = true)
{
    clear_objective_state();
    FakeObjectiveBuffer objective;
    set_criterion(objective, 0, type, 7, text_id, minimum_difficulty);
    set_criterion(objective, 1, kObjectiveEnd);
    install_objective(objective);
    g_objective_runtime.healthy = healthy;

    RawObject object;
    RawProp prop;
    FakeTag tag;
    set_raw_prop(prop, 0, 1.0f);
    if (with_object)
    {
        set_raw_object_prop(object, with_prop ? &prop : nullptr);
        install_tag(tag, &object);
    }
    return get_status_of_objective(0);
}

void test_stage_reset(TestState &tests)
{
    clear_objective_state();
    objective_count = 5;
    for (std::int32_t &status : objectiveStatuses)
    {
        status = 2;
    }
    FakeTag tag;
    FakeBriefingText briefing;
    FakeRoomCriteria room;
    FakeDepositCriteria deposit;
    FakePictureCriteria picture;
    objective_ptrs[0] = &tag;
    ptr_last_tag_entry_type16 = &tag;
    ptr_last_briefing_setup_entry_type23 = &briefing;
    ptr_last_enter_room_subobject_entry_type20 = &room;
    ptr_last_deposit_in_room_subobject_entry_type21 = &deposit;
    ptr_last_photo_obj_in_room_subobject_entry_type1E = &picture;

    g_fake_stage = 1;
    something_with_stage_objectives();

    tests.expect(objective_count == -1,
                 "stage reset restores the sentinel objective count");
    for (std::int32_t status : objectiveStatuses)
    {
        tests.expect(status == 0,
                     "stage reset marks every objective incomplete");
    }
    for (void *entry : objective_ptrs)
    {
        tests.expect(entry == nullptr,
                     "stage reset clears every objective pointer");
    }
    tests.expect(ptr_last_tag_entry_type16 == nullptr,
                 "stage reset clears the tag parent list");
    tests.expect(ptr_last_briefing_setup_entry_type23 == nullptr,
                 "stage reset clears the briefing parent list");
    tests.expect(ptr_last_enter_room_subobject_entry_type20 == nullptr,
                 "stage reset clears the room parent list");
    tests.expect(ptr_last_deposit_in_room_subobject_entry_type21 == nullptr,
                 "stage reset clears the deposit parent list");
    tests.expect(ptr_last_photo_obj_in_room_subobject_entry_type1E == nullptr,
                 "stage reset clears the photograph parent list");

    objective_count = 7;
    objectiveStatuses[0] = 1;
    objective_ptrs[0] = &tag;
    ptr_last_tag_entry_type16 = &tag;
    g_fake_stage = kLevelTitle;
    something_with_stage_objectives();
    tests.expect(objective_count == 7,
                 "title reset preserves the objective count");
    tests.expect(objectiveStatuses[0] == 1,
                 "title reset preserves objective status values");
    tests.expect(objective_ptrs[0] == nullptr,
                 "title reset still clears objective pointers");
    tests.expect(ptr_last_tag_entry_type16 == nullptr,
                 "title reset still clears parent lists");
}

void test_parent_lists_and_tag_lookup(TestState &tests)
{
    clear_objective_state();
    FakeTag first;
    first.id = 12;
    FakeTag second;
    second.id = 34;
    set_parent_cur_tag_entry(&first);
    set_parent_cur_tag_entry(&second);
    tests.expect(ptr_last_tag_entry_type16 == &second,
                 "tag parent insertion updates the head");
    tests.expect(second.next == &first,
                 "tag parent insertion links the previous head");
    tests.expect(sub_GAME_7F057080(12) == &first,
                 "tag lookup finds a non-head ID");
    tests.expect(sub_GAME_7F057080(34) == &second,
                 "tag lookup finds the head ID");
    tests.expect(sub_GAME_7F057080(99) == nullptr,
                 "tag lookup returns NULL for a missing ID");

    FakeObject object;
    second.tagged_object = &object;
    object.runtime_bitflags = 0x10;
    tests.expect(objFindByTagId(34) == &object,
                 "tagged-object lookup returns an active object");
    object.runtime_bitflags = 0;
    tests.expect(objFindByTagId(34) == nullptr,
                 "tagged-object lookup hides an inactive object");
}

void test_objective_and_child_registration(TestState &tests)
{
    clear_objective_state();
    FakeObjective first;
    first.menu = 2;
    FakeObjective second;
    second.menu = 5;
    add_ptr_to_objective(&first);
    add_ptr_to_objective(&second);
    tests.expect(objective_ptrs[2] == &first,
                 "objective registration stores the first menu entry");
    tests.expect(objective_ptrs[5] == &second,
                 "objective registration stores the second menu entry");
    tests.expect(objective_count == 5,
                 "objective registration tracks the highest menu index");
    tests.expect(objectiveGetCount() == 6,
                 "objective count exposes the inclusive menu count");

    FakeBriefingText briefing_a;
    FakeBriefingText briefing_b;
    setup_briefing_text_entry_parent(&briefing_a);
    setup_briefing_text_entry_parent(&briefing_b);
    tests.expect(briefing_b.next == &briefing_a,
                 "briefing entries are registered newest-first");

    FakeRoomCriteria room_a;
    FakeRoomCriteria room_b;
    set_parent_cur_obj_enter_room(&room_a);
    set_parent_cur_obj_enter_room(&room_b);
    tests.expect(room_b.next == &room_a,
                 "room criteria are registered newest-first");

    FakeDepositCriteria deposit_a;
    FakeDepositCriteria deposit_b;
    set_parent_cur_obj_deposited_in_room(&deposit_a);
    set_parent_cur_obj_deposited_in_room(&deposit_b);
    tests.expect(deposit_b.next == &deposit_a,
                 "deposit criteria are registered newest-first");

    FakePictureCriteria picture_a;
    FakePictureCriteria picture_b;
    set_parent_cur_obj_photograph(&picture_a);
    set_parent_cur_obj_photograph(&picture_b);
    tests.expect(picture_b.next == &picture_a,
                 "photograph criteria are registered newest-first");
}

void test_objective_text_and_index_boundaries(TestState &tests)
{
    clear_objective_state();

    tests.expect(std::strcmp(reinterpret_cast<char *>(get_ptr_text_for_watch_breifing_page(0)),
                             "E R R O R\n") == 0,
                 "missing page zero briefing uses the error text");
    tests.expect(std::strcmp(reinterpret_cast<char *>(get_ptr_text_for_watch_breifing_page(1)),
                             "no briefing for this mission\n") == 0,
                 "missing page one briefing uses the no-briefing text");
    tests.expect(std::strcmp(reinterpret_cast<char *>(get_ptr_text_for_watch_breifing_page(2)),
                             "\n") == 0,
                 "missing other briefing pages use a newline");

    FakeBriefingText match;
    match.second = 7;
    match.third = 77;
    FakeBriefingText head;
    head.second = 99;
    head.next = &match;
    ptr_last_briefing_setup_entry_type23 = &head;
    tests.expect(std::strcmp(reinterpret_cast<char *>(get_ptr_text_for_watch_breifing_page(7)),
                             "briefing text") == 0,
                 "briefing lookup walks the newest-first list");
    match.third = kNullTextID;
    tests.expect(std::strcmp(reinterpret_cast<char *>(get_ptr_text_for_watch_breifing_page(7)),
                             "\n") == 0,
                 "a missing language slot falls back after a matching entry");

    FakeObjectiveBuffer objective;
    set_criterion(objective, 0, kObjectiveEnd, 0, 123, 0);
    set_objective_difficulty(objective, 3);
    install_objective(objective);
    tests.expect(get_text_for_objective(-1) == nullptr,
                 "negative objective text indexes are rejected");
    tests.expect(get_text_for_objective(0) != nullptr,
                 "registered objective text is resolved");
    tests.expect(get_text_for_objective(1) == nullptr,
                 "empty objective text slots return null");
    tests.expect(get_text_for_objective(kObjectiveCount) == nullptr,
                 "objective text indexes at the upper bound are rejected");
    tests.expect(get_difficulty_for_objective(-1) == 0,
                 "negative objective difficulty indexes are rejected");
    tests.expect(get_difficulty_for_objective(0) == 3,
                 "registered objective difficulty is returned");
    tests.expect(get_difficulty_for_objective(1) == 0,
                 "empty objective difficulty slots use zero");
    tests.expect(get_difficulty_for_objective(kObjectiveCount) == 0,
                 "objective difficulty indexes at the upper bound are rejected");
}

void test_objective_status_boundaries(TestState &tests)
{
    clear_objective_state();
    objectiveStatuses[0] = 2;
    tests.expect(get_status_of_objective(0) == 2,
                 "empty objective slots preserve their saved status");
    tests.expect(get_status_of_objective(-1) == 1,
                 "negative objective status indexes return complete safely");
    tests.expect(get_status_of_objective(kObjectiveCount) == 1,
                 "objective status indexes at the upper bound return complete");

    g_objective_runtime.debug_all_complete = true;
    tests.expect(get_status_of_objective(0) == 1,
                 "debug all-objectives-complete overrides an empty slot");
    g_objective_runtime.debug_all_complete = false;
}

void test_objective_criterion_matrix(TestState &tests)
{
    tests.expect(status_for_criterion(kObjectiveStart) == 1,
                 "objective start criteria are neutral");
    tests.expect(status_for_criterion(kObjectiveEnd) == 1,
                 "objective end criteria are neutral");
    tests.expect(status_for_criterion(kObjectiveNull) == 1,
                 "objective null criteria are neutral");
    tests.expect(status_for_criterion(99) == 1,
                 "unknown objective criteria are neutral");

    tests.expect(status_for_criterion(kDestroyObject, 1, 1, false) == 1,
                 "destroy criteria complete when their object is absent");
    tests.expect(status_for_criterion(kDestroyObject, 1, 1, true, true) == 0,
                 "destroy criteria remain incomplete while a healthy object exists");
    tests.expect(status_for_criterion(kDestroyObject, 1, 1, true, true, false) == 1,
                 "destroy criteria complete after an object is unhealthy");

    clear_objective_state();
    FakeObjectiveBuffer condition;
    set_criterion(condition, 0, kCompleteCondition, 3);
    set_criterion(condition, 1, kObjectiveEnd);
    install_objective(condition);
    tests.expect(get_status_of_objective(0) == 0,
                 "complete-condition criteria are incomplete without their flag");
    g_objective_runtime.stage_flag = true;
    tests.expect(get_status_of_objective(0) == 1,
                 "complete-condition criteria complete with their flag");

    clear_objective_state();
    FakeObjectiveBuffer fail_condition;
    set_criterion(fail_condition, 0, kFailCondition, 4);
    set_criterion(fail_condition, 1, kObjectiveEnd);
    install_objective(fail_condition);
    tests.expect(get_status_of_objective(0) == 1,
                 "fail-condition criteria stay complete without their flag");
    g_objective_runtime.stage_flag = true;
    tests.expect(get_status_of_objective(0) == 2,
                 "fail-condition criteria fail with their flag");

    tests.expect(status_for_criterion(kCollectObject, 1, 1, false) == 2,
                 "collect criteria fail when their object is absent");
    tests.expect(status_for_criterion(kCollectObject, 1, 1, true, false) == 2,
                 "collect criteria fail when their object has no prop");
    clear_objective_state();
    FakeObjectiveBuffer collect;
    set_criterion(collect, 0, kCollectObject);
    set_criterion(collect, 1, kObjectiveEnd);
    install_objective(collect);
    RawObject collect_object;
    RawProp collect_prop;
    FakeTag collect_tag;
    set_raw_prop(collect_prop, 0, 1.0f);
    set_raw_object_prop(collect_object, &collect_prop);
    install_tag(collect_tag, &collect_object);
    g_objective_runtime.healthy = false;
    tests.expect(get_status_of_objective(0) == 2,
                 "collect criteria fail when their object is unhealthy");
    g_objective_runtime.healthy = true;
    tests.expect(get_status_of_objective(0) == 0,
                 "collect criteria are incomplete before inventory pickup");
    g_objective_runtime.inventory = true;
    tests.expect(get_status_of_objective(0) == 1,
                 "collect criteria complete after inventory pickup");

    tests.expect(status_for_criterion(kDepositObject, 1, 1, false) == 1,
                 "deposit criteria complete when their object is absent");
    clear_objective_state();
    FakeObjectiveBuffer deposit;
    set_criterion(deposit, 0, kDepositObject);
    set_criterion(deposit, 1, kObjectiveEnd);
    install_objective(deposit);
    RawObject deposit_object;
    RawProp deposit_prop;
    FakeTag deposit_tag;
    set_raw_prop(deposit_prop, 0, 1.0f);
    set_raw_object_prop(deposit_object, &deposit_prop);
    install_tag(deposit_tag, &deposit_object);
    g_objective_runtime.inventory = true;
    tests.expect(get_status_of_objective(0) == 0,
                 "deposit criteria are incomplete while the object is in inventory");
    g_objective_runtime.inventory = false;
    tests.expect(get_status_of_objective(0) == 1,
                 "deposit criteria complete after the object leaves inventory");

    tests.expect(status_for_criterion(kPhotograph, 0, 1, false) == 2,
                 "photograph criteria fail when the target is absent");
    tests.expect(status_for_criterion(kPhotograph, 0, 1, true, true) == 0,
                 "photograph criteria are incomplete for a live target");
    tests.expect(status_for_criterion(kPhotograph, 1, 1, false) == 1,
                 "photograph criteria with text metadata are neutral");

    tests.expect(status_for_criterion(kEnterRoom, 0) == 0,
                 "enter-room criteria are incomplete before entry");
    tests.expect(status_for_criterion(kEnterRoom, 1) == 1,
                 "enter-room criteria with completion metadata are neutral");
    tests.expect(status_for_criterion(kDepositObjectInRoom, 1, 0) == 0,
                 "deposit-in-room criteria are incomplete at zero difficulty");
    tests.expect(status_for_criterion(kDepositObjectInRoom, 1) == 1,
                 "deposit-in-room criteria complete above zero difficulty");

    clear_objective_state();
    FakeObjectiveBuffer copy;
    set_criterion(copy, 0, kCopyItem);
    set_criterion(copy, 1, kObjectiveEnd);
    install_objective(copy);
    tests.expect(get_status_of_objective(0) == 0,
                 "copy-item criteria are incomplete before key analysis");
    g_objective_runtime.keyanalyzer = true;
    tests.expect(get_status_of_objective(0) == 1,
                 "copy-item criteria complete after key analysis");

    clear_objective_state();
    FakeObjectiveBuffer aggregate;
    set_criterion(aggregate, 0, kCopyItem);
    set_criterion(aggregate, 1, kFailCondition, 4);
    set_criterion(aggregate, 2, kObjectiveEnd);
    install_objective(aggregate);
    g_objective_runtime.stage_flag = true;
    tests.expect(get_status_of_objective(0) == 2,
                 "a failed criterion overrides an earlier incomplete criterion");
    g_objective_runtime.stage_flag = false;
    tests.expect(get_status_of_objective(0) == 0,
                 "a neutral criterion preserves an earlier incomplete status");
}

void test_objective_completion_and_status_display(TestState &tests)
{
    clear_objective_state();
    tests.expect(objectiveIsAllComplete() != 0,
                 "no registered objectives are already all complete");

    FakeObjectiveBuffer objective;
    set_criterion(objective, 0, kCopyItem);
    set_criterion(objective, 1, kObjectiveEnd);
    set_objective_difficulty(objective, 3);
    install_objective(objective);
    g_objective_runtime.selected_difficulty = 2;
    tests.expect(objectiveIsAllComplete() != 0,
                 "objectives above the selected difficulty are ignored");
    set_objective_difficulty(objective, 2);
    tests.expect(objectiveIsAllComplete() == 0,
                 "an available incomplete objective blocks completion");
    g_objective_runtime.keyanalyzer = true;
    tests.expect(objectiveIsAllComplete() != 0,
                 "all available complete objectives allow completion");

    clear_objective_state();
    FakeObjectiveBuffer logged;
    set_criterion(logged, 0, kCopyItem);
    set_criterion(logged, 1, kObjectiveEnd);
    set_objective_difficulty(logged, 0);
    install_objective(logged);
    g_savelogObjOnce = 1;
    ::setenv("GE_SAVELOG", "1", 1);
    (void)get_status_of_objective(0);
    (void)objectiveIsAllComplete();
    ::unsetenv("GE_SAVELOG");
    g_savelogObjOnce = 0;
    tests.expect(true, "objective save-log diagnostics are safely injectable");

    clear_objective_state();
    FakeObjectiveBuffer completed;
    set_criterion(completed, 0, kObjectiveEnd);
    set_objective_difficulty(completed, 0);
    install_objective(completed);
    objectiveStatuses[0] = 0;
    j_text_trigger = 1;
    display_objective_status_text_on_status_change();
    tests.expect(objectiveStatuses[0] == 1,
                 "status display stores the newly computed complete status");
    tests.expect(g_objective_runtime.hud_count == 1 &&
                     g_objective_runtime.last_hud.find("completed") != std::string::npos &&
                     g_objective_runtime.last_hud.find('\x80') != std::string::npos,
                 "status display formats completed Japanese-trigger text");
    display_objective_status_text_on_status_change();
    tests.expect(g_objective_runtime.hud_count == 1,
                 "status display does not repeat unchanged messages");

    clear_objective_state();
    FakeObjectiveBuffer incomplete;
    set_criterion(incomplete, 0, kCopyItem);
    set_criterion(incomplete, 1, kObjectiveEnd);
    set_objective_difficulty(incomplete, 0);
    install_objective(incomplete);
    objectiveStatuses[0] = 1;
    j_text_trigger = 0;
    display_objective_status_text_on_status_change();
    tests.expect(g_objective_runtime.last_hud.find("objective a: incomplete") != std::string::npos,
                 "status display formats incomplete legacy text");

    clear_objective_state();
    FakeObjectiveBuffer failed;
    set_criterion(failed, 0, kFailCondition);
    set_criterion(failed, 1, kObjectiveEnd);
    set_objective_difficulty(failed, 0);
    install_objective(failed);
    objectiveStatuses[0] = 0;
    g_objective_runtime.stage_flag = true;
    display_objective_status_text_on_status_change();
    tests.expect(g_objective_runtime.last_hud.find("failed") != std::string::npos,
                 "status display formats failed text");

    clear_objective_state();
    FakeObjectiveBuffer hidden;
    set_criterion(hidden, 0, kObjectiveEnd);
    set_objective_difficulty(hidden, 3);
    install_objective(hidden);
    objectiveStatuses[0] = 0;
    g_objective_runtime.selected_difficulty = 2;
    display_objective_status_text_on_status_change();
    tests.expect(g_objective_runtime.hud_count == 0,
                 "status display suppresses objectives above the selected difficulty");
}

void test_room_and_deposit_criteria(TestState &tests)
{
    clear_objective_state();
    FakePad pads[2]{};
    FakePad boundpads[2]{};
    FakeStand room_four{};
    room_four.room = 4;
    FakeStand room_eight{};
    room_eight.room = 8;
    pads[1].stan = &room_four;
    boundpads[0].stan = &room_eight;
    g_CurrentSetup.pads = pads;
    g_CurrentSetup.boundpads = boundpads;

    FakeRoomCriteria already_done;
    already_done.pad = 1;
    already_done.status = 1;
    FakeRoomCriteria unbound;
    unbound.pad = 1;
    unbound.status = 0;
    unbound.next = &already_done;
    FakeRoomCriteria bound;
    bound.pad = 10000;
    bound.status = 0;
    bound.next = &unbound;
    ptr_last_enter_room_subobject_entry_type20 = &bound;
    objectivestatusCheckRoomEntered(99);
    tests.expect(bound.status == 0 && unbound.status == 0 && already_done.status == 1,
                 "room criteria ignore completed entries and mismatched rooms");
    objectivestatusCheckRoomEntered(8);
    tests.expect(bound.status == 1,
                 "room criteria resolve bound-pad rooms");
    objectivestatusCheckRoomEntered(4);
    tests.expect(unbound.status == 1,
                 "room criteria resolve ordinary pad rooms");

    clear_objective_state();
    g_CurrentSetup.pads = pads;
    g_CurrentSetup.boundpads = boundpads;
    FakeDepositCriteria deposit_done;
    deposit_done.weapon = 7;
    deposit_done.pad = 1;
    deposit_done.flag = 1;
    FakeDepositCriteria wrong_weapon;
    wrong_weapon.weapon = 8;
    wrong_weapon.pad = 1;
    wrong_weapon.flag = 0;
    wrong_weapon.next = &deposit_done;
    FakeDepositCriteria unbound_deposit;
    unbound_deposit.weapon = 7;
    unbound_deposit.pad = 1;
    unbound_deposit.flag = 0;
    unbound_deposit.next = &wrong_weapon;
    FakeDepositCriteria bound_deposit;
    bound_deposit.weapon = 7;
    bound_deposit.pad = 10000;
    bound_deposit.flag = 0;
    bound_deposit.next = &unbound_deposit;
    ptr_last_deposit_in_room_subobject_entry_type21 = &bound_deposit;
    objectivestatusCheckDeposit(7, 99);
    tests.expect(bound_deposit.flag == 0 && unbound_deposit.flag == 0 &&
                     wrong_weapon.flag == 0 && deposit_done.flag == 1,
                 "deposit criteria filter weapon, completion, and room mismatches");
    objectivestatusCheckDeposit(7, 8);
    tests.expect(bound_deposit.flag == 1,
                 "deposit criteria resolve bound-pad rooms");
    objectivestatusCheckDeposit(7, 4);
    tests.expect(unbound_deposit.flag == 1,
                 "deposit criteria resolve ordinary pad rooms");
}

void test_picture_criteria_edges(TestState &tests)
{
    auto run = [&](float left, float up, float right, float down) {
        clear_objective_state();
        PictureFixture fixture;
        fixture.criteria.tag = 7;
        fixture.criteria.flag = 0;
        fixture.criteria.next = nullptr;
        set_raw_prop(fixture.prop, 0x02, 1.0f);
        set_raw_object_prop(fixture.object, &fixture.prop);
        install_tag(fixture.tag, &fixture.object);
        ptr_last_photo_obj_in_room_subobject_entry_type1E = &fixture.criteria;
        g_objective_runtime.projected = {left, up, right, down};
        objectiveTakePictureHandler();
        return fixture.criteria.flag;
    };

    clear_objective_state();
    objectiveTakePictureHandler();

    PictureFixture skipped;
    skipped.criteria.tag = 7;
    skipped.criteria.flag = 1;
    clear_objective_state();
    ptr_last_photo_obj_in_room_subobject_entry_type1E = &skipped.criteria;
    objectiveTakePictureHandler();
    tests.expect(skipped.criteria.flag == 1,
                 "already-completed photograph criteria are not revisited");

    tests.expect(run(10, 10, 90, 90) == 1,
                 "a target fully inside the player viewport is photographable");
    tests.expect(run(10, 10, 0, 90) == 0,
                 "photograph visibility rejects a target at the left boundary");
    tests.expect(run(10, 10, 100, 90) == 0,
                 "photograph visibility rejects a target at the right boundary");
    tests.expect(run(100, 10, 90, 90) == 0,
                 "photograph visibility rejects a target whose left edge is outside");
    tests.expect(run(10, 10, 90, 100) == 0,
                 "photograph visibility rejects a target at the bottom boundary");
    tests.expect(run(10, 100, 90, 90) == 0,
                 "photograph visibility rejects a target at the top boundary");

    clear_objective_state();
    PictureFixture no_target;
    no_target.criteria.tag = 7;
    ptr_last_photo_obj_in_room_subobject_entry_type1E = &no_target.criteria;
    objectiveTakePictureHandler();
    tests.expect(no_target.criteria.flag == 0,
                 "photograph criteria stay open when the tag is absent");

    auto run_with_object_state = [&](bool has_prop, std::uint8_t flags, float z_depth,
                                     bool healthy, bool bounds) {
        clear_objective_state();
        PictureFixture fixture;
        fixture.criteria.tag = 7;
        set_raw_prop(fixture.prop, flags, z_depth);
        set_raw_object_prop(fixture.object, has_prop ? &fixture.prop : nullptr);
        install_tag(fixture.tag, &fixture.object);
        ptr_last_photo_obj_in_room_subobject_entry_type1E = &fixture.criteria;
        g_objective_runtime.healthy = healthy;
        g_objective_runtime.bounds = bounds;
        objectiveTakePictureHandler();
        return fixture.criteria.flag;
    };

    tests.expect(run_with_object_state(false, 0x02, 1.0f, true, true) == 0,
                 "photograph criteria reject objects without props");
    tests.expect(run_with_object_state(true, 0x00, 1.0f, true, true) == 0,
                 "photograph criteria reject offscreen props");
    tests.expect(run_with_object_state(true, 0x02, -1.0f, true, true) == 0,
                 "photograph criteria reject props behind the render plane");
    tests.expect(run_with_object_state(true, 0x02, 1.0f, false, true) == 0,
                 "photograph criteria reject unhealthy objects");
    tests.expect(run_with_object_state(true, 0x02, 1.0f, true, false) == 0,
                 "photograph criteria reject objects without render bounds");
}

} // namespace

int main()
{
    TestState tests;
    test_stage_reset(tests);
    test_parent_lists_and_tag_lookup(tests);
    test_objective_and_child_registration(tests);
    test_objective_text_and_index_boundaries(tests);
    test_objective_status_boundaries(tests);
    test_objective_criterion_matrix(tests);
    test_objective_completion_and_status_display(tests);
    test_room_and_deposit_criteria(tests);
    test_picture_criteria_edges(tests);

    if (tests.failures != 0)
    {
        std::fprintf(stderr, "%d objective contract(s) failed\n",
                     tests.failures);
        return 1;
    }
    std::puts("Objective unit suite passed");
    return 0;
}
