#include <stdio.h>
#include <string.h>

#include <ultra64.h>

#include "bondinv.h"
#include "bondconstants.h"
#include "gun.h"
#include "language.h"
#include "player.h"

struct player *g_CurrentPlayer;
s32 g_ClockTimer;
s32 j_text_trigger;

InvItem *bondinvGetNextAvailItem(void);
bool bondinvHasInvItem(ITEM_IDS item);
bool bondinvHasDualWeapon(ITEM_IDS right, ITEM_IDS left);
void bondinvRemoveItem(InvItem *item);
int bondinvAddPropToInv(PropRecord *prop);
int bondinvAddWeaponByProp(PropRecord *prop);
bool bondinvHasGEKey(void);
u16 *bondinvGetNameByIndex(s32 index);
u16 *bondinvGetLongNameByIndex(s32 index);
u16 *bondinvGetFirstTitlebyIndex(s32 index);
u16 *bondinvGetSecondTitlebyIndex(s32 index);
f32 bondinvGet45AngleForIndex(int index);
f32 bondinvGetDifferent45AngleForIndex(int index);
int bondinvGetHoffsetForIndex(int index);
int bondinvGetVoffsetForIndex(int index);
int bondinvGetDepthForIndex(int index);
int bondinvGetVposWatchForIndex(int index);
int bondinvGetHposWatchForIndex(int index);
int bondinvGetDepthWatchForIndex(int index);
int bondinvGetXrotWatchForIndex(int index);
int bondinvGetYrotWatchForIndex(int index);
u8 *bondinvGetActivatedTextObject(ObjectRecord *obj);
u8 *bondinvGetActivatedTextWeapon(ITEM_IDS weaponnum);
int bondinvGetCurEquippedItem(void);
void bondinvSetCurEquippedItem(int current_item);
void bondinvDetermineEquippedItem(void);

static struct player player_fixture;
static InvItem item_storage[8];
static u16 text_storage[64][4];
static s32 failures;
static s32 player_count;
static s32 ammo_available;
static s32 dual_wield_weapon;
static s32 held_time_weapon;
static ITEM_IDS current_right_weapon;

static void expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void expect_int(s32 actual, s32 expected, const char *message)
{
    if (actual != expected)
    {
        fprintf(stderr, "FAIL: %s (actual %d expected %d)\n",
                message, actual, expected);
        failures++;
    }
}

static void expect_float(f32 actual, f32 expected, const char *message)
{
    f32 difference = actual - expected;

    if (difference < -0.0001f || difference > 0.0001f)
    {
        fprintf(stderr, "FAIL: %s (actual %.3f expected %.3f)\n",
                message, actual, expected);
        failures++;
    }
}

static void reset_fixture(void)
{
    s32 i;

    memset(&player_fixture, 0, sizeof(player_fixture));
    memset(item_storage, 0, sizeof(item_storage));
    memset(text_storage, 0, sizeof(text_storage));

    g_CurrentPlayer = &player_fixture;
    player_fixture.p_itemcur = item_storage;
    player_fixture.equipmaxitems = 8;
    player_fixture.equipallguns = FALSE;
    player_fixture.bonddead = FALSE;
    player_fixture.equipcuritem = ITEM_UNARMED;
    for (i = 0; i < 8; i++)
    {
        item_storage[i].type = -1;
    }

    g_ClockTimer = 3;
    j_text_trigger = FALSE;
    player_count = 1;
    ammo_available = TRUE;
    dual_wield_weapon = ITEM_WPPK;
    held_time_weapon = ITEM_WPPK;
    current_right_weapon = ITEM_UNARMED;
}

static InvItem *item_at(s32 index)
{
    InvItem *item = g_CurrentPlayer->ptr_inventory_first_in_cycle;
    s32 i;

    for (i = 0; item != NULL && i < index; i++)
    {
        item = item->next;
        if (item == g_CurrentPlayer->ptr_inventory_first_in_cycle)
        {
            return NULL;
        }
    }
    return item;
}

