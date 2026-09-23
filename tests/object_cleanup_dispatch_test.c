#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "bondtypes.h"

void cleanupObjects(s32 stage);

stagesetup g_CurrentSetup = {};

static ObjectRecord *freed_objects[8];
static bool freed_flags[8];
static int freed_count;
static int failures;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

s32 sizepropdef(PropDefHeaderRecord *pdef)
{
    (void)pdef;
    return 1;
}

void objFreePermanently(ObjectRecord *object, bool freeprop)
{
    freed_objects[freed_count] = object;
    freed_flags[freed_count] = freeprop;
    freed_count++;
}

int main(void)
{
    PropDefHeaderRecord defs[5] = {};

    defs[0].type = PROPDEF_PROP;
    defs[1].type = PROPDEF_LINK;
    defs[2].type = PROPDEF_TANK;
    defs[3].type = PROPDEF_NOTHING;
    defs[4].type = PROPDEF_END;
    g_CurrentSetup.propDefs = defs;
    freed_count = 0;

    cleanupObjects(0);

    expect(freed_count == 2,
           "object cleanup dispatches only object-backed prop definitions");
    expect(freed_objects[0] == (ObjectRecord *)&defs[0] &&
               freed_objects[1] == (ObjectRecord *)&defs[2] &&
               freed_flags[0] == TRUE && freed_flags[1] == TRUE,
           "object cleanup passes each object record with the free flag");

    g_CurrentSetup.propDefs = NULL;
    freed_count = 0;
    cleanupObjects(0);
    expect(freed_count == 0,
           "object cleanup safely handles a missing prop-definition stream");

    if (failures != 0)
    {
        fprintf(stderr, "%d object-cleanup dispatch contract(s) failed\n", failures);
        return 1;
    }

    puts("object-cleanup dispatch suite passed");
    return 0;
}
