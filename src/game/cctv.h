#ifndef GE007_CCTV_H
#define GE007_CCTV_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CCTVRecord;

/* Return the setup pad that a CCTV head should look toward.  This is distinct
 * from ObjectRecord::pad, which places the physical camera prop. */
int32_t cctvGetLookPad(const struct CCTVRecord *cctv);

#ifdef __cplusplus
}
#endif

#endif