static s32 count_items(void)
{
    InvItem *first = g_CurrentPlayer->ptr_inventory_first_in_cycle;
    InvItem *item = first;
    s32 count = 0;

    while (item != NULL)
    {
        count++;
        item = item->next;
        if (item == first)
        {
            break;
        }
    }
    return count;
}

static void test_reinitialization_and_sorted_insertions(void)
{
    reset_fixture();
    item_storage[0].type = INV_ITEM_PROP;
    item_storage[1].type = INV_ITEM_PROP;
    player_fixture.ptr_inventory_first_in_cycle = &item_storage[0];
    player_fixture.textoverrides = (textoverride *)(uintptr_t)1;
    player_fixture.equipcuritem = ITEM_GOLDENGUN;

    bondinvReinitInv();

    expect_true(item_storage[0].type == -1 && item_storage[7].type == -1,
                "inventory reinitialization clears every equipment slot");
    expect_true(player_fixture.ptr_inventory_first_in_cycle == NULL,
                "inventory reinitialization clears the circular-list head");
    expect_true(player_fixture.textoverrides == NULL &&
                    player_fixture.equipcuritem == ITEM_UNARMED,
                "inventory reinitialization clears overrides and equips unarmed");
    expect_true(bondinvGetNextAvailItem() == &item_storage[0],
                "inventory exposes the first cleared slot for reuse");

    expect_true(bondinvAddInvItem(ITEM_WPPK) == TRUE,
                "inventory accepts a new weapon");
    expect_true(bondinvAddInvItem(ITEM_KNIFE) == TRUE,
                "inventory accepts a second weapon");
    expect_true(bondinvAddInvItem(ITEM_KNIFE) == FALSE,
                "inventory rejects a duplicate weapon");
    expect_int(count_items(), 2,
               "inventory contains each accepted weapon exactly once");
    expect_int(item_at(0)->type_inv_item.type_weap.weapon, ITEM_KNIFE,
               "inventory sorts the lower weapon ID first");
    expect_int(item_at(1)->type_inv_item.type_weap.weapon, ITEM_WPPK,
               "inventory sorts the higher weapon ID second");
    expect_true(item_at(0)->prev == item_at(1) &&
                    item_at(1)->next == item_at(0),
                "inventory preserves circular previous and next links");

    expect_true(bondinvAddDoublesInvItem(ITEM_WPPK, ITEM_WPPKSIL) == TRUE,
                "inventory accepts a new dual-weapon entry");
    expect_true(bondinvAddDoublesInvItem(ITEM_WPPK, ITEM_WPPKSIL) == FALSE,
                "inventory rejects a duplicate dual-weapon entry");
    expect_true(bondinvHasDualWeapon(ITEM_WPPK, ITEM_WPPKSIL),
                "inventory finds a stored dual-weapon entry");
    expect_true(!bondinvHasDualWeapon(ITEM_WPPKSIL, ITEM_WPPK),
                "inventory keeps dual-weapon hand order significant");

    reset_fixture();
    expect_true(bondinvAddDoublesInvItem(ITEM_WPPK, ITEM_WPPKSIL),
                "inventory accepts a high dual entry before a lower one");
    expect_true(bondinvAddDoublesInvItem(ITEM_KNIFE, ITEM_THROWKNIFE),
                "inventory accepts a lower dual entry");
    expect_int(item_at(0)->type_inv_item.type_dual.weapon_right, ITEM_KNIFE,
               "inventory compares dual entries by their right-hand weapon");
    expect_int(item_at(0)->type_inv_item.type_dual.weapon_left, ITEM_THROWKNIFE,
               "inventory compares equal dual rights by their left-hand weapon");
}

