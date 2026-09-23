#include <stdio.h>

#include <ultra64.h>

void init_weapon_animation_groups_maybe(void);

s32 get_ptr_allocated_block_for_vertices;

static int events[2];
static int event_count;
static void *allocator_argument;
static int failures;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

void set_vtxallocator(void *allocator)
{
    allocator_argument = allocator;
    events[event_count++] = 1;
}

void initWeaponAnimGroups(void)
{
    events[event_count++] = 2;
}

int main(void)
{
    event_count = 0;
    allocator_argument = NULL;
    init_weapon_animation_groups_maybe();

    expect(allocator_argument == &get_ptr_allocated_block_for_vertices,
           "weapon animation initialization registers the vertex allocator");
    expect(event_count == 2 && events[0] == 1 && events[1] == 2,
           "weapon animation initialization registers before resolving groups");

    if (failures != 0)
    {
        fprintf(stderr, "%d weapon-animation initialization contract(s) failed\n",
                failures);
        return 1;
    }

    puts("weapon-animation initialization suite passed");
    return 0;
}
