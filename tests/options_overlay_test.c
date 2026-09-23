/*
 * Behavioral tests for the port-owned F10 graphics/options overlay.
 *
 * The test supplies deterministic fakes for the port boundaries and observes
 * only the public overlay API.  It intentionally does not create a GL
 * context, load a ROM, or run a level: menu state, input ownership, and text
 * layout are all host-side contracts.
 */

#include <SDL.h>

#include <PR/gbi.h>
#include <PR/ultratypes.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The repository's N64 stdlib stub intentionally omits the host abort(). */
extern void abort(void);

static void test_require(int condition, const char *expression,
                         const char *file, int line)
{
    if (!condition) {
        fprintf(stderr, "assertion failed: %s (%s:%d)\n",
                expression, file, line);
        abort();
    }
}

#undef assert
#define assert(condition) \
    test_require((condition), #condition, __FILE__, __LINE__)

#include "config.h"
#include "optionsoverlay.h"

struct font {
    int sentinel;
};
struct fontchar {
    int sentinel;
};

struct font g_font = { 1 };
struct fontchar g_font_chars = { 2 };
struct font *ptrFontBankGothic = &g_font;
struct fontchar *ptrFontBankGothicChars = &g_font_chars;

enum { MAX_FAKE_OPTIONS = 64, MAX_TEXT_CALLS = 512 };

struct fake_option {
    const char *key;
    int type;
    void *ptr;
    double min;
    double max;
};

struct meta_call {
    char key[64];
    const char *label;
};

struct text_call {
    char text[128];
    int x;
    int y;
    int width;
    int height;
    u32 colour;
    int used_vector_path;
};

static int cfg_fullscreen = 0;
static int cfg_vsync = 1;
static int cfg_fps_cap = 60;
static int cfg_render_mode = 1;
static int cfg_dynamic_lighting = 0;
static int cfg_bump_maps = 0;
static int cfg_msaa = 4;
static int cfg_texture_filter = 1;
static int cfg_fix_mip_textures = 1;
static int cfg_wrap_fix = 0;
static int cfg_fov_scale = 100;
static int cfg_widescreen_auto = 1;
static int cfg_draw_distance = 150;
static int cfg_draw_distance_auto = 1;
static int cfg_lod_distance = 150;
static int cfg_lod_distance_auto = 0;
static int cfg_anisotropy = 4;
static int cfg_safe_area_crop = 1;
static int cfg_postfx = 1;
static int cfg_sharpen = 35;
static int cfg_depth_of_field = 0;
static int cfg_mouse_sensitivity = 100;
static int cfg_mouse_invert_y = 0;
static int cfg_pad_invert_y = 0;
static int cfg_no_hit_flash = 0;
static int cfg_all_unlocked = 0;
static int cfg_display_fps = 0;

static struct fake_option fake_options[] = {
    { "Video.Fullscreen", CONFIG_OPT_INT, &cfg_fullscreen, 0, 1 },
    { "Video.VSync", CONFIG_OPT_INT, &cfg_vsync, 0, 1 },
    { "Video.FpsCap", CONFIG_OPT_INT, &cfg_fps_cap, 0, 1000 },
    { "Video.RenderMode", CONFIG_OPT_INT, &cfg_render_mode, 0, 2 },
    { "Video.DynamicLighting", CONFIG_OPT_INT, &cfg_dynamic_lighting, 0, 1 },
    { "Video.BumpMaps", CONFIG_OPT_INT, &cfg_bump_maps, 0, 1 },
    { "Video.MSAA", CONFIG_OPT_INT, &cfg_msaa, 1, 8 },
    { "Video.TextureFilter", CONFIG_OPT_INT, &cfg_texture_filter, 0, 2 },
    { "Video.FixMipTextures", CONFIG_OPT_INT, &cfg_fix_mip_textures, 0, 1 },
    { "Video.WrapFix", CONFIG_OPT_INT, &cfg_wrap_fix, 0, 1 },
    { "Video.FovScale", CONFIG_OPT_INT, &cfg_fov_scale, 50, 150 },
    { "Video.WidescreenAuto", CONFIG_OPT_INT, &cfg_widescreen_auto, 0, 1 },
    { "Video.DrawDistance", CONFIG_OPT_INT, &cfg_draw_distance, 100, 400 },
    { "Video.DrawDistanceAutoFov", CONFIG_OPT_INT, &cfg_draw_distance_auto, 0, 1 },
    { "Video.LodDistance", CONFIG_OPT_INT, &cfg_lod_distance, 25, 400 },
    { "Video.LodDistanceAutoFov", CONFIG_OPT_INT, &cfg_lod_distance_auto, 0, 1 },
    { "Video.Anisotropy", CONFIG_OPT_INT, &cfg_anisotropy, 1, 16 },
    { "Video.SafeAreaCrop", CONFIG_OPT_INT, &cfg_safe_area_crop, 0, 1 },
    { "Video.PostFX", CONFIG_OPT_INT, &cfg_postfx, 0, 1 },
    { "Video.Sharpen", CONFIG_OPT_INT, &cfg_sharpen, 0, 100 },
    { "Video.DepthOfField", CONFIG_OPT_INT, &cfg_depth_of_field, 0, 1 },
    { "Input.MouseSensitivity", CONFIG_OPT_INT, &cfg_mouse_sensitivity, 1, 500 },
    { "Input.MouseInvertY", CONFIG_OPT_INT, &cfg_mouse_invert_y, 0, 1 },
    { "Input.PadLookInvertY", CONFIG_OPT_INT, &cfg_pad_invert_y, 0, 1 },
    { "Game.NoHitFlash", CONFIG_OPT_INT, &cfg_no_hit_flash, 0, 1 },
    { "Game.AllUnlocked", CONFIG_OPT_INT, &cfg_all_unlocked, 0, 1 },
};

static struct meta_call meta_calls[MAX_FAKE_OPTIONS];
static size_t meta_call_count;
static struct text_call text_calls[MAX_TEXT_CALLS];
static size_t text_call_count;
static int vector_enabled;
static int vector_late_available = 1;
static int fake_vector_char_width = 6;
static int vector_call_count;
static int vector_late_call_count;
static int live_config_request_count;
static int config_save_count;
static int fake_pad_button = -1;
static int fake_time_step;
static int fake_vi_width = 640;
static int fake_vi_height = 480;

static void reset_text_calls(void)
{
    memset(text_calls, 0, sizeof(text_calls));
    text_call_count = 0;
    vector_call_count = 0;
    vector_late_call_count = 0;
}

/* ---- config fake ------------------------------------------------------ */

void configRegisterInt(const char *key, int *value, int min, int max)
{
    if (strcmp(key, "Video.DisplayFPS") == 0) {
        (void)min;
        (void)max;
        *value = cfg_display_fps;
    }
}

void configSave(void)
{
    config_save_count++;
}

void configSetOptionMeta(const char *key, const char *label, double step,
                         const char *const *enum_names)
{
    (void)step;
    (void)enum_names;
    if (meta_call_count >= MAX_FAKE_OPTIONS) {
        abort();
    }
    strncpy(meta_calls[meta_call_count].key, key,
            sizeof(meta_calls[meta_call_count].key) - 1);
    meta_calls[meta_call_count].key[
        sizeof(meta_calls[meta_call_count].key) - 1] = '\0';
    meta_calls[meta_call_count].label = label;
    meta_call_count++;
}

void configForEachOption(ConfigOptionCb callback, void *ctx)
{
    size_t i;
    for (i = 0; i < sizeof(fake_options) / sizeof(fake_options[0]); i++) {
        callback(fake_options[i].key, fake_options[i].type,
                 fake_options[i].ptr, fake_options[i].min, fake_options[i].max,
                 0.0, NULL, NULL, ctx);
    }
}

/* ---- video/input/system fakes --------------------------------------- */

void videoGetDesktopSize(int *w, int *h)
{
    *w = 1920;
    *h = 1080;
}

void videoGetWindowSize(int *w, int *h)
{
    *w = 640;
    *h = 480;
}

int videoIsFullscreen(void)
{
    return cfg_fullscreen;
}

void videoRequestWindowSize(int w, int h)
{
    (void)w;
    (void)h;
}

void videoRequestFullscreen(int on)
{
    cfg_fullscreen = on != 0;
}

void videoRequestLiveConfig(void)
{
    live_config_request_count++;
}

void inputSuspendForOverlay(void)
{
}

int inputPadButton(int index, SDL_GameControllerButton button)
{
    (void)index;
    return button == (SDL_GameControllerButton)fake_pad_button;
}

short inputPadAxis(int index, SDL_GameControllerAxis axis)
{
    (void)index;
    (void)axis;
    return 0;
}

uint64_t sysGetMicroseconds(void)
{
    return (uint64_t)(++fake_time_step) * 16666u;
}

void sysLogPrintf(int level, const char *format, ...)
{
    (void)level;
    (void)format;
}

/* The overlay maps mouse coordinates through this rectangle. A full-size
 * deterministic rectangle is enough here; mouse input is not the subject of
 * this test and the controller path remains fully exercised below. */
void gfx_get_ui_screen_rect(int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    *x = 0;
    *y = 0;
    *w = 640;
    *h = 480;
}

/* ---- text/render fakes ----------------------------------------------- */

int gfx_vector_text_enabled(void)
{
    return vector_enabled;
}

static int record_vector_text(int x, int y, const char *text, uint32_t colour)
{
    struct text_call *call;
    if (!vector_enabled || text == NULL) {
        return 0;
    }
    if (text_call_count >= MAX_TEXT_CALLS) {
        abort();
    }
    call = &text_calls[text_call_count++];
    strncpy(call->text, text, sizeof(call->text) - 1);
    call->text[sizeof(call->text) - 1] = '\0';
    call->x = x;
    call->y = y;
    call->colour = colour;
    call->used_vector_path = 1;
    return 1;
}

int gfx_vector_text_queue(int x, int y, const char *text, uint32_t colour)
{
    if (!record_vector_text(x, y, text, colour)) {
        return 0;
    }
    vector_call_count++;
    return 1;
}

int gfx_vector_text_queue_late(int x, int y, const char *text, uint32_t colour)
{
    if (!vector_late_available) {
        return 0;
    }
    if (!record_vector_text(x, y, text, colour)) {
        return 0;
    }
    vector_late_call_count++;
    return 1;
}

int gfx_vector_text_measure(const char *text)
{
    return text != NULL ? (int)strlen(text) * fake_vector_char_width : 0;
}

Gfx *microcode_constructor(Gfx *gdl)
{
    return gdl;
}

Gfx *textRender(Gfx *gdl, s32 *x, s32 *y, char *text,
                struct fontchar *chars, struct font *font, u32 colour,
                s32 width, s32 height, u32 y_offset, s32 lineheight)
{
    struct text_call *call;
    (void)chars;
    (void)font;
    (void)y_offset;
    (void)lineheight;
    if (text_call_count >= MAX_TEXT_CALLS) {
        abort();
    }
    call = &text_calls[text_call_count++];
    strncpy(call->text, text != NULL ? text : "", sizeof(call->text) - 1);
    call->text[sizeof(call->text) - 1] = '\0';
    call->x = *x;
    call->y = *y;
    call->width = width;
    call->height = height;
    call->colour = colour;
    call->used_vector_path = 0;
    return gdl;
}

void textMeasure(s32 *textheight, s32 *textwidth, char *text,
                 struct fontchar *chars, struct font *font, s32 lineheight)
{
    (void)chars;
    (void)font;
    (void)lineheight;
    *textheight = 10;
    *textwidth = text != NULL ? (s32)strlen(text) * 6 : 0;
}

s16 viGetX(void)
{
    return (s16)fake_vi_width;
}

s16 viGetY(void)
{
    return (s16)fake_vi_height;
}

static void assert_menu_metadata(void)
{
    static const char *const expected[] = {
        "Video.Fullscreen", "Video.VSync", "Video.FpsCap",
        "Video.RenderMode", "Video.DynamicLighting", "Video.BumpMaps",
        "Video.MSAA", "Video.TextureFilter", "Video.FixMipTextures",
        "Video.WrapFix", "Video.FovScale", "Video.WidescreenAuto",
        "Video.DrawDistance", "Video.DrawDistanceAutoFov",
        "Video.LodDistance", "Video.LodDistanceAutoFov", "Video.Anisotropy",
        "Video.SafeAreaCrop", "Video.PostFX", "Video.Sharpen",
        "Video.DepthOfField",
    };
    size_t i;

    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        size_t j;
        int found = 0;
        for (j = 0; j < meta_call_count; j++) {
            if (strcmp(meta_calls[j].key, expected[i]) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "graphics option missing from overlay: %s\n",
                    expected[i]);
            abort();
        }
    }
}

