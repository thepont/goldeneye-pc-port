/*
 * Contract and compiler tests for the presentation shader embedded in the
 * OpenGL adapter.  The source is read from the production translation unit so
 * this test cannot silently validate a second copy of the shader.
 */
#include <array>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <SDL.h>
#include <string>
#include <string_view>
#include <vector>

#include "../port/fast3d/glad/glad.h"

namespace fs = std::filesystem;

static std::string read_text(const fs::path &path)
{
    std::ifstream input(path);
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

static std::string extract_shader(std::string_view source,
                                  std::string_view symbol)
{
    const std::string marker = std::string(symbol) + " = R\"GLSL(";
    const size_t start_marker = source.find(marker);
    if (start_marker == std::string_view::npos) return {};
    const size_t start = start_marker + marker.size();
    const size_t end = source.find(")GLSL\";", start);
    if (end == std::string_view::npos) return {};
    return std::string(source.substr(start, end - start));
}

static void require(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

static std::string shell_quote(const std::string &value)
{
    std::string quoted = "'";
    for (const char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += '\'';
    return quoted;
}

class TempDirectory {
public:
    TempDirectory()
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
        root_ = fs::temp_directory_path() /
                ("ge007-postfx-shader-" + std::to_string(stamp));
        for (unsigned int suffix = 0; !fs::create_directory(root_); ++suffix) {
            root_ = fs::temp_directory_path() /
                    ("ge007-postfx-shader-" + std::to_string(stamp) +
                     "-" + std::to_string(suffix));
        }
    }

    ~TempDirectory()
    {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    const fs::path &path() const { return root_; }

private:
    fs::path root_;
};

static void write_text(const fs::path &path, const std::string &text)
{
    std::ofstream output(path);
    require(output.good(), "could not create temporary shader source");
    output << text;
    require(output.good(), "could not write temporary shader source");
}

static int compile_profile(const std::string &validator,
                           const std::string &vertex_body,
                           const std::string &fragment_body,
                           std::string_view version,
                           const fs::path &root)
{
    const std::string suffix(version);
    const fs::path vertex_path = root / ("postfx-" + suffix + ".vert");
    const fs::path fragment_path = root / ("postfx-" + suffix + ".frag");
    const fs::path log_path = root / ("postfx-" + suffix + ".log");
    const std::string preamble = "#version " + suffix + "\n";
    write_text(vertex_path, preamble + vertex_body);
    write_text(fragment_path, preamble + fragment_body);

    const std::string command =
        "(" + shell_quote(validator) + " --no-link -S vert " +
        shell_quote(vertex_path.string()) + " && " +
        shell_quote(validator) + " --no-link -S frag " +
        shell_quote(fragment_path.string()) + ") >" +
        shell_quote(log_path.string()) + " 2>&1";
    const int result = std::system(command.c_str());
    if (result != 0) {
        std::cerr << "GLSL profile " << suffix << " failed:\n"
                  << read_text(log_path);
    }
    return result;
}

static void test_production_contract(const fs::path &source_root)
{
    const std::string source = read_text(
        source_root / "port" / "src" / "ascension_postfx.cpp");
    const std::string vertex = extract_shader(source, "kVertexShaderBody");
    const std::string fragment = extract_shader(source, "kFragmentShaderBody");
    require(!vertex.empty(), "production vertex shader literal is missing");
    require(!fragment.empty(), "production fragment shader literal is missing");
    require(fragment.find("uDofEnabled") != std::string::npos,
            "production fragment shader lost its DoF enable uniform");
    require(fragment.find("ascDofDepthWeight") != std::string::npos,
            "production fragment shader lost depth-aware blur rejection");
    require(fragment.find("ascDofViewport") != std::string::npos,
            "production fragment shader lost split-viewport isolation");
    require(fragment.find("fragColor") != std::string::npos,
            "production fragment shader has no color output");
}

static int test_compiler_profiles(const fs::path &source_root,
                                  const std::string &validator)
{
    const std::string source = read_text(
        source_root / "port" / "src" / "ascension_postfx.cpp");
    const std::string vertex = extract_shader(source, "kVertexShaderBody");
    const std::string fragment = extract_shader(source, "kFragmentShaderBody");
    require(!vertex.empty() && !fragment.empty(),
            "shader compiler test could not extract production literals");

    TempDirectory temporary;
    /* These are the dialects selected by gfx_opengl_init for compatibility,
     * core, and GLES contexts. */
    const std::vector<std::string> profiles = {"120", "130", "150 core",
                                               "330 core", "300 es"};
    for (const std::string &profile : profiles) {
        if (compile_profile(validator, vertex, fragment, profile,
                            temporary.path()) != 0) {
            return 1;
        }
    }
    std::cerr << "Ascension PostFX GLSL profiles passed: "
              << profiles.size() << '\n';
    return 0;
}

static void *sdl_get_proc(const char *name)
{
    return SDL_GL_GetProcAddress(name);
}

struct SdlVideoGuard {
    ~SdlVideoGuard()
    {
        SDL_Quit();
    }
};

struct SdlContextGuard {
    SDL_Window *window = nullptr;
    SDL_GLContext context = nullptr;

    ~SdlContextGuard()
    {
        if (context != nullptr) SDL_GL_DeleteContext(context);
        if (window != nullptr) SDL_DestroyWindow(window);
    }
};

struct GpuResources {
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint scene_texture = 0;
    GLuint depth_texture = 0;
    GLuint framebuffer = 0;
    GLuint output_texture = 0;

    ~GpuResources()
    {
        if (output_texture) glDeleteTextures(1, &output_texture);
        if (framebuffer) glDeleteFramebuffers(1, &framebuffer);
        if (depth_texture) glDeleteTextures(1, &depth_texture);
        if (scene_texture) glDeleteTextures(1, &scene_texture);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (program) glDeleteProgram(program);
    }
};

static std::string gpu_shader_log(GLuint shader, bool program)
{
    GLint length = 0;
    if (program) {
        glGetProgramiv(shader, GL_INFO_LOG_LENGTH, &length);
    } else {
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    }
    if (length <= 0) return {};
    std::vector<GLchar> log(static_cast<size_t>(length) + 1, '\0');
    GLsizei written = 0;
    if (program) {
        glGetProgramInfoLog(shader, length, &written, log.data());
    } else {
        glGetShaderInfoLog(shader, length, &written, log.data());
    }
    return std::string(log.data(), static_cast<size_t>(written));
}

static GLuint compile_gpu_shader(GLenum type, const std::string &source)
{
    const GLuint shader = glCreateShader(type);
    if (!shader) return 0;
    const char *text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        std::cerr << "GPU shader compile failed:\n"
                  << gpu_shader_log(shader, false);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint link_gpu_program(const std::string &vertex,
                               const std::string &fragment)
{
    const GLuint vertex_shader = compile_gpu_shader(GL_VERTEX_SHADER, vertex);
    const GLuint fragment_shader = compile_gpu_shader(GL_FRAGMENT_SHADER, fragment);
    if (!vertex_shader || !fragment_shader) {
        if (vertex_shader) glDeleteShader(vertex_shader);
        if (fragment_shader) glDeleteShader(fragment_shader);
        return 0;
    }

    const GLuint program = glCreateProgram();
    if (!program) {
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return 0;
    }
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glBindAttribLocation(program, 0, "aPosition");
    glBindAttribLocation(program, 1, "aTexCoord");
    glLinkProgram(program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        std::cerr << "GPU shader link failed:\n"
                  << gpu_shader_log(program, true);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

static GLuint create_rgba_texture(int width, int height,
                                  const std::vector<uint8_t> &pixels)
{
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, pixels.data());
    return texture;
}

static GLuint create_depth_texture(int width, int height,
                                   const std::vector<float> &depth)
{
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, width, height, 0, GL_RED,
                 GL_FLOAT, depth.data());
    return texture;
}

static std::vector<uint8_t> make_scene(int width, int height)
{
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool check = ((x / 2) + (y / 2)) % 2 == 0;
            const bool left = x < width / 2;
            const uint8_t r = left ? (check ? 255 : 20) : (check ? 20 : 0);
            const uint8_t g = left ? (check ? 25 : 0) : (check ? 25 : 0);
            const uint8_t b = left ? (check ? 25 : 0) : (check ? 255 : 20);
            const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
            pixels[offset + 0] = r;
            pixels[offset + 1] = g;
            pixels[offset + 2] = b;
            pixels[offset + 3] = 255;
        }
    }
    return pixels;
}

static std::vector<float> make_depth(int width, int height, bool split_focus)
{
    std::vector<float> depth(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            depth[static_cast<size_t>(y) * width + x] =
                split_focus && x < width / 2 ? 0.5f : 0.9f;
        }
    }
    return depth;
}

static float roi_difference(const std::vector<uint8_t> &left,
                            const std::vector<uint8_t> &right,
                            int width, int height, int x_begin, int x_end)
{
    double total = 0.0;
    size_t count = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = x_begin; x < x_end; ++x) {
            const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
            for (int channel = 0; channel < 3; ++channel) {
                total += std::abs(static_cast<int>(left[offset + channel]) -
                                  static_cast<int>(right[offset + channel]));
                ++count;
            }
        }
    }
    return count == 0 ? 0.0f : static_cast<float>(total / (count * 255.0));
}

