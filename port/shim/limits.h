/*
 * PC port shim for <limits.h>.
 *
 * The N64 include/limits.h describes the console ABI and intentionally does
 * not expose host integer limits unless MIPS-specific macros are defined.
 * C++ port-only dependencies such as FreeType need the real host limits.
 * Keep C/game translation units on the original header and route C++ port
 * translation units through the generated absolute-path host header.
 */
#if defined(__cplusplus)
#include "hostlimits.h"
#else
#include "include/limits.h"
#endif