static const struct text_call *find_text(const char *text)
{
    size_t i;
    for (i = 0; i < text_call_count; i++) {
        if (strcmp(text_calls[i].text, text) == 0) {
            return &text_calls[i];
        }
    }
    return NULL;
}

static void assert_bitmap_value_is_right_aligned(const char *text)
{
    const struct text_call *call = find_text(text);
    int expected_x;
    if (call == NULL) {
        fprintf(stderr, "bitmap fallback did not emit value '%s'\n", text);
        abort();
    }
    expected_x = fake_vi_width - 20 - (int)strlen(text) * 6;
    if (call->x != expected_x) {
        fprintf(stderr,
                "bitmap fallback used vector metrics for '%s': x=%d expected=%d\n",
                text, call->x, expected_x);
        abort();
    }
}

static void assert_bitmap_value_is_centered(const char *text)
{
    const struct text_call *call = find_text(text);
    const int right = fake_vi_width - 20;
    const int center = ((right - 14) + (right + 7)) / 2;
    const int expected_x = center - (int)strlen(text) * 6 / 2;
    if (call == NULL) {
        fprintf(stderr, "bitmap fallback did not emit centered value '%s'\n", text);
        abort();
    }
    if (call->x != expected_x) {
        fprintf(stderr,
                "bitmap fallback used vector metrics for centered '%s': x=%d expected=%d\n",
                text, call->x, expected_x);
        abort();
    }
}

