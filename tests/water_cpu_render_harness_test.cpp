/* Deterministic water render harness.
 *
 * This executable intentionally does not link the game, SDL, OpenGL, or ROM
 * loader. It renders the production water functional core into a fixed RGB
 * buffer, then checks the same image relationships used by the live BDD. */
#include "gfx_water_cpu_reference.h"
#include "gfx_water_time_policy.h"
#include "water_pixel_oracle.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using geWaterPixelOracle::Image;

struct TestState {
    int failures = 0;

    void expect(bool condition, const char *message)
    {
        if (!condition) {
            ++failures;
            std::cerr << "RED: " << message << '\n';
        }
    }
};

static Image render_image(const GeWaterCpuReferenceScene &scene, float time)
{
    Image image;
    image.width = static_cast<int>(scene.width);
    image.height = static_cast<int>(scene.height);
    image.pixels.resize(static_cast<std::size_t>(scene.width) * scene.height * 3);
    if (!geWaterCpuReferenceRender(&scene, time, image.pixels.data(),
                                   image.pixels.size())) {
        image.pixels.clear();
    }
    return image;
}

static bool equal_pixel(const Image &image, int x, int y,
                        std::uint8_t red, std::uint8_t green,
                        std::uint8_t blue)
{
    const std::size_t offset =
        (static_cast<std::size_t>(y) * image.width + x) * 3;
    return image.pixels[offset + 0] == red &&
           image.pixels[offset + 1] == green &&
           image.pixels[offset + 2] == blue;
}

static std::array<std::uint8_t, 3> pixel_at(const Image &image, int x, int y)
{
    const std::size_t offset =
        (static_cast<std::size_t>(y) * image.width + x) * 3;
    return {image.pixels[offset + 0], image.pixels[offset + 1],
            image.pixels[offset + 2]};
}

static GeWaterCpuReferenceScene brown_box_reflection_scene()
{
    GeWaterCpuReferenceScene scene =
        geWaterCpuReferenceDefaultScene();
    scene.occluder_top = 0.14f;
    scene.occluder_bottom = 0.46f;
    return scene;
}

static void test_brown_occluder_reflection(TestState &tests)
{
    /* A brown box above a blue water line is the smallest useful reflection
     * fixture. The sampled water pixel is below the box, so it is not direct
     * occlusion; only the reflected view can contribute brown energy. */
    const GeWaterCpuReferenceScene reflected_scene =
        brown_box_reflection_scene();

    GeWaterCpuReferenceScene sky_only_scene = reflected_scene;
    sky_only_scene.occluder_left = 1.1f;
    sky_only_scene.occluder_right = 1.2f;

    const float time = 25.25f;
    const Image reflected = render_image(reflected_scene, time);
    const Image sky_only = render_image(sky_only_scene, time);
    tests.expect(!reflected.pixels.empty() && !sky_only.pixels.empty(),
                 "brown-box reflection fixture must produce both reference images");
    if (reflected.pixels.empty() || sky_only.pixels.empty()) {
        return;
    }
    const auto with_box = pixel_at(reflected, 48, 37);
    const auto without_box = pixel_at(sky_only, 48, 37);

    tests.expect(with_box[0] > without_box[0] + 2,
                 "water reflection must carry measurable red energy from the brown box");
    tests.expect(with_box[2] < without_box[2],
                 "a brown reflected box must not collapse to the same blue-only water sample");
    std::cout << "WATER_CPU_REFLECTION: with-box="
              << static_cast<unsigned>(with_box[0]) << ','
              << static_cast<unsigned>(with_box[1]) << ','
              << static_cast<unsigned>(with_box[2]) << " without-box="
              << static_cast<unsigned>(without_box[0]) << ','
              << static_cast<unsigned>(without_box[1]) << ','
              << static_cast<unsigned>(without_box[2]) << '\n';
}

static void test_wave_budget_for_open_boat(TestState &tests)
{
    const float remaster_bound =
        geWaterCompositeWaveBound(GE_WATER_COMPOSITE_REMASTER);
    tests.expect(remaster_bound <= 0.02f,
                 "global water waves must remain a restrained displacement budget for a floating speedboat");

    const GeWaterWaveSample sample = geWaterCompositeWaveSample(
        {0.37f, 0.61f}, 25.25f, GE_WATER_DEFAULT_WAVELENGTH,
        GE_WATER_DEFAULT_AMPLITUDE, GE_WATER_DEFAULT_SPEED,
        geWaterCompositeWaveScale(GE_WATER_COMPOSITE_REMASTER),
        {GE_WATER_DEFAULT_DIRECTION_X, GE_WATER_DEFAULT_DIRECTION_Y});
    tests.expect(geWaterFinite(sample.height) &&
                     geWaterAbs(sample.height) <= remaster_bound &&
                     geWaterFinite(sample.derivative_x) &&
                     geWaterFinite(sample.derivative_y),
                 "open-boat wave samples must stay finite and inside the global amplitude budget");
}

