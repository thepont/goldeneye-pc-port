#ifndef GE007_GFX_RELOCATION_POLICY_H
#define GE007_GFX_RELOCATION_POLICY_H

#include <stdint.h>

/* PC Gfx records occupy 16 bytes. The current model-GDL relocation path still
 * obtains a source offset by applying the N64 low-24-bit mask; retaining this
 * pure transformation lets the unit contract test the missing PC-stride
 * validation without loading a model or starting the renderer. */
#define GE_GFX_PC_STRIDE 16U

static inline uint32_t geGfxRelocationOffset(uint32_t encoded_address)
{
    return encoded_address & UINT32_C(0x00ffffff);
}

#endif /* GE007_GFX_RELOCATION_POLICY_H */