static bool render_gpu_postfx(
    GLuint program, GLuint vao, GLuint framebuffer, GLuint scene_texture,
    GLuint depth_texture, int width, int height, bool dof_enabled,
    const std::array<float, 16> &viewports, GLint scene_loc, GLint depth_loc,
    GLint inv_size_loc, GLint strength_loc, GLint dof_enabled_loc,
    GLint dof_focus_loc, GLint dof_ranges_loc, GLint dof_max_radius_loc,
    GLint viewport_count_loc, GLint viewport_rects_loc,
    std::vector<uint8_t> &pixels)
{
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_texture);
    glUniform1i(scene_loc, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, depth_texture);
    glUniform1i(depth_loc, 1);
    glUniform2f(inv_size_loc, 1.0f / width, 1.0f / height);
    glUniform1f(strength_loc, 0.0f);
    glUniform1i(dof_enabled_loc, dof_enabled ? 1 : 0);
    glUniform1f(dof_focus_loc, 0.5f);
    glUniform4f(dof_ranges_loc, 0.1f, 0.2f, 0.6f, 0.7f);
    glUniform1f(dof_max_radius_loc, 3.0f);
    glUniform1i(viewport_count_loc, 2);
    glUniform4fv(viewport_rects_loc, 4, viewports.data());
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glFinish();
    pixels.resize(static_cast<size_t>(width) * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                 pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return glGetError() == GL_NO_ERROR;
}

