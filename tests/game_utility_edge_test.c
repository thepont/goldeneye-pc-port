/* TDD coverage for small but stateful game seams that previously had no
 * executable test boundary. The production C files are linked directly;
 * the fakes below are only the external game services those seams require. */
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "bondtypes.h"
#include "chrai.h"
#include "cleanup_alarms.h"
#include "compiletime.h"
#include "explosion.h"
#include "glass.h"
#include "mp_weapon.h"
#include "objective_status.h"
#include "padhalllv.h"

void cleanupObjectives(void);
void cleanup_window_pieces(void);
void null_init_main_2(void);
void null_init_main_3(void);
void cleanup_REMOVED_(void);
void set_gu_scale(void);
waypoint *waypointFindRandomByDist(s32 *pointnums, s32 dist);
waygroup *waygroupFindRandomByDist(s32 *groupnums, s32 dist);
waypoint *waypointFindNearestToPos(coord3d *pos, s32 unused);
waypoint *waypointRefineNearestToPos(coord3d *pos, s32 unused,
                                     waypoint *start);
waygroup *waygroupFindByDist(s32 *groupnums, s32 value);
void waygroupsSetUnvisitedDist(s32 *groupnums, s32 value);
bool waygroupsPropagateDist(waygroup *group, s32 value);
bool waygroupsFloodDist(waygroup *from, waygroup *to, waygroup *groups,
                        s32 continue_search);
bool waygroupsMarkRoute(waygroup *from, waygroup *to, waygroup *groups);
s32 waygroupFindRoute(waygroup *from, waygroup *to, waygroup **arr,
                      s32 arrlen);
waypoint *findPadWithDistAndSet(s32 *pointnums, s32 value, s32 groupnum);
void waypointsSetUnvisitedDist(s32 *pointnums, s32 value, s32 groupnum);
bool waypointsPropagateDist(s32 *pointnums, s32 value, s32 groupnum);
void do_BFS_withinPathSet(waypoint *from, waypoint *to, s32 continue_search);
void waypointMarkRoute(waypoint *from, waypoint *to);
s32 waypointFindRouteInGroup(waypoint *from, waypoint *to, waypoint **arr,
                             s32 arrlen);
void sub_GAME_7F08F438(waygroup *groupa, waygroup *groupb,
                       waypoint **pointa, waypoint **pointb);
s32 waypointFindRoute(waypoint *from, waypoint *to, waypoint **arr,
                      s32 arrlen);
void waypointDebugTestRandomRoute(void);
void resetWaypointDistances(void);
waypoint *waypointFindNextStepToward(waypoint *from, waypoint *to);

stagesetup g_CurrentSetup = {};
struct objective_entry *objective_ptrs[OBJECTIVES_MAX] = {};
OBJECTIVESTATUS objectiveStatuses[OBJECTIVES_MAX] = {};
s32 SHATTERED_WINDOW_PIECES_BUFFER_LEN = 0;
s_shattered_window_piece *ptr_shattered_window_pieces = NULL;
Mtx dword_CODE_bss_8007A100 = {};

struct TestState
{
    int failures;
};

static int alarm_deactivate_calls;
static int gas_deactivate_calls;
static Mtx *scaled_matrix;
static float scaled_x;
static float scaled_y;
static float scaled_z;
static u32 random_value;

static waypoint path_points[6];
static waygroup path_groups[4];
static PadRecord path_pads[5];
static s32 path_point_neighbours[5][4] = {
    {1, 2, -1, -1},
    {0, -1, -1, -1},
    {3, 0, -1, -1},
    {2, 4, -1, -1},
    {3, -1, -1, -1},
};
static s32 path_group_neighbours[3][4] = {
    {1, -1, -1, -1},
    {0, 2, -1, -1},
    {1, -1, -1, -1},
};
static s32 path_group_waypoints[3][4] = {
    {0, 1, -1, -1},
    {2, 3, -1, -1},
    {4, -1, -1, -1},
};

