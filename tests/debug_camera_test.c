#include <stdarg.h>
#include <stdio.h>

#include <ultra64.h>
#include "debug_camera.h"
#include "bondview.h"
#include "gun.h"

extern s32 debugCameraStateCounter;
extern coord3d debugCameraPosition;
extern coord3d debugCameraForward;
extern coord3d debugCameraUp;
extern f32 debugCameraHorizontalAngle;
extern f32 debugCameraHorizontalCosine;
extern f32 debugCameraHorizontalSine;
extern f32 debugCameraVerticalAngle;
extern f32 debugCameraVerticalCosine;
extern f32 debugCameraVerticalSine;
extern f32 debugCameraScale;
extern f32 debugCameraInverseScale;
extern coord3d debugCameraPreviousPosition;

static coord3d fake_player_position;
static f32 fake_player_yaw;
static f32 last_fovy;
static int right_item_removals;
static int left_item_removals;
static int solo_loads;
static int camera_matrix_updates;
static coord3d captured_camera_position;
static coord3d captured_camera_forward;
static coord3d captured_camera_up;
static int briefing_log_seen;
static f32 briefing_x;
static f32 briefing_y;
static f32 briefing_z;
static int failures;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

coord3d *bondviewGetCurrentPlayersPosition(void)
{
    return &fake_player_position;
}

f32 bondviewGetPlayerYawRadians(void)
{
    return fake_player_yaw;
}

void set_cur_player_fovy(f32 fovy)
{
    last_fovy = fovy;
}

void remove_item_in_hand(GUNHAND hand)
{
    if (hand == GUNRIGHT)
    {
        right_item_removals++;
    }
    else if (hand == GUNLEFT)
    {
        left_item_removals++;
    }
}

void solo_char_load(void)
{
    solo_loads++;
}

void bondviewUpdateCameraMatrices(coord3d *position,
                                  coord3d *forward,
                                  coord3d *up)
{
    camera_matrix_updates++;
    captured_camera_position = *position;
    captured_camera_forward = *forward;
    captured_camera_up = *up;
}

void osSyncPrintf(const char *format, ...)
{
    va_list args;

    if (format[0] == 'p')
    {
        va_start(args, format);
        briefing_x = (f32)va_arg(args, double);
        briefing_y = (f32)va_arg(args, double);
        briefing_z = (f32)va_arg(args, double);
        va_end(args);
        briefing_log_seen = 1;
    }
}

static void reset_fixture(void)
{
    fake_player_position.x = 0.0f;
    fake_player_position.y = 0.0f;
    fake_player_position.z = 0.0f;
    fake_player_yaw = 0.0f;
    last_fovy = 0.0f;
    right_item_removals = 0;
    left_item_removals = 0;
    solo_loads = 0;
    camera_matrix_updates = 0;
    briefing_log_seen = 0;
    briefing_x = 0.0f;
    briefing_y = 0.0f;
    briefing_z = 0.0f;
    debugCameraStateCounter = 0;
    debugCameraPosition.x = 0.0f;
    debugCameraPosition.y = 0.0f;
    debugCameraPosition.z = 0.0f;
    debugCameraPreviousPosition = debugCameraPosition;
    debugCameraHorizontalAngle = 0.0f;
    debugCameraVerticalAngle = 0.0f;
    debugCameraHorizontalCosine = 1.0f;
    debugCameraHorizontalSine = 0.0f;
    debugCameraVerticalCosine = 1.0f;
    debugCameraVerticalSine = 0.0f;
    debugCameraScale = 1.0f;
    debugCameraInverseScale = 1.0f;
}

static void test_world_briefing_rotation(void)
{
    reset_fixture();
    debugCameraPosition.x = 10.0f;
    debugCameraPosition.y = 20.0f;
    debugCameraPosition.z = 30.0f;
    fake_player_yaw = 1.57079632679f;

    initializeDebugCameraPosition();

    expect(briefing_log_seen,
           "debug camera initialization emits a briefing-key diagnostic");
    expect((briefing_x + 30.0f < 0.01f) && (briefing_x + 30.0f > -0.01f) &&
               (briefing_y - 20.0f < 0.01f) && (briefing_y - 20.0f > -0.01f) &&
               (briefing_z - 10.0f < 0.01f) && (briefing_z - 10.0f > -0.01f),
           "debug camera briefing coordinates rotate X/Z around the player");
}

static void test_scale_reset_and_world_position(void)
{
    reset_fixture();
    debugCameraPosition.x = 2.0f;
    debugCameraPosition.y = 4.0f;
    debugCameraPosition.z = 6.0f;
    setDebugCameraScale(2.0f);
    expect(debugCameraPosition.x == 1.0f && debugCameraPosition.y == 2.0f &&
               debugCameraPosition.z == 3.0f && debugCameraInverseScale == 0.5f,
           "debug camera scale preserves world coordinates while changing scale");

    fake_player_position.x = 11.0f;
    fake_player_position.y = 12.0f;
    fake_player_position.z = 13.0f;
    debugCameraPosition.x = 1.0f;
    debugCameraPosition.y = 2.0f;
    debugCameraPosition.z = 3.0f;
    debugCameraStateCounter = 9;
    resetDebugCameraToPlayerPosition();
    expect(debugCameraPosition.x == 11.0f && debugCameraPosition.y == 12.0f &&
               debugCameraPosition.z == 13.0f && debugCameraStateCounter == 0,
           "debug camera reset copies the player position and state");

    debugCameraPosition.x = 14.0f;
    debugCameraPosition.y = 15.0f;
    debugCameraPosition.z = 16.0f;
    updateDebugCameraWorldPosition();
    expect(debugCameraPreviousPosition.x == 14.0f &&
               debugCameraPreviousPosition.y == 15.0f &&
               debugCameraPreviousPosition.z == 16.0f,
           "debug camera world-position update advances the previous position");
}

static void test_free_camera_input_and_lifecycle(void)
{
    Gfx display_list[1] = {{0, 0}};

    reset_fixture();
    debugFreeCamera(-10, 10,
                    Z_TRIG | A_BUTTON | B_BUTTON |
                    L_CBUTTONS | R_CBUTTONS | L_JPAD | R_JPAD |
                    U_JPAD | D_JPAD | U_CBUTTONS | D_CBUTTONS);
    expect(debugCameraStateCounter == 1 && right_item_removals == 1 &&
               left_item_removals == 1,
           "first free-camera update removes both held items");
    expect(last_fovy > 0.0f && debugCameraHorizontalAngle >= 0.0f &&
               debugCameraHorizontalAngle < 360.0f &&
               debugCameraVerticalAngle >= 0.0f && debugCameraVerticalAngle < 360.0f,
           "free-camera input normalizes angles and updates field of view");

    debugFreeCamera(0, 0, R_TRIG);
    debugFreeCamera(0, 0, 0);
    debugFreeCamera(0, 0, 0);
    expect(debugCameraStateCounter == 4 && solo_loads == 1,
           "free-camera lifecycle loads the solo character on the fourth update");

    expect(sub_GAME_7F091580(display_list) == display_list &&
               camera_matrix_updates == 1 &&
               captured_camera_forward.y == debugCameraForward.y &&
               captured_camera_up.y == debugCameraUp.y,
           "debug camera render hook forwards the active camera basis");
}

int main(void)
{
    test_world_briefing_rotation();
    test_scale_reset_and_world_position();
    test_free_camera_input_and_lifecycle();

    if (failures != 0)
    {
        return 1;
    }

    puts("debug camera suite passed");
    return 0;
}
