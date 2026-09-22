#ifndef ASCENSION_POSTFX_H
#define ASCENSION_POSTFX_H

#ifdef __cplusplus
extern "C" {
#endif

/* Presentation-only adaptive sharpening. It runs after the final Fast3D
 * composite and fails open if the active OpenGL context cannot support it. */
void ascensionPostFxApply(void);
void ascensionPostFxShutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* ASCENSION_POSTFX_H */