static void test_inventory_availability_and_removal(void)
{
    reset_fixture();
    bondinvAddInvItem(ITEM_WPPK);
    bondinvAddDoublesInvItem(ITEM_WPPK, ITEM_WPPKSIL);

    expect_true(bondinvHasInvItem(ITEM_WPPK),
                "inventory reports a stored weapon as present");
    expect_true(!bondinvHasInvItem(ITEM_AK47),
                "inventory reports an absent weapon as missing");
    expect_true(bondinvItemAvailable(ITEM_WPPK),
                "inventory makes a stored weapon available");
    expect_true(!bondinvItemAvailable(ITEM_AK47),
                "inventory rejects an absent weapon when all guns is off");
    expect_true(!bondinvItemAvailableForHand(ITEM_WPPK, ITEM_UNARMED),
                "inventory requires an explicit dual entry for a second hand");
    expect_true(bondinvItemAvailableForHand(ITEM_WPPK, ITEM_WPPKSIL),
                "inventory finds an explicitly stored dual weapon");

    bondinvSetAllGunsFlag(TRUE);
    expect_true(bondinvGetAllGunsFlag() == TRUE,
                "inventory stores and reports the all-guns flag");
    expect_true(bondinvItemAvailable(ITEM_AK47),
                "all-guns mode exposes ordinary weapons without pickups");
    expect_true(!bondinvItemAvailable(ITEM_BOMBCASE),
                "all-guns mode does not expose the inventory sentinel");
    expect_true(bondinvItemAvailableForHand(dual_wield_weapon,
                                            dual_wield_weapon),
                "single-player all-guns mode permits a supported dual wield");

    bondinvSetAllGunsFlag(FALSE);
    bondinvRemoveItemByID(ITEM_WPPK);
    expect_true(!bondinvHasInvItem(ITEM_WPPK),
                "inventory removal by ID removes a normal weapon");
    expect_true(bondinvHasDualWeapon(ITEM_WPPK, ITEM_WPPKSIL),
                "inventory removal by ID leaves a distinct dual entry intact");
    bondinvRemoveItem(item_at(0));
    expect_int(count_items(), 0,
               "removing the final circular-list item clears the inventory");
}

static void test_inventory_indexing_and_text_overrides(void)
{
    textoverride override;
    ObjectRecord object;
    PropRecord prop;
    InvItem *weapon;
    u16 *expected;

    reset_fixture();
    memset(&override, 0, sizeof(override));
    memset(&object, 0, sizeof(object));
    override.objoffset = 0;
    override.weapon = ITEM_WPPK;
    override.titletext1 = 3;
    override.titletext2 = 4;
    override.shorttext = 1;
    override.longtext = 2;
    override.pickuptext = 5;
    override.obj = &object;
    bondinvAddTextOverride(&override);
    bondinvAddInvItem(ITEM_WPPK);
    weapon = item_at(0);

    expect_true(bondinvGetTextbyWeaponID(ITEM_WPPK) == &override,
                "inventory finds a weapon text override by ID");
    expect_true(bondinvGetTextbyObj(&object) == &override,
                "inventory finds an object text override by object pointer");
    expect_int(bondinvGetTextbyInvIndex(0), ITEM_WPPK,
               "inventory index resolves a normal weapon ID");
    expect_true(bondinvGetItemByIndex(0) == weapon &&
                    bondinvGetItemByIndex(1) == NULL,
                "inventory index returns only entries in range");

    expected = (u16 *)langGet(1);
    expect_true(bondinvGetNameByIndex(0) == expected,
                "inventory name lookup prefers an override short label");
    expected = (u16 *)langGet(2);
    expect_true(bondinvGetLongNameByIndex(0) == expected,
                "inventory long-name lookup prefers an override label");
    expect_true(bondinvGetFirstTitlebyIndex(0) == (u16 *)langGet(3) &&
                    bondinvGetSecondTitlebyIndex(0) == (u16 *)langGet(4),
                "inventory title lookup uses both override title lines");
    expect_true(bondinvGetActivatedTextWeapon(ITEM_WPPK) == langGet(5),
                "inventory activation text uses the weapon override");
    expect_true(bondinvGetActivatedTextObject(&object) == langGet(5),
                "inventory activation text uses the object override");

    prop.type = PROP_TYPE_OBJ;
    prop.obj = &object;
    object.flags2 = 0;
    bondinvAddPropToInv(&prop);
    expect_int(bondinvGetTextbyInvIndex(1), ITEM_WPPK,
               "inventory resolves an object property through its override");
    expect_true(bondinvGetNameByIndex(1) == (u16 *)langGet(1) &&
                    bondinvGetLongNameByIndex(1) == (u16 *)langGet(2) &&
                    bondinvGetFirstTitlebyIndex(1) == (u16 *)langGet(3) &&
                    bondinvGetSecondTitlebyIndex(1) == (u16 *)langGet(4),
                "inventory applies object overrides to every watch label");
    expect_true(bondinvGetActivatedTextWeapon(ITEM_AK47) == NULL &&
                    bondinvGetActivatedTextObject((ObjectRecord *)(uintptr_t)1) == NULL,
                "inventory returns no activation text for an unknown override");

    expect_float(bondinvGet45AngleForIndex(0), ITEM_WPPK + 0.5f,
                 "inventory exposes the equipment-page angle for an item");
    expect_float(bondinvGetDifferent45AngleForIndex(0), ITEM_WPPK + 1.5f,
                 "inventory exposes the main-page angle for an item");
    expect_float(bondinvGetHoffsetForIndex(0), ITEM_WPPK + 2.0f,
                 "inventory exposes the equipment horizontal offset");
    expect_float(bondinvGetVoffsetForIndex(0), ITEM_WPPK + 3.0f,
                 "inventory exposes the equipment vertical offset");
    expect_float(bondinvGetDepthForIndex(0), ITEM_WPPK + 4.0f,
                 "inventory exposes the equipment depth offset");
    expect_float(bondinvGetVposWatchForIndex(0), ITEM_WPPK + 5.0f,
                 "inventory exposes the main-page vertical position");
    expect_float(bondinvGetHposWatchForIndex(0), ITEM_WPPK + 6.0f,
                 "inventory exposes the main-page horizontal position");
    expect_float(bondinvGetDepthWatchForIndex(0), ITEM_WPPK + 7.0f,
                 "inventory exposes the main-page depth position");
    expect_float(bondinvGetXrotWatchForIndex(0), ITEM_WPPK + 8.0f,
                 "inventory exposes the watch X rotation");
    expect_float(bondinvGetYrotWatchForIndex(0), ITEM_WPPK + 9.0f,
                 "inventory exposes the watch Y rotation");
}