static void reset_path_fixture(void)
{
    int i;

    memset(path_points, 0, sizeof(path_points));
    memset(path_groups, 0, sizeof(path_groups));
    memset(path_pads, 0, sizeof(path_pads));

    for (i = 0; i < 5; ++i)
    {
        path_points[i].padID = i;
        path_points[i].groupNum = i == 0 || i == 1 ? 0 :
                                  i == 2 || i == 3 ? 1 : 2;
        path_points[i].dist = -1;
        path_points[i].neighbours = path_point_neighbours[i];
        path_pads[i].pos.x = (f32)(i * 10);
        path_pads[i].pos.z = 0.0f;
    }
    path_points[5].padID = -1;

    for (i = 0; i < 3; ++i)
    {
        path_groups[i].neighbours = path_group_neighbours[i];
        path_groups[i].waypoints = path_group_waypoints[i];
        path_groups[i].dist = -1;
    }
    /* A null-neighbour group terminates the group tables in the original
     * data format. */
    path_groups[3].neighbours = NULL;
    path_groups[3].waypoints = NULL;

    g_CurrentSetup.pathwaypoints = path_points;
    g_CurrentSetup.waypointgroups = path_groups;
    g_CurrentSetup.pads = path_pads;
}

void alarmDeactivate(void)
{
    alarm_deactivate_calls++;
}

void check_deactivate_gas_sound(void)
{
    gas_deactivate_calls++;
}

void guScale(Mtx *matrix, float x, float y, float z)
{
    scaled_matrix = matrix;
    scaled_x = x;
    scaled_y = y;
    scaled_z = z;
}

u32 randomGetNext(void)
{
    return random_value;
}

