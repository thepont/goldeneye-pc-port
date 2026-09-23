#ifndef GE007_CONTROLLER_MAPPING_H
#define GE007_CONTROLLER_MAPPING_H

#ifdef __cplusplus
extern "C" {
#endif

/* N64 controller button bits. Keep the host controller translation at this
 * boundary so keyboard/mouse and SDL gamepad paths share one mask contract. */
#define GE_CONT_A      0x8000u
#define GE_CONT_B      0x4000u
#define GE_CONT_G      0x2000u  /* Z / fire */
#define GE_CONT_START  0x1000u
#define GE_CONT_UP     0x0800u
#define GE_CONT_DOWN   0x0400u
#define GE_CONT_LEFT   0x0200u
#define GE_CONT_RIGHT  0x0100u
#define GE_CONT_L      0x0020u
#define GE_CONT_R      0x0010u
#define GE_CONT_E      0x0008u  /* C-up */
#define GE_CONT_D      0x0004u  /* C-down */
#define GE_CONT_C      0x0002u  /* C-left */
#define GE_CONT_F      0x0001u  /* C-right */

typedef struct GeControllerDigitalState {
    int a_pressed;
    int x_pressed;
    int b_pressed;
    int y_pressed;
    int right_trigger_active;
    int left_trigger_active;
    int left_shoulder_rising;
    int right_shoulder_rising;
    int start_pressed;
    int dpad_up_pressed;
    int dpad_down_pressed;
    int dpad_left_pressed;
    int dpad_right_pressed;
} GeControllerDigitalState;

int geControllerAxisPastThreshold(int value, int threshold, int positive_direction);
int geControllerTriggerIsActive(int value, int threshold);
int geControllerScaleAxis(int value, int deadzone, int output_max);
int geControllerApplyLookInvertY(int value, int inverted);

/* Values intentionally match the PC-only GAMEMODE values in bondconstants.h
 * so front.c can route controller navigation without duplicating a mode map. */
#define GE_CONTROLLER_MENU_MODE_SOLO   0
#define GE_CONTROLLER_MENU_MODE_MULTI  1
#define GE_CONTROLLER_MENU_MODE_CHEATS 2
#define GE_CONTROLLER_MENU_MODE_COOP   3

/* Move through the visible mode-selector rows. `direction` is -1 for up and
 * +1 for down; unavailable rows are skipped and the list wraps. */
int geControllerMenuModeMove(int current_mode,
                             int direction,
                             int multiplayer_available,
                             int coop_available,
                             int cheats_available);

unsigned geControllerMapDigitalButtons(GeControllerDigitalState state,
                                       int menu_mode);

#ifdef __cplusplus
}
#endif

#endif /* GE007_CONTROLLER_MAPPING_H */
