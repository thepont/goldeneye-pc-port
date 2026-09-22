#ifndef GE007_INPUT_HARNESS_H
#define GE007_INPUT_HARNESS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Parse the opt-in GE_FAKE_CONTROLLERS test harness setting. */
int geInputFakeControllerCount(const char *value);

#ifdef __cplusplus
}
#endif

#endif