static void test_inventory_property_paths_and_all_guns_indices(void)
{
    PropRecord prop;
    ObjectRecord object;
    WeaponObjRecord weapon;
    InvItem *stored;

    reset_fixture();
    memset(&prop, 0, sizeof(prop));
    memset(&object, 0, sizeof(object));
    prop.type = PROP_TYPE_OBJ;
    prop.obj = &object;
    object.flags2 = 0;
    bondinvAddPropToInv(&prop);
    stored = item_at(0);
    expect_int(bondinvCountTotalItemsInInv(), 1,
               "inventory counts a normal object property");
    expect_true(bondinvGetItemByIndex(0) == stored,
                "inventory indexes a normal object property");
    object.flags2 = 0x40000;
    expect_int(bondinvCountTotalItemsInInv(), 0,
               "inventory excludes hidden object properties from its count");
    expect_true(bondinvGetItemByIndex(0) == NULL,
                "inventory excludes hidden object properties from its index");

    reset_fixture();
    memset(&weapon, 0, sizeof(weapon));
    memset(&prop, 0, sizeof(prop));
    weapon.type = PROPDEF_COLLECTABLE;
    weapon.weaponnum = ITEM_AK47;
    prop.type = PROP_TYPE_WEAPON;
    prop.weapon = &weapon;
    bondinvAddPropToInv(&prop);
    bondinvRemoveItemByID(ITEM_AK47);
    expect_true(!bondinvHasPropInInv(&prop),
                "inventory removal by ID also removes collectable properties");

    reset_fixture();
    memset(&weapon, 0, sizeof(weapon));
    memset(&prop, 0, sizeof(prop));
    weapon.type = PROPDEF_COLLECTABLE;
    weapon.weaponnum = ITEM_WPPK;
    prop.type = PROP_TYPE_WEAPON;
    prop.weapon = &weapon;
    bondinvAddWeaponByProp(&prop);
    expect_true(bondinvHasInvItem(ITEM_WPPK),
                "inventory imports a weapon from a collectable property");

    weapon.weaponnum = ITEM_AK47;
    weapon.LinkedWeaponType = ITEM_WPPKSIL;
    bondinvAddWeaponByProp(&prop);
    expect_true(bondinvHasDualWeapon(ITEM_AK47, ITEM_WPPKSIL),
                "inventory imports a linked dual weapon from a property");

    reset_fixture();
    memset(&weapon, 0, sizeof(weapon));
    memset(&prop, 0, sizeof(prop));
    weapon.type = PROPDEF_COLLECTABLE;
    weapon.weaponnum = ITEM_WPPK;
    weapon.flags = PROPFLAG_WEAPON_LEFTHANDED;
    weapon.LinkedWeaponType = ITEM_WPPKSIL;
    prop.type = PROP_TYPE_WEAPON;
    prop.weapon = &weapon;
    bondinvAddWeaponByProp(&prop);
    expect_true(bondinvHasDualWeapon(ITEM_WPPKSIL, ITEM_WPPK),
                "inventory preserves left-handed linked weapon order");

    reset_fixture();
    player_fixture.equipallguns = TRUE;
    expect_true(bondinvAddInvItem(ITEM_AK47) == FALSE,
                "all-guns pickup reports that an ordinary weapon was already available");
    expect_int(bondinvCountTotalItemsInInv(), ITEM_TANKSHELLS,
               "all-guns count excludes ordinary weapon duplicates");
    expect_true(bondinvGetItemByIndex(ITEM_TANKSHELLS - 1) == NULL,
                "all-guns index reserves its synthetic weapon prefix");
    expect_true(bondinvGetTextbyInvIndex(0) == ITEM_FIST,
                "all-guns text indexing exposes its first synthetic weapon");
    expect_true(bondinvGetNameByIndex(0) == (u16 *)langGet(ITEM_FIST) &&
                    bondinvGetLongNameByIndex(0) ==
                        (u16 *)langGet(ITEM_FIST + 1) &&
                    bondinvGetFirstTitlebyIndex(0) ==
                        (u16 *)langGet(ITEM_FIST + 2) &&
                    bondinvGetSecondTitlebyIndex(0) ==
                        (u16 *)langGet(ITEM_FIST + 3),
                "all-guns watch labels resolve synthetic weapon entries");

    reset_fixture();
    player_fixture.equipallguns = TRUE;
    expect_true(bondinvAddInvItem(ITEM_PLASTIQUE),
                "all-guns mode still stores non-weapon inventory entries");
    expect_int(bondinvCountTotalItemsInInv(), ITEM_TANKSHELLS + 1,
               "all-guns count includes entries beyond the synthetic weapon range");
    expect_true(bondinvGetItemByIndex(ITEM_TANKSHELLS) != NULL,
                "all-guns indexing returns stored entries after its prefix");
}

