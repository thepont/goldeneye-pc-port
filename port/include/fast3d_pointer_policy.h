#ifndef GE007_FAST3D_POINTER_POLICY_H
#define GE007_FAST3D_POINTER_POLICY_H

#include <stdint.h>

/* This is the range-only guard currently used by the PC fast3d decoder. It
 * rejects the two obvious integer/pointer sentinels, but it does not prove
 * that the address is actually mapped by the host process. The defect tests
 * deliberately keep that missing precondition visible. */
static inline int geFast3dPointerLooksMapped(uintptr_t address)
{
    return address >= (uintptr_t)0x10000 &&
           address < (uintptr_t)0x0000800000000000ULL;
}

#endif /* GE007_FAST3D_POINTER_POLICY_H */
