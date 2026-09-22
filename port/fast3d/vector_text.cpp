#include "vector_text.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <PR/gbi.h>

#include "glad/glad.h"

#include "gfx_api.h"
#include "gfx_gl_state_guard.h"
#include "gfx_opengl.h"
#include "gfx_shader_source.h"
#include "system.h"
#include "vector_text_layout.h"
#ifdef GE007_HAVE_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

namespace {

constexpr int kMaxCommands = 64;
constexpr int kMaxTextLength = 127;
constexpr int kAtlasWidth = 1024;
constexpr int kAtlasHeight = 1024;
constexpr int kFontPixelHeight = 13;
constexpr int kLineHeight = 15;

struct VectorTextCommand {
    int x;
    int y;
    uint32_t colour;
    char text[kMaxTextLength + 1];
};

#ifdef GE007_HAVE_FREETYPE
struct VectorGlyph {
    float u0;
    float v0;
    float u1;
    float v1;
    int width;
    int height;
    int bearing_x;
    int bearing_y;
    int advance;
};

FT_Library g_ft_library = nullptr;
FT_Face g_ft_face = nullptr;
std::array<VectorGlyph, 128> g_glyphs{};
std::array<uint8_t, kAtlasWidth * kAtlasHeight * 4> g_atlas{};
GLuint g_atlas_texture = 0;
GLuint g_program = 0;
GLuint g_vbo = 0;
GLuint g_vao = 0;
GLint g_position_location = -1;
GLint g_uv_location = -1;
GLint g_colour_location = -1;
GLint g_texture_location = -1;
float g_ascender = (float)kFontPixelHeight;
float g_face_line_height = (float)kLineHeight;
float g_raster_scale_y = 1.0f;
int g_raster_pixel_height = 0;
#endif

std::array<VectorTextCommand, kMaxCommands> g_commands{};
std::size_t g_command_count = 0;
bool g_attempted = false;
bool g_ready = false;
std::vector<float> g_vertices;

static bool fileExists(const char *path)
{
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    FILE *file = std::fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }
    std::fclose(file);
    return true;
}

#ifdef GE007_HAVE_FREETYPE
static bool loadFaceFromCandidates()
{
    const char *environment_path = std::getenv("GE_UI_FONT");
    const char *candidates[] = {
        environment_path,
        "data/fonts/LibreFranklin-Regular.ttf",
        "data/fonts/LibreFranklin-SemiBold.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "C:/Windows/Fonts/arial.ttf",
    };

    for (const char *candidate : candidates) {
        if (!fileExists(candidate)) {
            continue;
        }
        if (FT_New_Face(g_ft_library, candidate, 0, &g_ft_face) == 0) {
            sysLogPrintf(LOG_NOTE, "vector UI font: %s", candidate);
            return true;
        }
    }
    return false;
}

static bool loadShader(GLuint shader, const char *source)
{
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    return success == GL_TRUE;
}