static void test_inventory_capacity_and_linked_weapon_edges(void)
{
    PropRecord prop;
    WeaponObjRecord weapon;
    WeaponObjRecord linked;
    s32 i;

    reset_fixture();
    for (i = 0; i < player_fixture.equipmaxitems; ++i)
    {
        item_storage[i].type = INV_ITEM_WEAPON;
    }
    expect_true(bondinvGetNextAvailItem() == NULL,
                "inventory reports no free slot when equipment capacity is full");

    reset_fixture();
    memset(&prop, 0, sizeof(prop));
    memset(&weapon, 0, sizeof(weapon));
    memset(&linked, 0, sizeof(linked));
    weapon.type = PROPDEF_COLLECTABLE;
    weapon.weaponnum = ITEM_WPPK;
    weapon.dualweapon = &linked;
    linked.type = PROPDEF_COLLECTABLE;
    linked.weaponnum = ITEM_WPPKSIL;
    prop.type = PROP_TYPE_WEAPON;
    prop.weapon = &weapon;
    expect_true(bondinvAddWeaponByProp(&prop),
                "inventory accepts a collectable with a linked weapon");
    expect_true(bondinvHasInvItem(ITEM_WPPK) &&
                    weapon.dualweapon == NULL && linked.dualweapon == NULL,
                "linked weapon import stores the primary item and consumes the link");
    expect_int(linked.LinkedWeaponType, ITEM_WPPK,
               "linked weapon import records the primary weapon ID");

    reset_fixture();
    memset(&prop, 0, sizeof(prop));
    memset(&weapon, 0, sizeof(weapon));
    memset(&linked, 0, sizeof(linked));
    weapon.type = PROPDEF_COLLECTABLE;
    weapon.weaponnum = ITEM_WPPK;
    weapon.flags = PROPFLAG_WEAPON_LEFTHANDED;
    weapon.dualweapon = &linked;
    linked.type = PROPDEF_COLLECTABLE;
    linked.weaponnum = ITEM_WPPKSIL;
    prop.type = PROP_TYPE_WEAPON;
    prop.weapon = &weapon;
    expect_true(bondinvAddWeaponByProp(&prop),
                "inventory accepts a left-handed linked weapon");
    expect_int(linked.LinkedWeaponType, ITEM_WPPK,
               "left-handed linking still records the primary weapon ID");
    expect_true(weapon.dualweapon == NULL && linked.dualweapon == NULL,
                "left-handed linking consumes both temporary link pointers");
}

