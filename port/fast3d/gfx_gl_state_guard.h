#ifndef GE007_GFX_GL_STATE_GUARD_H
#define GE007_GFX_GL_STATE_GUARD_H

#include "glad/glad.h"

/* RAII boundary for port-owned presentation/UI draws. Fast3D keeps several
 * bindings implicit between calls, so an auxiliary pass must restore the
 * complete state it touches before returning to the renderer. */
class GeGlStateGuard {
public:
    GeGlStateGuard()
    {
        glGetIntegerv(GL_CURRENT_PROGRAM, &program_);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer_);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture_);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer_);
        glGetIntegerv(GL_READ_BUFFER, &read_buffer_);
        glGetIntegerv(GL_DRAW_BUFFER, &draw_buffer_);
        glGetIntegerv(GL_VIEWPORT, viewport_);
        glGetIntegerv(GL_BLEND_SRC_RGB, &blend_src_rgb_);
        glGetIntegerv(GL_BLEND_DST_RGB, &blend_dst_rgb_);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src_alpha_);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst_alpha_);

        vao_supported_ = glBindVertexArray != nullptr;
        if (vao_supported_) {
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertex_array_);
        }

        framebuffer_supported_ = glBindFramebuffer != nullptr &&
                                 glReadBuffer != nullptr &&
                                 glDrawBuffer != nullptr;

        const GLboolean old_depth = glIsEnabled(GL_DEPTH_TEST);
        const GLboolean old_blend = glIsEnabled(GL_BLEND);
        const GLboolean old_cull = glIsEnabled(GL_CULL_FACE);
        const GLboolean old_scissor = glIsEnabled(GL_SCISSOR_TEST);
        depth_enabled_ = old_depth == GL_TRUE;
        blend_enabled_ = old_blend == GL_TRUE;
        cull_enabled_ = old_cull == GL_TRUE;
        scissor_enabled_ = old_scissor == GL_TRUE;

        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture0_);
        glActiveTexture((GLenum)active_texture_);
    }

    GeGlStateGuard(const GeGlStateGuard &) = delete;
    GeGlStateGuard &operator=(const GeGlStateGuard &) = delete;

    ~GeGlStateGuard()
    {
        if (framebuffer_supported_) {
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)framebuffer_);
            glReadBuffer((GLenum)read_buffer_);
            glDrawBuffer((GLenum)draw_buffer_);
        }
        glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);

        if (depth_enabled_) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
        if (blend_enabled_) glEnable(GL_BLEND);
        else glDisable(GL_BLEND);
        if (cull_enabled_) glEnable(GL_CULL_FACE);
        else glDisable(GL_CULL_FACE);
        if (scissor_enabled_) glEnable(GL_SCISSOR_TEST);
        else glDisable(GL_SCISSOR_TEST);

        if (glBlendFuncSeparate != nullptr) {
            glBlendFuncSeparate((GLenum)blend_src_rgb_, (GLenum)blend_dst_rgb_,
                                (GLenum)blend_src_alpha_, (GLenum)blend_dst_alpha_);
        } else {
            glBlendFunc((GLenum)blend_src_rgb_, (GLenum)blend_dst_rgb_);
        }

        glUseProgram((GLuint)program_);
        if (vao_supported_) {
            glBindVertexArray((GLuint)vertex_array_);
        }
        glBindBuffer(GL_ARRAY_BUFFER, (GLuint)array_buffer_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, (GLuint)texture0_);
        glActiveTexture((GLenum)active_texture_);
    }

private:
    GLint program_ = 0;
    GLint vertex_array_ = 0;
    GLint array_buffer_ = 0;
    GLint active_texture_ = GL_TEXTURE0;
    GLint texture0_ = 0;
    GLint framebuffer_ = 0;
    GLint read_buffer_ = GL_BACK;
    GLint draw_buffer_ = GL_BACK;
    GLint viewport_[4] = {0, 0, 0, 0};
    GLint blend_src_rgb_ = GL_SRC_ALPHA;
    GLint blend_dst_rgb_ = GL_ONE_MINUS_SRC_ALPHA;
    GLint blend_src_alpha_ = GL_SRC_ALPHA;
    GLint blend_dst_alpha_ = GL_ONE_MINUS_SRC_ALPHA;
    bool vao_supported_ = false;
    bool framebuffer_supported_ = false;
    bool depth_enabled_ = false;
    bool blend_enabled_ = false;
    bool cull_enabled_ = false;
    bool scissor_enabled_ = false;
};

#endif /* GE007_GFX_GL_STATE_GUARD_H */