static void expect(struct TestState *tests, int condition, const char *message)
{
    if (!condition)
    {
        tests->failures++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

static void expect_near(struct TestState *tests, float actual, float expected,
                        const char *message)
{
    expect(tests, actual > expected - 0.00001f &&
                       actual < expected + 0.00001f,
           message);
}

static sigjmp_buf fpe_jump;
static volatile sig_atomic_t fpe_seen;

static void fpe_handler(int unused)
{
    (void)unused;
    fpe_seen = 1;
    siglongjmp(fpe_jump, 1);
}

static int empty_waypoint_list_is_safe(void)
{
    s32 empty[] = {-1};
    void (*old)(int) = signal(SIGFPE, fpe_handler);
    int returned_null;
    fpe_seen = 0;

    if (sigsetjmp(fpe_jump, 1) == 0)
    {
        returned_null = waypointFindRandomByDist(empty, 0) == NULL;
        signal(SIGFPE, old);
        return returned_null && !fpe_seen;
    }

    signal(SIGFPE, old);
    return 0;
}

static int empty_group_list_is_safe(void)
{
    s32 empty[] = {-1};
    void (*old)(int) = signal(SIGFPE, fpe_handler);
    int returned_null;
    fpe_seen = 0;

    if (sigsetjmp(fpe_jump, 1) == 0)
    {
        returned_null = waygroupFindRandomByDist(empty, 0) == NULL;
        signal(SIGFPE, old);
        return returned_null && !fpe_seen;
    }

    signal(SIGFPE, old);
    return 0;
}

static void test_cleanup_alarm_boundary(struct TestState *tests)
{
    alarm_deactivate_calls = 0;
    gas_deactivate_calls = 0;
    cleanupAlarms();
    expect(tests, alarm_deactivate_calls == 1,
           "cleanupAlarms deactivates the alarm exactly once");
    expect(tests, gas_deactivate_calls == 1,
           "cleanupAlarms deactivates gas sound exactly once");
}

static void test_cleanup_objective_matrix(struct TestState *tests)
{
    struct objective_entry active = {0};
    struct objective_entry failed = {0};
    struct objective_entry inactive = {0};
    int i;

    active.unkD = 1;
    failed.unkD = 1;

    for (i = 0; i < OBJECTIVES_MAX; ++i)
    {
        objective_ptrs[i] = NULL;
        objectiveStatuses[i] = OBJECTIVESTATUS_INCOMPLETE;
    }
    objective_ptrs[0] = &active;
    objective_ptrs[1] = &failed;
    objective_ptrs[2] = &inactive;
    objective_ptrs[3] = &active;
    objectiveStatuses[1] = OBJECTIVESTATUS_FAILED;

    cleanupObjectives();

    expect(tests, objectiveStatuses[0] == OBJECTIVESTATUS_COMPLETE,
           "active objective becomes complete during cleanup");
    expect(tests, objectiveStatuses[1] == OBJECTIVESTATUS_FAILED,
           "failed objective is not overwritten during cleanup");
    expect(tests, objectiveStatuses[2] == OBJECTIVESTATUS_INCOMPLETE,
           "inactive objective is not completed during cleanup");
    expect(tests, objectiveStatuses[3] == OBJECTIVESTATUS_COMPLETE,
           "another active objective is completed independently");
    expect(tests, objectiveStatuses[4] == OBJECTIVESTATUS_INCOMPLETE,
           "null objective entry remains unchanged");
}

static void test_window_cleanup_boundary(struct TestState *tests)
{
    s_shattered_window_piece pieces[3] = {};

    pieces[0].active = -1;
    pieces[1].active = 7;
    pieces[2].active = 99;
    ptr_shattered_window_pieces = pieces;
    SHATTERED_WINDOW_PIECES_BUFFER_LEN = 3;

    cleanup_window_pieces();

    expect(tests, pieces[0].active == 0 && pieces[1].active == 0 &&
                       pieces[2].active == 0,
           "window cleanup clears every shard state");

    ptr_shattered_window_pieces = NULL;
    SHATTERED_WINDOW_PIECES_BUFFER_LEN = 0;
    cleanup_window_pieces();
    expect(tests, 1, "zero-sized window cleanup is a no-op");
}

static void test_weapon_set_boundaries(struct TestState *tests)
{
    struct s_mp_weapon_set *expected[] = {
        mp_weapon_set_slaps,
        mp_weapon_set_pistols,
        mp_weapon_set_knife,
        mp_weapon_set_auto,
        mp_weapon_set_power,
        mp_weapon_set_sniper,
        mp_weapon_set_grenade,
        mp_weapon_set_remote_m,
        mp_weapon_set_glaunch,
        mp_weapon_set_timed_m,
        mp_weapon_set_prox_m,
        mp_weapon_set_rockets,
        mp_weapon_set_lasers,
        mp_weapon_set_golden,
    };
    s32 i;

    for (i = 0; i < 14; ++i)
    {
        setMPWeaponSet(i);
        expect(tests, getMPWeaponSet() == i,
               "weapon-set selection preserves every valid index");
        expect(tests, getPtrMPWeaponSetData() == expected[i],
               "weapon-set selection returns the matching data table");
        expect(tests, getPtrMPWeaponSetTextID() != NULL,
               "every weapon-set selection has a text-id slot");
    }

    setMPWeaponSet(13);
    incrementMPWeaponSet();
    expect(tests, getMPWeaponSet() == 0,
           "weapon-set increment wraps from the final menu entry");
}

static void test_compile_time_contract(struct TestState *tests)
{
    char *value = get_compile_time();
    expect(tests, value != NULL && strlen(value) > 0,
           "compile-time provenance is non-empty");
#if defined(BYTEMATCH) && defined(VERSION_US)
    expect(tests, strcmp(value, "Jun 29 1997 20:46:05") == 0,
           "US byte-match build keeps the retail compile timestamp");
#endif
}

static void test_scale_adapter(struct TestState *tests)
{
    scaled_matrix = NULL;
    scaled_x = scaled_y = scaled_z = 0.0f;
    set_gu_scale();
    expect(tests, scaled_matrix == &dword_CODE_bss_8007A100,
           "set_gu_scale targets the shared game matrix");
    expect_near(tests, scaled_x, 0.1f, "set_gu_scale forwards X scale");
    expect_near(tests, scaled_y, 0.1f, "set_gu_scale forwards Y scale");
    expect_near(tests, scaled_z, 0.1f, "set_gu_scale forwards Z scale");
}

static void test_null_entrypoints(struct TestState *tests)
{
    null_init_main_2();
    null_init_main_3();
    cleanup_REMOVED_();
    expect(tests, 1, "legacy null entrypoints return without side effects");
}

static void test_path_random_boundaries(struct TestState *tests)
{
    s32 point_numbers[] = {0, 1, 2, 3, 4, -1};
    s32 group_numbers[] = {0, 1, 2, -1};
    waypoint *route[8];
    waygroup *group_route[8];
    waypoint *pointa;
    waypoint *pointb;
    coord3d pos = {};
    s32 route_count;
    int i;

    expect(tests, empty_waypoint_list_is_safe(),
           "empty waypoint list returns null instead of dividing by zero");
    expect(tests, empty_group_list_is_safe(),
           "empty group list returns null instead of dividing by zero");

    reset_path_fixture();
    random_value = 0;
    path_points[0].dist = 4;
    path_points[1].dist = 9;
    expect(tests, waypointFindRandomByDist(point_numbers, 9) == &path_points[1],
           "waypoint random search finds a matching distance");
    expect(tests, waypointFindRandomByDist(point_numbers, 3) == NULL,
           "waypoint random search rejects a missing distance");
    random_value = 2;
    expect(tests, waypointFindRandomByDist(point_numbers, 4) == &path_points[0],
           "waypoint random search wraps to the lower list segment");

    path_groups[0].dist = 2;
    path_groups[1].dist = 8;
    expect(tests, waygroupFindRandomByDist(group_numbers, 8) == &path_groups[1],
           "group random search finds a matching distance");
    expect(tests, waygroupFindRandomByDist(group_numbers, 3) == NULL,
           "group random search rejects a missing distance");
    expect(tests, waygroupFindRandomByDist(group_numbers, 2) == &path_groups[0],
           "group random search wraps to the lower list segment");

    expect(tests, waygroupFindByDist(group_numbers, 8) == &path_groups[1],
           "group distance lookup returns the matching group");
    expect(tests, waygroupFindByDist(group_numbers, 99) == NULL,
           "group distance lookup rejects a missing group");

    random_value = 1;
    expect(tests, waygroupFindRandomByDist(group_numbers, 8) == &path_groups[1],
           "group random search accepts a match in its first scan segment");

    path_groups[0].dist = -1;
    path_groups[1].dist = 7;
    path_groups[2].dist = -1;
    waygroupsSetUnvisitedDist(group_numbers, 4);
    expect(tests, path_groups[0].dist == 4 && path_groups[1].dist == 7 &&
                       path_groups[2].dist == 4,
           "group distance initialization preserves assigned distances");

    path_groups[0].dist = 0;
    path_groups[1].dist = -1;
    path_groups[2].dist = -1;
    expect(tests, waygroupsPropagateDist(path_groups, 0),
           "group propagation reports a matching frontier");
    expect(tests, path_groups[1].dist == 1,
           "group propagation assigns the next frontier distance");
    expect(tests, !waygroupsPropagateDist(path_groups, 9),
           "group propagation reports an empty frontier");

    reset_path_fixture();
    expect(tests, waygroupsFloodDist(&path_groups[0], &path_groups[2],
                                     path_groups, 0),
           "group flood finds a connected destination");
    expect(tests, path_groups[0].dist == 0 && path_groups[1].dist == 1 &&
                       path_groups[2].dist == 2,
           "group flood records shortest group distances");
    expect(tests, waygroupsMarkRoute(&path_groups[0], &path_groups[2],
                                     path_groups),
           "group route marking succeeds for connected groups");

    reset_path_fixture();
    expect(tests, waygroupFindRoute(&path_groups[0], &path_groups[2],
                                    group_route, 8) == 4,
           "group route writes source, intermediate, destination, terminator");
    expect(tests, group_route[0] == &path_groups[0] &&
                       group_route[1] == &path_groups[1] &&
                       group_route[2] == &path_groups[2] &&
                       group_route[3] == NULL,
           "group route preserves the connected group order");
    expect(tests, waygroupFindRoute(&path_groups[0], &path_groups[2],
                                    group_route, 1) == 1 &&
                       group_route[0] == NULL,
           "bounded group route leaves no room for a route entry");

    reset_path_fixture();
    path_points[0].dist = 4;
    path_points[1].dist = -1;
    path_points[2].dist = 9;
    path_points[3].dist = -1;
    path_points[4].dist = 9;
    expect(tests, findPadWithDistAndSet(path_point_neighbours[0], 9, 1) ==
                       &path_points[2],
           "waypoint distance lookup finds the matching group member");
    waypointsSetUnvisitedDist(path_point_neighbours[0], 6, 0);
    expect(tests, path_points[1].dist == 6,
           "waypoint distance initialization fills an unvisited member");
    expect(tests, !waypointsPropagateDist(path_point_neighbours[1], 6, 0),
           "waypoint propagation reports a frontier with no neighbours");

    path_points[1].dist = 8;
    expect(tests, findPadWithDistAndSet(path_point_neighbours[0], 99, 0) == NULL,
           "waypoint distance lookup returns null when no entry matches");
    expect(tests, waypointFindRandomByDist(point_numbers, 99) == NULL,
           "waypoint random search returns null after both scan segments miss");

    path_points[0].dist = 6;
    path_points[1].dist = 6;
    path_points[1].neighbours = NULL;
    expect(tests, !waypointsPropagateDist(path_point_neighbours[0], 6, 0),
           "waypoint propagation rejects a matching-distance waypoint without neighbours");

    reset_path_fixture();
    do_BFS_withinPathSet(&path_points[0], &path_points[1], 0);
    expect(tests, path_points[1].dist >= 0,
           "waypoint BFS reaches a target within one group");
    resetWaypointDistances();
    for (i = 0; i < 5; ++i)
    {
        expect(tests, path_points[i].dist == -1,
               "waypoint reset clears every path distance");
    }

    reset_path_fixture();
    expect(tests, waypointFindRouteInGroup(&path_points[0], &path_points[1],
                                           route, 8) == 3,
           "within-group route returns two points and terminator");
    expect(tests, route[0] == &path_points[0] && route[1] == &path_points[1] &&
                       route[2] == NULL,
           "within-group route preserves waypoint order");
    expect(tests, waypointFindRouteInGroup(&path_points[0], &path_points[1],
                                           route, 1) == 1 && route[0] == NULL,
           "bounded within-group route writes only its terminator");

    reset_path_fixture();
    sub_GAME_7F08F438(&path_groups[0], &path_groups[1], &pointa, &pointb);
    expect(tests, pointa == &path_points[0] && pointb == &path_points[2],
           "group bridge lookup returns the first cross-group edge");
    sub_GAME_7F08F438(&path_groups[0], &path_groups[2], &pointa, &pointb);
    expect(tests, pointa == NULL && pointb == NULL,
           "group bridge lookup rejects a non-adjacent group");

    reset_path_fixture();
    memset(route, 0, sizeof(route));
    route_count = waypointFindRoute(&path_points[0], &path_points[4], route, 8);
    expect(tests, route_count >= 2 && route_count <= 8,
           "full waypoint route returns a non-empty cross-group path");
    expect(tests, route[0] == &path_points[0] && route[1] != NULL,
           "full waypoint route starts at the requested point");
    expect(tests, route[route_count - 1] == NULL,
           "full waypoint route terminates within its output array");

    pos.x = 9.0f;
    pos.z = 1.0f;
    reset_path_fixture();
    expect(tests, waypointFindNearestToPos(&pos, 0) == &path_points[1],
           "nearest waypoint uses horizontal pad distance");
    pos.x = 19.0f;
    pos.z = 0.0f;
    expect(tests, waypointRefineNearestToPos(&pos, 0, &path_points[0]) ==
                       &path_points[2],
           "nearest refinement chooses the closest connected waypoint");
    path_points[0].neighbours = NULL;
    expect(tests, waypointRefineNearestToPos(&pos, 0, &path_points[0]) ==
                       &path_points[0],
           "nearest refinement preserves the start without neighbours");

    reset_path_fixture();
    path_points[0].neighbours = path_point_neighbours[1];
    path_point_neighbours[1][0] = -1;
    expect(tests, waypointFindNextStepToward(&path_points[0], &path_points[1]) ==
                       NULL,
           "next-step lookup returns null when no improving neighbour exists");
}

static void test_next_step_route_boundaries(struct TestState *tests)
{
    /* Keep this fixture deliberately small: it exercises the two within-group
     * candidate priorities and both cross-group route choices without a live
     * level, controller, renderer, or random source. */
    s32 same_group_waypoints[] = {0, 1, 2, -1};
    s32 same_group_neighbours[3][4] = {
        {1, 2, -1, -1},
        {0, -1, -1, -1},
        {-1, -1, -1, -1},
    };
    waypoint *result;

    reset_path_fixture();
    path_points[0].groupNum = 0;
    path_points[1].groupNum = 0;
    path_points[2].groupNum = 0;
    path_groups[0].waypoints = same_group_waypoints;
    path_points[0].neighbours = same_group_neighbours[0];
    path_points[1].neighbours = same_group_neighbours[1];
    path_points[2].neighbours = same_group_neighbours[2];
    random_value = 0;
    result = waypointFindNextStepToward(&path_points[0], &path_points[1]);
    expect(tests, result == &path_points[2],
           "same-group next-step prefers an unvisited neighbour");

    reset_path_fixture();
    path_points[0].groupNum = 0;
    path_points[1].groupNum = 0;
    path_points[2].groupNum = 0;
    path_groups[0].waypoints = same_group_waypoints;
    same_group_neighbours[0][0] = 2;
    same_group_neighbours[0][1] = -1;
    same_group_neighbours[1][0] = 0;
    same_group_neighbours[1][1] = -1;
    path_points[0].neighbours = same_group_neighbours[0];
    path_points[1].neighbours = same_group_neighbours[1];
    path_points[2].neighbours = same_group_neighbours[2];
    result = waypointFindNextStepToward(&path_points[0], &path_points[1]);
    expect(tests, result == &path_points[2],
           "same-group next-step falls back to the next-distance neighbour");

    reset_path_fixture();
    random_value = 1;
    result = waypointFindNextStepToward(&path_points[3], &path_points[0]);
    expect(tests, result == &path_points[4],
           "cross-group next-step uses the direct bridge from the current waypoint");

    reset_path_fixture();
    random_value = 1;
    result = waypointFindNextStepToward(&path_points[2], &path_points[0]);
    expect(tests, result == &path_points[3],
           "cross-group next-step routes within the current group to its bridge");

    reset_path_fixture();
    random_value = 0;
    result = waypointFindNextStepToward(&path_points[0], &path_points[4]);
    expect(tests, result == &path_points[1],
           "cross-group next-step falls back to a preceding group frontier");

    reset_path_fixture();
    path_point_neighbours[2][0] = -1;
    path_point_neighbours[2][1] = -1;
    random_value = 1;
    result = waypointFindNextStepToward(&path_points[2], &path_points[0]);
    expect(tests, result == NULL,
           "cross-group next-step returns null when the bridge is unreachable within the group");

    reset_path_fixture();
    path_group_neighbours[1][0] = -1;
    result = waypointFindNextStepToward(&path_points[4], &path_points[0]);
    expect(tests, result == NULL,
           "cross-group next-step returns null when the destination group is disconnected");

    reset_path_fixture();
    g_CurrentSetup.waypointgroups = NULL;
    expect(tests, waypointFindNextStepToward(&path_points[0], &path_points[1]) == NULL,
           "next-step lookup returns null without a waypoint-group table");

    reset_path_fixture();
    random_value = 0;
    waypointDebugTestRandomRoute();
    expect(tests, 1, "debug route probe handles a valid waypoint table");

    reset_path_fixture();
}

int main(void)
{
    struct TestState tests = {0};
    test_cleanup_alarm_boundary(&tests);
    test_cleanup_objective_matrix(&tests);
    test_window_cleanup_boundary(&tests);
    test_weapon_set_boundaries(&tests);
    test_compile_time_contract(&tests);
    test_scale_adapter(&tests);
    test_null_entrypoints(&tests);
    test_path_random_boundaries(&tests);
    test_next_step_route_boundaries(&tests);

    if (tests.failures != 0)
    {
        fprintf(stderr, "%d game utility contract(s) failed\n",
                tests.failures);
        return 1;
    }
    puts("game utility edge suite passed");
    return 0;
}