static void test_inventory_cycle_and_weapon_history(void)
{
    s32 right;
    s32 left;

    reset_fixture();
    bondinvAddInvItem(ITEM_WPPK);
    bondinvAddInvItem(ITEM_KNIFE);

    right = ITEM_UNARMED;
    left = ITEM_UNARMED;
    bondinvCycleForward(&right, &left, FALSE);
    expect_int(right, ITEM_KNIFE,
               "forward inventory cycling chooses the next sorted weapon");
    bondinvCycleForward(&right, &left, FALSE);
    expect_int(right, ITEM_WPPK,
               "forward inventory cycling advances to the next weapon");

    bondinvCycleBackward(&right, &left, FALSE);
    expect_int(right, ITEM_KNIFE,
               "backward inventory cycling returns to the previous weapon");

    reset_fixture();
    bondinvAddDoublesInvItem(ITEM_WPPK, ITEM_WPPKSIL);
    right = ITEM_UNARMED;
    left = ITEM_UNARMED;
    bondinvCycleForward(&right, &left, TRUE);
    expect_true(right == ITEM_WPPK && left == ITEM_WPPKSIL,
                "forward inventory cycling selects a qualifying dual entry");
    right = ITEM_BOMBCASE;
    left = ITEM_UNARMED;
    bondinvCycleBackward(&right, &left, TRUE);
    expect_true(right == ITEM_WPPK && left == ITEM_WPPKSIL,
                "backward inventory cycling selects a qualifying dual entry");

    reset_fixture();
    bondinvAddInvItem(ITEM_KNIFE);
    right = ITEM_KNIFE;
    left = ITEM_UNARMED;
    bondinvCycleForward(&right, &left, FALSE);
    expect_true(right == ITEM_KNIFE && left == ITEM_UNARMED,
                "forward inventory cycling wraps when only one weapon exists");
    right = ITEM_UNARMED;
    left = ITEM_UNARMED;
    bondinvCycleBackward(&right, &left, FALSE);
    expect_true(right == ITEM_KNIFE && left == ITEM_UNARMED,
                "backward inventory cycling wraps when only one weapon exists");

    player_fixture.equipallguns = TRUE;
    right = ITEM_WPPK;
    left = ITEM_UNARMED;
    bondinvCycleForward(&right, &left, TRUE);
    expect_true(right != ITEM_UNARMED && right < ITEM_BOMBCASE,
                "all-guns cycling skips the unarmed sentinel while requiring ammo");

    player_fixture.equipallguns = FALSE;
    bondinvAddInvItem(ITEM_WPPK);
    memset(player_fixture.gunheldarr, 0xFF, sizeof(player_fixture.gunheldarr));
    held_time_weapon = ITEM_WPPK;
    bondinvIncrementHeldTime(held_time_weapon, ITEM_WPPKSIL);
    expect_int(player_fixture.gunheldarr[0].totaltime, g_ClockTimer,
               "weapon history allocates an unused held-time slot");
    expect_int(player_fixture.gunheldarr[0].weapon2, ITEM_UNARMED,
               "weapon history normalizes an unsupported off-hand weapon");
    bondinvIncrementHeldTime(held_time_weapon, ITEM_WPPKSIL);
    expect_int(player_fixture.gunheldarr[0].totaltime, 2 * g_ClockTimer,
               "weapon history accumulates time for the same weapon pair");

    bondinvIncrementHeldTime(ITEM_KNIFE, ITEM_UNARMED);
    right = ITEM_UNARMED;
    left = ITEM_UNARMED;
    bondinvGetWeaponOfChoice(&right, &left);
    expect_true(right == held_time_weapon && left == ITEM_UNARMED,
                "weapon history returns the pair with the greatest held time");
    expect_int(bondinvGetCurEquippedItem(), ITEM_UNARMED,
               "inventory exposes its current equipped index");
    bondinvSetCurEquippedItem(4);
    expect_int(bondinvGetCurEquippedItem(), 4,
               "inventory stores its current equipped index");
    current_right_weapon = ITEM_WPPK;
    bondinvDetermineEquippedItem();
    expect_int(bondinvGetCurEquippedItem(), 1,
               "inventory determines the equipped index from the active weapon");

    reset_fixture();
    for (right = 0; right < 10; right++)
    {
        player_fixture.gunheldarr[right].totaltime = right + 1;
    }
    bondinvIncrementHeldTime(held_time_weapon, ITEM_UNARMED);
    expect_int(player_fixture.gunheldarr[0].totaltime, g_ClockTimer,
               "weapon history replaces the least-used full history slot");
}

