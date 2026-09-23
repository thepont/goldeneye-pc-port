#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "bondtypes.h"
#include "chrai.h"
#include "initpathtablelinks.h"

#ifdef __unix__
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

stagesetup g_CurrentSetup;

static int failures;
static int connection_queries;

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

void sub_GAME_7F08F438(waygroup *group_a, waygroup *group_b,
                       waypoint **point_a, waypoint **point_b)
{
    connection_queries++;
    expect_true(group_a != NULL && group_b != NULL,
                "group connectivity probe receives both groups");
    *point_a = NULL;
    *point_b = NULL;

    if (group_a != group_b && group_a->waypoints != NULL &&
        group_b->waypoints != NULL)
    {
        *point_a = (waypoint *)(uintptr_t)1;
        *point_b = (waypoint *)(uintptr_t)1;
    }
}

struct path_fixture
{
    waypoint waypoints[3];
    s32 waypoint_neighbours[2][2];
    waygroup groups[3];
    s32 group_neighbours[2][2];
    s32 group_waypoints[2][2];
};

static void reset_fixture(struct path_fixture *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    memset(&g_CurrentSetup, 0, sizeof(g_CurrentSetup));
    connection_queries = 0;
}

static void setup_two_connected_groups(struct path_fixture *fixture)
{
    reset_fixture(fixture);

    fixture->waypoints[0].padID = 0;
    fixture->waypoints[0].neighbours = fixture->waypoint_neighbours[0];
    fixture->waypoints[0].groupNum = 0;
    fixture->waypoints[1].padID = 1;
    fixture->waypoints[1].neighbours = fixture->waypoint_neighbours[1];
    fixture->waypoints[1].groupNum = 1;
    fixture->waypoints[2].padID = -1;

    fixture->waypoint_neighbours[0][0] = 1;
    fixture->waypoint_neighbours[0][1] = -1;
    fixture->waypoint_neighbours[1][0] = 0;
    fixture->waypoint_neighbours[1][1] = -1;

    fixture->groups[0].neighbours = fixture->group_neighbours[0];
    fixture->groups[0].waypoints = fixture->group_waypoints[0];
    fixture->groups[1].neighbours = fixture->group_neighbours[1];
    fixture->groups[1].waypoints = fixture->group_waypoints[1];
    fixture->groups[2].neighbours = NULL;

    fixture->group_neighbours[0][0] = 1;
    fixture->group_neighbours[0][1] = -1;
    fixture->group_neighbours[1][0] = 0;
    fixture->group_neighbours[1][1] = -1;
    fixture->group_waypoints[0][0] = 0;
    fixture->group_waypoints[0][1] = -1;
    fixture->group_waypoints[1][0] = 1;
    fixture->group_waypoints[1][1] = -1;

    g_CurrentSetup.pathwaypoints = fixture->waypoints;
    g_CurrentSetup.waypointgroups = fixture->groups;
}

static void test_two_waypoint_group_topology(void)
{
    struct path_fixture fixture;

    setup_two_connected_groups(&fixture);
    init_path_table_links();

    expect_true(connection_queries == 2,
                "each reciprocal group edge is checked for a waypoint bridge");
    expect_true(fixture.waypoints[0].groupNum == 0 &&
                    fixture.waypoints[1].groupNum == 1,
                "preassigned waypoint groups remain consistent");
    expect_true(fixture.waypoints[0].dist == 1 &&
                    fixture.waypoints[1].dist == 1,
                "connected group members receive reachable distance marks");
}

static void test_unassigned_waypoints_receive_group_numbers(void)
{
    struct path_fixture fixture;

    setup_two_connected_groups(&fixture);
    fixture.waypoints[0].groupNum = -1;
    fixture.waypoints[1].groupNum = -1;
    init_path_table_links();

    expect_true(fixture.waypoints[0].groupNum == 0 &&
                    fixture.waypoints[1].groupNum == 1,
                "group membership is assigned to unassigned waypoints");
}

static void test_single_group_without_group_neighbours(void)
{
    struct path_fixture fixture;

    reset_fixture(&fixture);
    fixture.waypoints[0].padID = 0;
    fixture.waypoints[0].neighbours = fixture.waypoint_neighbours[0];
    fixture.waypoints[0].groupNum = -1;
    fixture.waypoints[1].padID = -1;
    fixture.waypoint_neighbours[0][0] = -1;

    fixture.groups[0].neighbours = fixture.group_neighbours[0];
    fixture.groups[0].waypoints = fixture.group_waypoints[0];
    fixture.groups[1].neighbours = NULL;
    fixture.group_neighbours[0][0] = -1;
    fixture.group_waypoints[0][0] = 0;
    fixture.group_waypoints[0][1] = -1;

    g_CurrentSetup.pathwaypoints = fixture.waypoints;
    g_CurrentSetup.waypointgroups = fixture.groups;
    init_path_table_links();

    expect_true(fixture.waypoints[0].groupNum == 0,
                "a group with no group neighbours still assigns its member");
    expect_true(fixture.waypoints[0].dist == 1,
                "a single waypoint group is connected to itself");
    expect_true(connection_queries == 0,
                "a group with no neighbours needs no bridge query");
}

static void test_waypoint_only_setup(void)
{
    struct path_fixture fixture;

    reset_fixture(&fixture);
    fixture.waypoints[0].padID = 0;
    fixture.waypoints[0].neighbours = fixture.waypoint_neighbours[0];
    fixture.waypoints[1].padID = -1;
    fixture.waypoint_neighbours[0][0] = -1;
    g_CurrentSetup.pathwaypoints = fixture.waypoints;
    g_CurrentSetup.waypointgroups = NULL;

    init_path_table_links();
    expect_true(connection_queries == 0,
                "waypoint-only setup skips group connectivity work");
}

#ifdef __unix__
static void terminate_invalid_topology_child(int signal_number)
{
    (void)signal_number;
    _exit(124);
}

static void test_self_link_enters_bounded_error_path(void)
{
    struct path_fixture fixture;
    pid_t child;
    int status;

    reset_fixture(&fixture);
    fixture.waypoints[0].padID = 0;
    fixture.waypoints[0].neighbours = fixture.waypoint_neighbours[0];
    fixture.waypoints[1].padID = -1;
    fixture.waypoint_neighbours[0][0] = 0;
    fixture.waypoint_neighbours[0][1] = -1;
    g_CurrentSetup.pathwaypoints = fixture.waypoints;
    g_CurrentSetup.waypointgroups = NULL;

    child = fork();
    expect_true(child >= 0, "invalid-topology probe can create a child process");
    if (child < 0)
    {
        return;
    }

    if (child == 0)
    {
        signal(SIGALRM, terminate_invalid_topology_child);
        alarm(1);
        init_path_table_links();
        _exit(0);
    }

    expect_true(waitpid(child, &status, 0) == child,
                "invalid-topology child terminates deterministically");
    expect_true(WIFEXITED(status) && WEXITSTATUS(status) == 124,
                "a self-link reaches the documented error loop rather than returning as valid");
}
#endif

int main(void)
{
    test_two_waypoint_group_topology();
    test_unassigned_waypoints_receive_group_numbers();
    test_single_group_without_group_neighbours();
    test_waypoint_only_setup();
#ifdef __unix__
    test_self_link_enters_bounded_error_path();
#endif

    if (failures != 0)
    {
        fprintf(stderr, "%d init-path-table test(s) failed\n", failures);
        return 1;
    }

    puts("init-path-table-links tests passed");
    return 0;
}
