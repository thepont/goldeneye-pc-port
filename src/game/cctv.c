#include <bondtypes.h>
#include "cctv.h"

int32_t cctvGetLookPad(const struct CCTVRecord *cctv)
{
    /* The PC compiler cannot represent the original duplicate `pad` member
     * name.  The explicit port name preserves the CCTV tail field at the same
     * serialized location instead of accidentally reading ObjectRecord::pad.
     */
#ifdef PORT
    return cctv->lookpad;
#else
    return cctv->pad;
#endif
}