static bool createVectorProgram()
{
    const std::string vertex_source = geGlslSourceWithRendererVersion(
        "#ifdef GL_ES\n"
        "precision mediump float;\n"
        "#endif\n"
        "in vec2 aPosition;\n"
        "in vec2 aUv;\n"
        "in vec4 aColour;\n"
        "out vec2 vUv;\n"
        "out vec4 vColour;\n"
        "void main() {\n"
        "  gl_Position = vec4(aPosition, 0.0, 1.0);\n"
        "  vUv = aUv;\n"
        "  vColour = aColour;\n"
        "}\n");
    const std::string fragment_source = geGlslSourceWithRendererVersion(
        "#ifdef GL_ES\n"
        "precision mediump float;\n"
        "#endif\n"
        "uniform sampler2D uAtlas;\n"
        "in vec2 vUv;\n"
        "in vec4 vColour;\n"
        "out vec4 outColour;\n"
        "void main() {\n"
        "  float coverage = texture(uAtlas, vUv).a;\n"
        "  outColour = vec4(vColour.rgb, vColour.a * coverage);\n"
        "}\n");
    if (vertex_source.empty() || fragment_source.empty()) {
        return false;
    }

    const GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    const GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    if (vertex_shader == 0 || fragment_shader == 0 ||
        !loadShader(vertex_shader, vertex_source.c_str()) ||
        !loadShader(fragment_shader, fragment_source.c_str())) {
        if (vertex_shader != 0) glDeleteShader(vertex_shader);
        if (fragment_shader != 0) glDeleteShader(fragment_shader);
        return false;
    }

    g_program = glCreateProgram();
    glAttachShader(g_program, vertex_shader);
    glAttachShader(g_program, fragment_shader);
    glBindAttribLocation(g_program, 0, "aPosition");
    glBindAttribLocation(g_program, 1, "aUv");
    glBindAttribLocation(g_program, 2, "aColour");
    glLinkProgram(g_program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    GLint linked = GL_FALSE;
    glGetProgramiv(g_program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        glDeleteProgram(g_program);
        g_program = 0;
        return false;
    }

    g_position_location = 0;
    g_uv_location = 1;
    g_colour_location = 2;
    g_texture_location = glGetUniformLocation(g_program, "uAtlas");
    return g_texture_location >= 0;
}

static bool createAtlas(int raster_pixel_height)
{
    if (raster_pixel_height <= 0 ||
        FT_Set_Pixel_Sizes(g_ft_face, 0, (FT_UInt)raster_pixel_height) != 0) {
        return false;
    }

    g_raster_scale_y = (float)raster_pixel_height / kFontPixelHeight;
    g_ascender = (float)(g_ft_face->size->metrics.ascender >> 6) /
                 g_raster_scale_y;
    g_face_line_height = std::max((float)kLineHeight,
                                  (float)(g_ft_face->size->metrics.height >> 6) /
                                  g_raster_scale_y);
    std::fill(g_glyphs.begin(), g_glyphs.end(), VectorGlyph{});
    std::fill(g_atlas.begin(), g_atlas.end(), 0);

    int pen_x = 1;
    int pen_y = 1;
    int row_height = 0;
    for (int codepoint = 32; codepoint < 127; ++codepoint) {
        if (FT_Load_Char(g_ft_face, (FT_ULong)codepoint, FT_LOAD_RENDER) != 0) {
            continue;
        }
        const FT_GlyphSlot glyph = g_ft_face->glyph;
        if (pen_x + (int)glyph->bitmap.width + 1 >= kAtlasWidth) {
            pen_x = 1;
            pen_y += row_height + 1;
            row_height = 0;
        }
        if (pen_y + (int)glyph->bitmap.rows + 1 >= kAtlasHeight) {
            return false;
        }

        VectorGlyph &entry = g_glyphs[(std::size_t)codepoint];
        entry.width = (int)glyph->bitmap.width;
        entry.height = (int)glyph->bitmap.rows;
        entry.bearing_x = glyph->bitmap_left;
        entry.bearing_y = glyph->bitmap_top;
        entry.advance = (int)(glyph->advance.x >> 6);
        entry.u0 = (float)pen_x / kAtlasWidth;
        entry.v0 = (float)pen_y / kAtlasHeight;
        entry.u1 = (float)(pen_x + entry.width) / kAtlasWidth;
        entry.v1 = (float)(pen_y + entry.height) / kAtlasHeight;

        const int pitch = glyph->bitmap.pitch;
        for (int row = 0; row < entry.height; ++row) {
            const int source_row = pitch >= 0 ? row : entry.height - 1 - row;
            const uint8_t *source = glyph->bitmap.buffer +
                                    (std::ptrdiff_t)source_row * (pitch >= 0 ? pitch : -pitch);
            for (int column = 0; column < entry.width; ++column) {
                const std::size_t atlas_index =
                    ((std::size_t)(pen_y + row) * kAtlasWidth +
                     (std::size_t)(pen_x + column)) * 4;
                g_atlas[atlas_index + 0] = 255;
                g_atlas[atlas_index + 1] = 255;
                g_atlas[atlas_index + 2] = 255;
                g_atlas[atlas_index + 3] = source[column];
            }
        }
        pen_x += entry.width + 1;
        row_height = std::max(row_height, entry.height);
    }

    if (g_atlas_texture == 0) {
        glGenTextures(1, &g_atlas_texture);
    }
    glBindTexture(GL_TEXTURE_2D, g_atlas_texture);
    /* The atlas is regenerated at output size. Nearest filtering preserves
     * FreeType's coverage samples and prevents filtering across the one-pixel
     * gutters into a neighboring glyph. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kAtlasWidth, kAtlasHeight, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, g_atlas.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    if (glGetError() != GL_NO_ERROR) {
        return false;
    }
    g_raster_pixel_height = raster_pixel_height;
    return true;
}

static int currentRasterPixelHeight()
{
    int32_t rect_x;
    int32_t rect_y;
    int32_t rect_width;
    int32_t rect_height;
    gfx_get_ui_screen_rect(&rect_x, &rect_y, &rect_width, &rect_height);
    (void)rect_x;
    (void)rect_y;
    return geVectorTextRasterPixelHeightForScreen(
        kFontPixelHeight, rect_width, rect_height,
        (float)gfx_current_native_viewport.width,
        (float)gfx_current_native_viewport.height);
}

static bool ensureAtlasForCurrentScale()
{
    const int raster_pixel_height = currentRasterPixelHeight();
    if (!geVectorTextNeedsAtlasRebuild(g_raster_pixel_height,
                                       raster_pixel_height)) {
        return true;
    }
    return createAtlas(raster_pixel_height);
}

static bool initializeVectorText()
{
    if (FT_Init_FreeType(&g_ft_library) != 0 || !loadFaceFromCandidates()) {
        return false;
    }
    if (!createVectorProgram() || !ensureAtlasForCurrentScale()) {
        return false;
    }

    glGenBuffers(1, &g_vbo);
    if (glGenVertexArrays != nullptr) {
        glGenVertexArrays(1, &g_vao);
    }
    return g_vbo != 0;
}

static const VectorGlyph &glyphFor(unsigned char codepoint)
{
    static const VectorGlyph fallback = {};
    if (codepoint < 32 || codepoint >= 127) {
        return fallback;
    }
    return g_glyphs[codepoint];
}

static int glyphKerning(unsigned char previous, unsigned char current)
{
    if (previous < 32 || previous >= 127 || current < 32 || current >= 127) {
        return 0;
    }
    FT_Vector kerning{};
    if (FT_Get_Kerning(g_ft_face, FT_Get_Char_Index(g_ft_face, previous),
                       FT_Get_Char_Index(g_ft_face, current), FT_KERNING_DEFAULT,
                       &kerning) != 0) {
        return 0;
    }
    return (int)(kerning.x >> 6);
}

/* One layout walk feeds both measurement and drawing. Keeping kerning,
 * newline handling, output-scale conversion, and advances in one place makes
 * the port overlay's hit/layout geometry agree with the pixels it emits. */
template <typename GlyphCallback, typename LineBreakCallback>
static void visitTextLayout(const char *text, float origin_x, float origin_y,
                            GlyphCallback on_glyph,
                            LineBreakCallback on_line_break)
{
    float pen_x = origin_x;
    float pen_y = origin_y;
    unsigned char previous = 'H';
    for (const unsigned char *cursor = (const unsigned char *)text;
         *cursor != '\0'; ++cursor) {
        if (*cursor == '\n') {
            on_line_break();
            pen_x = origin_x;
            pen_y += g_face_line_height;
            previous = 'H';
            continue;
        }
        const VectorGlyph &glyph = glyphFor(*cursor);
        pen_x += (float)glyphKerning(previous, *cursor) / g_raster_scale_y;
        const float glyph_x = pen_x;
        const float glyph_y = pen_y;
        pen_x += (float)glyph.advance / g_raster_scale_y;
        on_glyph(glyph, glyph_x, glyph_y, pen_x);
        previous = *cursor;
    }
}
#endif

static bool ensureReady()
{
    if (g_attempted) {
#ifdef GE007_HAVE_FREETYPE
        return g_ready && ensureAtlasForCurrentScale();
#else
        return false;
#endif
    }
    g_attempted = true;
#ifdef GE007_HAVE_FREETYPE
    g_ready = initializeVectorText();
    if (!g_ready) {
        sysLogPrintf(LOG_WARNING,
                     "vector UI unavailable; using the original bitmap font");
    }
#else
    g_ready = false;
#endif
    if (!g_ready) {
        return false;
    }
#ifdef GE007_HAVE_FREETYPE
    return ensureAtlasForCurrentScale();
#else
    return false;
#endif
}

} // namespace

extern "C" void gfx_vector_text_begin_frame(void)
{
    g_command_count = 0;
    g_vertices.clear();
}

extern "C" int gfx_vector_text_enabled(void)
{
    return gfx_get_render_mode() >= GFX_RENDER_ENHANCED && ensureReady() ? 1 : 0;
}

extern "C" int gfx_vector_text_measure(const char *text)
{
    if (!gfx_vector_text_enabled() || text == nullptr) {
        return 0;
    }

#ifdef GE007_HAVE_FREETYPE
    float longest = 0.0f;
    float current = 0.0f;
    visitTextLayout(text, 0.0f, 0.0f,
        [&current](const VectorGlyph &, float, float, float next_pen_x) {
            current = next_pen_x;
        },
        [&longest, &current]() {
            longest = std::max(longest, current);
            current = 0.0f;
        });
    return (int)(std::max(longest, current) + 0.5f);
#else
    return 0;
#endif
}

extern "C" int gfx_vector_text_queue(int x, int y, const char *text, uint32_t colour)
{
    if (!gfx_vector_text_enabled() || text == nullptr ||
        g_command_count >= g_commands.size()) {
        return 0;
    }

    VectorTextCommand &command = g_commands[g_command_count++];
    command.x = x;
    command.y = y;
    command.colour = colour;
    std::strncpy(command.text, text, kMaxTextLength);
    command.text[kMaxTextLength] = '\0';
    return 1;
}

#ifdef GE007_HAVE_FREETYPE
static void appendVertex(float x, float y, float u, float v,
                         float r, float g, float b, float a,
                         const GeVectorTextScreenRect &screen,
                         float logical_width, float logical_height)
{
    float mapped_x;
    float mapped_y;
    geVectorTextMapLogicalPoint(x, y, logical_width, logical_height,
                                screen, &mapped_x, &mapped_y);
    /* Keep glyph bounds on output-pixel boundaries. Without this, a
     * fractional window/native scale makes every edge sample two pixels and
     * reintroduces blur even when the atlas was rasterized at the new size. */
    mapped_x = std::round(mapped_x);
    mapped_y = std::round(mapped_y);
    const float window_width = (float)gfx_current_dimensions.width;
    const float window_height = (float)gfx_current_dimensions.height;
    g_vertices.push_back((mapped_x / window_width) * 2.0f - 1.0f);
    g_vertices.push_back(1.0f - (mapped_y / window_height) * 2.0f);
    g_vertices.push_back(u);
    g_vertices.push_back(v);
    g_vertices.push_back(r);
    g_vertices.push_back(g);
    g_vertices.push_back(b);
    g_vertices.push_back(a);
}

static void appendGlyphQuad(const VectorGlyph &glyph, float x, float y,
                            uint32_t colour,
                            const GeVectorTextScreenRect &screen,
                            float logical_width, float logical_height)
{
    if (glyph.width <= 0 || glyph.height <= 0) {
        return;
    }
    const float r = (float)((colour >> 24) & 0xff) / 255.0f;
    const float g = (float)((colour >> 16) & 0xff) / 255.0f;
    const float b = (float)((colour >> 8) & 0xff) / 255.0f;
    const float a = (float)(colour & 0xff) / 255.0f;
    const float x0 = x + (float)glyph.bearing_x / g_raster_scale_y;
    const float y0 = y + g_ascender -
                     (float)glyph.bearing_y / g_raster_scale_y;
    const float x1 = x0 + (float)glyph.width / g_raster_scale_y;
    const float y1 = y0 + (float)glyph.height / g_raster_scale_y;

    appendVertex(x0, y0, glyph.u0, glyph.v0, r, g, b, a,
                 screen, logical_width, logical_height);
    appendVertex(x1, y0, glyph.u1, glyph.v0, r, g, b, a,
                 screen, logical_width, logical_height);
    appendVertex(x1, y1, glyph.u1, glyph.v1, r, g, b, a,
                 screen, logical_width, logical_height);
    appendVertex(x0, y0, glyph.u0, glyph.v0, r, g, b, a,
                 screen, logical_width, logical_height);
    appendVertex(x1, y1, glyph.u1, glyph.v1, r, g, b, a,
                 screen, logical_width, logical_height);
    appendVertex(x0, y1, glyph.u0, glyph.v1, r, g, b, a,
                 screen, logical_width, logical_height);
}
#endif

extern "C" void gfx_vector_text_draw(void)
{
#ifndef GE007_HAVE_FREETYPE
    return;
#else
    if (!gfx_vector_text_enabled() || g_command_count == 0) {
        return;
    }

    int32_t rect_x;
    int32_t rect_y;
    int32_t rect_width;
    int32_t rect_height;
    gfx_get_ui_screen_rect(&rect_x, &rect_y, &rect_width, &rect_height);
    const GeVectorTextScreenRect screen = {rect_x, rect_y, rect_width, rect_height};
    const float logical_width = (float)gfx_current_native_viewport.width;
    const float logical_height = (float)gfx_current_native_viewport.height;

    g_vertices.clear();
    g_vertices.reserve(g_command_count * 256);
    for (std::size_t command_index = 0; command_index < g_command_count;
         ++command_index) {
        const VectorTextCommand &command = g_commands[command_index];
        visitTextLayout(command.text, (float)command.x, (float)command.y,
            [&command, &screen, logical_width, logical_height]
            (const VectorGlyph &glyph, float glyph_x, float glyph_y, float) {
                appendGlyphQuad(glyph, glyph_x, glyph_y, command.colour,
                                screen, logical_width, logical_height);
            },
            []() {});
    }
    if (g_vertices.empty()) {
        return;
    }

    {
        GeGlStateGuard state_guard;
        glUseProgram(g_program);
        if (g_vao != 0) {
            glBindVertexArray(g_vao);
        }
        glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)(g_vertices.size() * sizeof(float)),
                     g_vertices.data(), GL_STREAM_DRAW);
        glEnableVertexAttribArray((GLuint)g_position_location);
        glEnableVertexAttribArray((GLuint)g_uv_location);
        glEnableVertexAttribArray((GLuint)g_colour_location);
        glVertexAttribPointer((GLuint)g_position_location, 2, GL_FLOAT, GL_FALSE,
                              8 * (GLsizei)sizeof(float), (void *)(0 * sizeof(float)));
        glVertexAttribPointer((GLuint)g_uv_location, 2, GL_FLOAT, GL_FALSE,
                              8 * (GLsizei)sizeof(float), (void *)(2 * sizeof(float)));
        glVertexAttribPointer((GLuint)g_colour_location, 4, GL_FLOAT, GL_FALSE,
                              8 * (GLsizei)sizeof(float), (void *)(4 * sizeof(float)));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_atlas_texture);
        glUniform1i(g_texture_location, 0);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glViewport(0, 0, (GLsizei)gfx_current_dimensions.width,
                   (GLsizei)gfx_current_dimensions.height);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(g_vertices.size() / 8));
        glBindTexture(GL_TEXTURE_2D, 0);
    }
#endif
}