static void test_fixed_scene(TestState &tests, Image &first, Image &last)
{
    GeWaterCpuReferenceScene scene = geWaterCpuReferenceDefaultScene();
    scene.render_mode = GE_WATER_COMPOSITE_REMASTER;

    /* The harness advances only an injected fixed clock. No host timestamp is
     * read, so these image times are part of the test fixture. */
    GeWaterClockState clock = {};
    geWaterClockAdvance(&clock, 0.0, 0.25);
    const float first_time = clock.elapsed;
    for (int frame = 0; frame < 100; ++frame) {
        geWaterClockAdvance(&clock, (double)frame + 1.0, 0.25);
    }
    const float last_time = clock.elapsed;

    first = render_image(scene, first_time);
    last = render_image(scene, last_time);
    const Image repeat = render_image(scene, first_time);

    tests.expect(!first.pixels.empty() && first.pixels == repeat.pixels,
                 "identical fixed inputs must produce byte-identical output");
    tests.expect(first.pixels.size() == last.pixels.size(),
                 "fixed scene captures must have equal dimensions");
    tests.expect(first.width == 96 && first.height == 64,
                 "the reference fixture must keep its bounded dimensions");
    tests.expect(geWaterPixelOracle::roi_variance(
                     last, last.height / 2, last.height) > 0.00001f,
                 "water region must contain spatial variation");
    tests.expect(geWaterPixelOracle::luma(last, 8, 56) > 0.25f,
                 "open water must be display-encoded instead of raw linear dark blue");
    tests.expect(geWaterPixelOracle::vertical_asymmetry(last) > 0.002f,
                 "reflection fixture must differ from the sky across the water line");
    tests.expect(geWaterPixelOracle::edge_count(
                     last, last.height / 2, last.height) > 8,
                 "the depth-tested ship/dam fixture must leave visible edges");
    tests.expect(geWaterPixelOracle::mean_difference(
                     first, last, 0, first.height) > 0.001f,
                 "fixed-step wave time must produce a measurable image change");

    /* The occluder is closer than the water surface. The compositor must
     * preserve its opaque scene colour instead of painting water over it. */
    const int occluder_x = 48;
    const int occluder_y = 40;
    const float u = ((float)occluder_x + 0.5f) / scene.width;
    const float v = ((float)occluder_y + 0.5f) / scene.height;
    const GeWaterVec3 expected = geWaterCpuReferenceSceneColor(
        &scene, u, v);
    tests.expect(equal_pixel(
                     last, occluder_x, occluder_y,
                     geWaterCpuReferenceByte(expected.x),
                     geWaterCpuReferenceByte(expected.y),
                     geWaterCpuReferenceByte(expected.z)),
                 "closer opaque geometry must survive water depth testing");

    tests.expect(geWaterCpuReferenceRender(
                     &scene, first_time, nullptr, first.pixels.size()) == 0,
                 "null output buffers must fail closed");
    tests.expect(geWaterCpuReferenceRender(
                     &scene, first_time, first.pixels.data(), 2) == 0,
                 "undersized output buffers must fail closed");

    std::cout << "WATER_CPU_REFERENCE: size=" << first.width << 'x'
              << first.height << " times=" << first_time << ',' << last_time
              << " variance=" << geWaterPixelOracle::roi_variance(
                                      last, last.height / 2, last.height)
              << " asymmetry=" << geWaterPixelOracle::vertical_asymmetry(last)
              << " edges=" << geWaterPixelOracle::edge_count(
                                      last, last.height / 2, last.height)
              << " temporal=" << geWaterPixelOracle::mean_difference(
                                      first, last, 0, last.height)
              << '\n';
}

static int run_self_test(const std::string &dump_directory)
{
    TestState tests;
    Image first, last;
    test_brown_occluder_reflection(tests);
    test_wave_budget_for_open_boat(tests);
    test_fixed_scene(tests, first, last);

    if (tests.failures == 0 && !dump_directory.empty()) {
        std::error_code error;
        fs::create_directories(dump_directory, error);
        tests.expect(!error, "CPU harness could not create its dump directory");
        if (tests.failures == 0) {
            tests.expect(geWaterPixelOracle::write_ppm(
                             (fs::path(dump_directory) / "water_cpu_t0.ppm").string(),
                             first),
                         "CPU harness could not write its first PPM");
            tests.expect(geWaterPixelOracle::write_ppm(
                             (fs::path(dump_directory) / "water_cpu_t1.ppm").string(),
                             last),
                         "CPU harness could not write its second PPM");
            const Image brown_box = render_image(
                brown_box_reflection_scene(), 25.25f);
            tests.expect(geWaterPixelOracle::write_ppm(
                             (fs::path(dump_directory) /
                              "water_brown_box_reflection.ppm").string(),
                             brown_box),
                         "CPU harness could not write its brown-box reflection PPM");
        }
    }

    if (tests.failures != 0) {
        std::cerr << tests.failures << " deterministic water harness check(s) are red\n";
        return 1;
    }
    std::cout << "water CPU render harness passed\n";
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--self-test")) {
        return run_self_test({});
    }
    if (argc == 3 && std::string(argv[1]) == "--dump-dir") {
        return run_self_test(argv[2]);
    }
    std::cerr << "usage: water_cpu_render_harness_test [--self-test] | "
                 "--dump-dir DIRECTORY\n";
    return 2;
}
