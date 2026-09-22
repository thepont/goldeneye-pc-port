#ifndef GE007_INPUT_HARNESS_H
#define GE007_INPUT_HARNESS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Parse the opt-in GE_FAKE_CONTROLLERS test harness setting. */
int geInputFakeControllerCount(const char *value);

/* Testable boundary for the physical mouse -> N64 button translation. The
 * mouse bits are deliberately separate from the N64 mask so unit tests can
 * cover capture/menu gating without depending on SDL state. */
#define GE_INPUT_MOUSE_LEFT  0x01u
#define GE_INPUT_MOUSE_RIGHT 0x02u

unsigned geInputApplyMouseButtons(unsigned button,
                                  unsigned mouse_buttons,
                                  int mouse_grabbed,
                                  int menu_mode,
                                  int absolute_aim_suspended);

#ifdef __cplusplus
}
#endif

#endif