static void test_inventory_counting_and_flags(void)
{
    PropRecord prop;
    ObjectRecord object;
    WeaponObjRecord weapon;
    KeyRecord key;

    reset_fixture();
    bondinvAddInvItem(ITEM_WPPK);
    bondinvAddInvItem(ITEM_GOLDENGUN);
    expect_int(bondinvCountTotalItemsInInv(), 2,
               "inventory counts ordinary weapon entries");
    expect_true(bondinvHasGoldenGun(),
                "inventory detects the golden gun");

    memset(&object, 0, sizeof(object));
    memset(&weapon, 0, sizeof(weapon));
    memset(&prop, 0, sizeof(prop));
    weapon.type = PROPDEF_COLLECTABLE;
    weapon.runtime_bitflags = 0x400;
    weapon.weaponnum = ITEM_AK47;
    prop.type = PROP_TYPE_WEAPON;
    prop.weapon = &weapon;
    bondinvAddPropToInv(&prop);
    expect_true(bondinvHasPropInInv(&prop),
                "inventory detects a stored property pointer");
    expect_int(bondinvCountTotalItemsInInv(), 3,
               "inventory counts a collectable weapon property");
    expect_true(bondinvRemovePropWeaponByID(ITEM_AK47) == &weapon,
                "inventory removes and returns a matching collectable weapon");
    expect_true(!bondinvHasPropInInv(&prop),
                "inventory no longer reports a removed property");
    expect_true(bondinvRemovePropWeaponByID(ITEM_AK47) == NULL,
                "inventory returns null when no collectable weapon matches");

    memset(&key, 0, sizeof(key));
    key.type = PROPDEF_KEY;
    key.keyflags = 0x5;
    prop.type = PROP_TYPE_OBJ;
    prop.obj = (ObjectRecord *)&key;
    bondinvAddPropToInv(&prop);
    expect_true(bondinvCheckHasKeyFlags(0x1) &&
                    bondinvCheckHasKeyFlags(0x5) &&
                    !bondinvCheckHasKeyFlags(0x8),
                "inventory accumulates and matches key flag requirements");

    player_fixture.bonddead = FALSE;
    bondinvAddInvItem(ITEM_TOKEN);
    expect_true(bondinvIsAliveWithFlag(),
                "inventory reports a living player holding the flag token");
    player_fixture.bonddead = TRUE;
    expect_true(!bondinvIsAliveWithFlag(),
                "inventory suppresses the flag result after player death");

    reset_fixture();
    memset(&object, 0, sizeof(object));
    memset(&prop, 0, sizeof(prop));
    object.obj = PROJECTILES_TYPE_GE_KEY;
    prop.type = PROP_TYPE_WEAPON;
    prop.obj = &object;
    bondinvAddPropToInv(&prop);
    expect_true(bondinvHasGEKey(),
                "inventory detects the GoldenEye key projectile property");
    object.obj = 0;
    expect_true(!bondinvHasGEKey(),
                "inventory rejects a non-key weapon property");
}