static int test_gpu_execution(const fs::path &source_root)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SKIP: SDL video unavailable: " << SDL_GetError() << '\n';
        return 77;
    }
    SdlVideoGuard video_guard;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);
    SDL_Window *window = SDL_CreateWindow(
        "ge007 shader probe", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) {
        std::cerr << "SKIP: OpenGL window unavailable: " << SDL_GetError() << '\n';
        return 77;
    }
    SdlContextGuard context_guard;
    context_guard.window = window;
    context_guard.context = SDL_GL_CreateContext(window);
    if (!context_guard.context) {
        std::cerr << "SKIP: OpenGL context unavailable: " << SDL_GetError() << '\n';
        return 77;
    }
    if (!gladLoadGLLoader(sdl_get_proc)) {
        std::cerr << "SKIP: OpenGL entry points unavailable\n";
        return 77;
    }

    const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    if (!version) {
        std::cerr << "SKIP: OpenGL did not report a version\n";
        return 77;
    }
    std::cerr << "GPU shader probe using " << version << '\n';
    const std::string source = read_text(
        source_root / "port" / "src" / "ascension_postfx.cpp");
    const std::string vertex = "#version 330 core\n" +
                               extract_shader(source, "kVertexShaderBody");
    const std::string fragment = "#version 330 core\n" +
                                 extract_shader(source, "kFragmentShaderBody");
    GpuResources resources;
    resources.program = link_gpu_program(vertex, fragment);
    if (!resources.program) return 1;

    const int width = 32;
    const int height = 16;
    const std::vector<uint8_t> scene = make_scene(width, height);
    const std::vector<float> depth = make_depth(width, height, true);
    resources.scene_texture = create_rgba_texture(width, height, scene);
    resources.depth_texture = create_depth_texture(width, height, depth);
    glGenFramebuffers(1, &resources.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, resources.framebuffer);
    glGenTextures(1, &resources.output_texture);
    glBindTexture(GL_TEXTURE_2D, resources.output_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, resources.output_texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "FAIL: GPU probe framebuffer is incomplete\n";
        return 1;
    }

    const float quad[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
    };
    glGenVertexArrays(1, &resources.vao);
    glGenBuffers(1, &resources.vbo);
    glBindVertexArray(resources.vao);
    glBindBuffer(GL_ARRAY_BUFFER, resources.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<const void *>(2 * sizeof(float)));

    const GLint scene_loc = glGetUniformLocation(resources.program, "uScene");
    const GLint depth_loc = glGetUniformLocation(resources.program, "uDepth");
    const GLint inv_size_loc = glGetUniformLocation(resources.program, "uInvSize");
    const GLint strength_loc = glGetUniformLocation(resources.program, "uStrength");
    const GLint dof_enabled_loc = glGetUniformLocation(resources.program, "uDofEnabled");
    const GLint dof_focus_loc = glGetUniformLocation(resources.program, "uDofFocus");
    const GLint dof_ranges_loc = glGetUniformLocation(resources.program, "uDofRanges");
    const GLint dof_max_radius_loc = glGetUniformLocation(resources.program, "uDofMaxRadius");
    const GLint viewport_count_loc = glGetUniformLocation(resources.program, "uViewportCount");
    const GLint viewport_rects_loc = glGetUniformLocation(resources.program, "uViewportRects[0]");
    if (!(scene_loc >= 0 && depth_loc >= 0 && inv_size_loc >= 0 &&
          strength_loc >= 0 && dof_enabled_loc >= 0 && dof_focus_loc >= 0 &&
          dof_ranges_loc >= 0 && dof_max_radius_loc >= 0 &&
          viewport_count_loc >= 0 && viewport_rects_loc >= 0)) {
        std::cerr << "FAIL: GPU shader optimized away a required PostFX uniform\n";
        return 1;
    }

    const std::array<float, 16> full_viewports = {
        0.0f, 0.0f, 0.5f, 1.0f,
        0.5f, 0.0f, 0.5f, 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
    };
    std::vector<uint8_t> sharp;
    std::vector<uint8_t> depth_of_field;
    if (!render_gpu_postfx(resources.program, resources.vao,
                           resources.framebuffer, resources.scene_texture,
                           resources.depth_texture, width, height, false,
                           full_viewports, scene_loc, depth_loc, inv_size_loc,
                           strength_loc, dof_enabled_loc, dof_focus_loc,
                           dof_ranges_loc, dof_max_radius_loc,
                           viewport_count_loc, viewport_rects_loc, sharp) ||
        !render_gpu_postfx(resources.program, resources.vao,
                           resources.framebuffer, resources.scene_texture,
                           resources.depth_texture, width, height, true,
                           full_viewports, scene_loc, depth_loc, inv_size_loc,
                           strength_loc, dof_enabled_loc, dof_focus_loc,
                           dof_ranges_loc, dof_max_radius_loc,
                           viewport_count_loc, viewport_rects_loc,
                           depth_of_field)) {
        std::cerr << "FAIL: GPU PostFX draw/readback reported an OpenGL error\n";
        return 1;
    }
    const float left_difference = roi_difference(
        sharp, depth_of_field, width, height, 2, width / 2 - 2);
    const float right_difference = roi_difference(
        sharp, depth_of_field, width, height, width / 2 + 2, width - 2);
    require(left_difference < 0.002f,
            "in-focus viewport was blurred by the GPU DoF pass");
    require(right_difference > 0.01f,
            "out-of-focus viewport did not change under the GPU DoF pass");
    std::cerr << "GPU DoF ROI differences left=" << left_difference
              << " right=" << right_difference << '\n';
    return 0;
}

int main(int argc, char **argv)
{
#ifndef GE007_SOURCE_DIR
    std::cerr << "FAIL: shader test has no source root\n";
    return 1;
#else
    const fs::path source_root = GE007_SOURCE_DIR;
    if (argc == 2 && std::string_view(argv[1]) == "--gpu") {
        return test_gpu_execution(source_root);
    }
    test_production_contract(source_root);
    if (argc == 1) return 0;
    if (argc == 3 && std::string_view(argv[1]) == "--compile") {
        return test_compiler_profiles(source_root, argv[2]);
    }
    std::cerr << "usage: ascension_postfx_shader_test [--compile validator]\n";
    return 2;
#endif
}
