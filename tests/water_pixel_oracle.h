#ifndef GE007_WATER_PIXEL_ORACLE_H
#define GE007_WATER_PIXEL_ORACLE_H

/* Small, dependency-free image metrics shared by the CPU water harness and
 * the display-backed water BDD.  These measure relationships in a generated
 * image; they do not depend on a GL context or a game/ROM process. */

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace geWaterPixelOracle {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

inline bool next_token(std::istream &input, std::string &token)
{
    token.clear();
    char ch = 0;
    while (input.get(ch)) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            continue;
        }
        if (ch == '#') {
            input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }
        token.push_back(ch);
        break;
    }
    while (input.get(ch) && !std::isspace(static_cast<unsigned char>(ch))) {
        token.push_back(ch);
    }
    return !token.empty();
}

inline bool load_ppm(const std::string &path, Image &image)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::string magic, width, height, max_value;
    if (!next_token(input, magic) || magic != "P6" ||
        !next_token(input, width) || !next_token(input, height) ||
        !next_token(input, max_value)) {
        return false;
    }
    image.width = std::stoi(width);
    image.height = std::stoi(height);
    if (image.width <= 0 || image.height <= 0 || max_value != "255") {
        return false;
    }
    image.pixels.resize(static_cast<std::size_t>(image.width) * image.height * 3);
    input.read(reinterpret_cast<char *>(image.pixels.data()),
               static_cast<std::streamsize>(image.pixels.size()));
    return input.gcount() == static_cast<std::streamsize>(image.pixels.size());
}

inline bool write_ppm(const std::string &path, const Image &image)
{
    if (image.width <= 0 || image.height <= 0 ||
        image.pixels.size() != static_cast<std::size_t>(image.width) *
            image.height * 3) {
        return false;
    }
    std::ofstream output(path, std::ios::binary);
    if (!output) return false;
    output << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    output.write(reinterpret_cast<const char *>(image.pixels.data()),
                 static_cast<std::streamsize>(image.pixels.size()));
    return output.good();
}

inline float luma(const Image &image, int x, int y)
{
    const std::size_t offset =
        (static_cast<std::size_t>(y) * image.width + x) * 3;
    return (0.2126f * image.pixels[offset] +
            0.7152f * image.pixels[offset + 1] +
            0.0722f * image.pixels[offset + 2]) / 255.0f;
}

inline float roi_variance(const Image &image, int top, int bottom)
{
    top = std::max(0, top);
    bottom = std::min(image.height, bottom);
    if (top >= bottom) return 0.0f;
    double sum = 0.0;
    double squared = 0.0;
    std::size_t count = 0;
    for (int y = top; y < bottom; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const double value = luma(image, x, y);
            sum += value;
            squared += value * value;
            ++count;
        }
    }
    const double mean = sum / static_cast<double>(count);
    return static_cast<float>(std::max(0.0, squared / count - mean * mean));
}

inline float mean_difference(const Image &left, const Image &right,
                             int top, int bottom)
{
    if (left.width != right.width || left.height != right.height) return 0.0f;
    top = std::max(0, top);
    bottom = std::min(left.height, bottom);
    double total = 0.0;
    std::size_t count = 0;
    for (int y = top; y < bottom; ++y) {
        for (int x = 0; x < left.width; ++x) {
            total += std::fabs(luma(left, x, y) - luma(right, x, y));
            ++count;
        }
    }
    return count == 0 ? 0.0f : static_cast<float>(total / count);
}

inline float vertical_asymmetry(const Image &image)
{
    double total = 0.0;
    std::size_t count = 0;
    for (int y = image.height / 2; y < image.height; ++y) {
        const int mirror_y = image.height - 1 - y;
        for (int x = 0; x < image.width; ++x) {
            total += std::fabs(luma(image, x, y) - luma(image, x, mirror_y));
            ++count;
        }
    }
    return count == 0 ? 0.0f : static_cast<float>(total / count);
}

inline std::size_t edge_count(const Image &image, int top, int bottom)
{
    std::size_t edges = 0;
    top = std::max(1, top);
    bottom = std::min(image.height - 1, bottom);
    for (int y = top; y < bottom; ++y) {
        for (int x = 1; x < image.width - 1; ++x) {
            const float horizontal = std::fabs(luma(image, x + 1, y) -
                                               luma(image, x - 1, y));
            const float vertical = std::fabs(luma(image, x, y + 1) -
                                             luma(image, x, y - 1));
            if (horizontal + vertical > 0.18f) ++edges;
        }
    }
    return edges;
}

inline Image self_test_image(int width, int height, bool changed)
{
    Image image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<std::size_t>(width) * height * 3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * width + x) * 3;
            const std::uint8_t wave =
                static_cast<std::uint8_t>((x * 29 + y * 47) & 255);
            image.pixels[offset + 0] = wave;
            image.pixels[offset + 1] =
                static_cast<std::uint8_t>((wave + y * 9) & 255);
            image.pixels[offset + 2] =
                static_cast<std::uint8_t>((wave + x * 13) & 255);
            if (changed && y >= height / 2 && x == width / 2) {
                image.pixels[offset + 0] = 255;
                image.pixels[offset + 1] = 255;
                image.pixels[offset + 2] = 255;
            }
        }
    }
    return image;
}

} // namespace geWaterPixelOracle

#endif /* GE007_WATER_PIXEL_ORACLE_H */