/* External policy/data services used by bondinv.c.  These fakes make the
 * inventory contract independent of the live gun, text, and player systems. */
int getPlayerCount(void)
{
    return player_count;
}

u32 bondwalkItemCheckBitflags(ITEM_IDS item, u32 mask)
{
    if (mask == WEAPONSTATBITFLAG_CAN_DUAL_WIELD)
    {
        return item == dual_wield_weapon;
    }
    if (mask == WEAPONSTATBITFLAG_USE_HOLD_TIME)
    {
        return item == held_time_weapon;
    }
    return FALSE;
}

s32 bondwalkItemHasAmmo(ITEM_IDS item)
{
    (void)item;
    return ammo_available;
}

ITEM_IDS getCurrentPlayerWeaponId(GUNHAND hand)
{
    (void)hand;
    return current_right_weapon;
}

u8 *langGet(s32 slot)
{
    return (u8 *)text_storage[(u32)slot % 64u];
}

u16 *get_ptr_short_watch_text_for_item(ITEM_IDS item)
{
    return (u16 *)langGet(item);
}

u16 *get_ptr_long_watch_text_for_item(ITEM_IDS item)
{
    return (u16 *)langGet(item + 1);
}

u16 *get_ptr_first_title_line_item(ITEM_IDS item)
{
    return (u16 *)langGet(item + 2);
}

u16 *get_ptr_second_title_line_item(ITEM_IDS item)
{
    return (u16 *)langGet(item + 3);
}

f32 get_45_degree_angle_0(s32 item)
{
    return item + 0.5f;
}

f32 get_45_degree_angle(s32 item)
{
    return item + 1.5f;
}

f32 get_horizontal_offset_on_solo_watch_menu_for_item(ITEM_IDS item)
{
    return item + 2.0f;
}

f32 get_vertical_offset_on_solo_watch_menu_for_item(ITEM_IDS item)
{
    return item + 3.0f;
}

f32 get_depth_offset_solo_watch_menu_inventory_page_for_item(ITEM_IDS item)
{
    return item + 4.0f;
}

f32 get_vertical_position_solo_watch_menu_main_page_for_item(ITEM_IDS item)
{
    return item + 5.0f;
}

f32 get_lateral_position_solo_watch_menu_main_page_for_item(ITEM_IDS item)
{
    return item + 6.0f;
}

f32 get_depth_on_solo_watch_menu_page_for_item(ITEM_IDS item)
{
    return item + 7.0f;
}

f32 get_xrotation_solo_watch_menu_for_item(ITEM_IDS item)
{
    return item + 8.0f;
}

f32 get_yrotation_solo_watch_menu_for_item(ITEM_IDS item)
{
    return item + 9.0f;
}

int main(void)
{
    test_reinitialization_and_sorted_insertions();
    test_inventory_availability_and_removal();
    test_inventory_indexing_and_text_overrides();
    test_inventory_property_paths_and_all_guns_indices();
    test_inventory_capacity_and_linked_weapon_edges();
    test_inventory_cycle_and_weapon_history();
    test_inventory_counting_and_flags();

    if (failures != 0)
    {
        fprintf(stderr, "%d inventory contract(s) failed\n", failures);
        return 1;
    }

    puts("inventory suite passed");
    return 0;
}
