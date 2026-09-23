#ifndef GE007_FAST3D_POINTER_POLICY_H
#define GE007_FAST3D_POINTER_POLICY_H

#include <stdint.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

/* Validate the address against the host process's mapped ranges before the
 * fast3d decoder reads a matrix or vertex buffer. The old canonical-address
 * range check accepted stale 32-bit N64 values such as 0x401c68e0. */
static inline int geFast3dPointerLooksMapped(uintptr_t address)
{
#if defined(_WIN32)
    MEMORY_BASIC_INFORMATION info;
    if (address == 0 || VirtualQuery((const void *)address, &info,
                                      sizeof(info)) == 0)
    {
        return 0;
    }
    return info.State == MEM_COMMIT &&
           (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) == 0;
#elif defined(__linux__)
    const long page_size = sysconf(_SC_PAGESIZE);
    unsigned char residency;
    uintptr_t page;

    if (address == 0 || page_size <= 0)
    {
        return 0;
    }
    page = address & ~((uintptr_t)page_size - 1U);
    return mincore((void *)page, (size_t)page_size, &residency) == 0;
#else
    /* Platforms without a mapped-range query retain the canonical fallback;
     * the fast3d call sites still reject null/non-canonical values. */
    return address >= (uintptr_t)0x10000 &&
           address < (uintptr_t)0x0000800000000000ULL;
#endif
}

#endif /* GE007_FAST3D_POINTER_POLICY_H */
