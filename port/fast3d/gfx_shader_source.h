#ifndef GE007_GFX_SHADER_SOURCE_H
#define GE007_GFX_SHADER_SOURCE_H

#include <string>

#include "gfx_opengl.h"

/* All port-owned GLSL programs use the dialect selected by the active
 * renderer. This keeps compatibility, core, and GLES shader users from
 * quietly drifting into separate hard-coded version paths. */
static inline std::string geGlslSourceWithRendererVersion(const char *body)
{
    const char *version = gfx_opengl_get_glsl_version();
    if (version == nullptr || body == nullptr) {
        return std::string();
    }
    return std::string("#version ") + version + "\n" + body;
}

#endif /* GE007_GFX_SHADER_SOURCE_H */