static void assert_bitmap_text_contract(void)
{
    size_t i;
    int saw_title = 0;
    int previous_row_y = -1;
    int row_count = 0;

    for (i = 0; i < text_call_count; i++) {
        const struct text_call *call = &text_calls[i];
        size_t j;
        if (call->used_vector_path) {
            abort();
        }
        if (call->width != viGetX() || call->height != viGetY()) {
            fprintf(stderr, "bitmap text was given a clipped text-sized rect: %s (%d x %d)\n",
                    call->text, call->width, call->height);
            abort();
        }
        for (j = 0; call->text[j] != '\0'; j++) {
            if ((unsigned char)call->text[j] < 0x20 ||
                (unsigned char)call->text[j] >= 0x7f) {
                fprintf(stderr, "overlay emitted a non-printable glyph in '%s'\n",
                        call->text);
                abort();
            }
        }
        if (strcmp(call->text, "PC OPTIONS") == 0) {
            saw_title = 1;
        }
        if (call->y >= 42) {
            if (previous_row_y >= 0 && call->y != previous_row_y &&
                call->y - previous_row_y < 15) {
                fprintf(stderr, "overlay text rows overlap at y=%d and y=%d\n",
                        previous_row_y, call->y);
                abort();
            }
            if (call->y != previous_row_y) {
                previous_row_y = call->y;
                row_count++;
            }
        }
    }
    if (!saw_title || row_count < 4) {
        abort();
    }
}

static int selected_label_is(const char *label)
{
    const struct text_call *call = find_text(label);
    return call != NULL && call->colour == 0xffffffffu;
}

static void pulse_controller_button(SDL_GameControllerButton button)
{
    fake_pad_button = button;
    optionsOverlayHandleInput();
    fake_pad_button = -1;
    optionsOverlayHandleInput();
}

static void select_label(const char *label)
{
    int i;
    for (i = 0; i < 64; i++) {
        reset_text_calls();
        optionsOverlayEmit();
        if (selected_label_is(label)) {
            return;
        }
        optionsOverlayScroll(-1);
    }
    fprintf(stderr, "overlay row was not reachable: %s\n", label);
    abort();
}

int main(void)
{
    int i;

    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    optionsOverlayToggle();
    assert(optionsOverlayIsOpen() != 0);
    assert_menu_metadata();

    /* The bitmap fallback must use the full VI clip rectangle and emit a
     * stable, non-overlapping list of printable strings. */
    vector_enabled = 0;
    fake_vi_width = 640;
    fake_vi_height = 480;
    reset_text_calls();
    optionsOverlayEmit();
    assert_bitmap_text_contract();

    /* The front end uses a smaller logical canvas. Exercise that boundary as
     * well; it is where clipped baselines and one-past-the-last-row display
     * list bugs tend to hide. */
    fake_vi_width = 440;
    fake_vi_height = 330;
    reset_text_calls();
    optionsOverlayEmit();
    assert_bitmap_text_contract();

    /* The real vector queue can become unavailable during a frame (for
     * example after its bounded command batch is full). The overlay must
     * fall back to one complete bitmap pass, not emit duplicate text or leave
     * half the menu on a different font path. */
    vector_enabled = 1;
    vector_late_available = 0;
    fake_vector_char_width = 11;
    reset_text_calls();
    optionsOverlayEmit();
    assert(vector_call_count == 0);
    assert(vector_late_call_count == 0);
    assert_bitmap_text_contract();
    assert_bitmap_value_is_right_aligned("ON");
    assert_bitmap_value_is_centered("X");
    vector_late_available = 1;
    fake_vector_char_width = 6;

    /* Enhanced mode must route the same menu strings to the vector path, not
     * emit both paths or silently fall back to the old low-resolution glyphs. */
    vector_enabled = 1;
    reset_text_calls();
    optionsOverlayEmit();
    assert(vector_call_count == 0);
    assert(vector_late_call_count > 0);
    for (i = 0; i < (int)text_call_count; i++) {
        assert(text_calls[i].used_vector_path != 0);
    }

    /* The in-level renderer uses the native 320x240 logical canvas. Keep this
     * boundary covered separately from the larger front-end canvas so a
     * future row or font-width change cannot push the panel off-screen. */
    vector_enabled = 0;
    fake_vi_width = 320;
    fake_vi_height = 240;
    reset_text_calls();
    optionsOverlayEmit();
    assert_bitmap_text_contract();

    /* Scroll through the public list until the graphics control is selected,
     * then drive it with a fake controller A edge. This proves the menu is
     * navigable without SDL hardware and that the selected row owns the edit. */
    vector_enabled = 0;
    select_label("Bump maps");
    assert(cfg_bump_maps == 0);
    pulse_controller_button(SDL_CONTROLLER_BUTTON_A);
    assert(cfg_bump_maps == 1);
    assert(live_config_request_count > 0);

    select_label("Fix mip textures");
    assert(cfg_fix_mip_textures == 1);
    pulse_controller_button(SDL_CONTROLLER_BUTTON_A);
    assert(cfg_fix_mip_textures == 0);

    select_label("Texture wrap fix");
    assert(cfg_wrap_fix == 0);
    pulse_controller_button(SDL_CONTROLLER_BUTTON_A);
    assert(cfg_wrap_fix == 1);

    optionsOverlayToggle();
    assert(optionsOverlayIsOpen() == 0);
    assert(config_save_count > 0);
    SDL_Quit();
    return 0;
}
