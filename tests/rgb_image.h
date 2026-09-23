#ifndef GE007_TEST_RGB_IMAGE_H
#define GE007_TEST_RGB_IMAGE_H

/* Dependency-free RGB image and P6 PPM I/O shared by render-test oracles. */

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace geTestImage
{

struct Image
{
    int                       width  = 0;
    int                       height = 0;
    std::vector<std::uint8_t> pixels;
};

inline bool valid(const Image &image)
{
    if (image.width <= 0 || image.height <= 0) return false;
    const auto width  = static_cast<std::size_t>(image.width);
    const auto height = static_cast<std::size_t>(image.height);
    return width <= std::numeric_limits<std::size_t>::max() / height / 3 &&
           image.pixels.size() == width * height * 3;
}

inline bool next_token(std::istream &input, std::string &token)
{
    token.clear();
    char ch = 0;
    while (input.get(ch))
    {
        if (std::isspace(static_cast<unsigned char>(ch))) continue;
        if (ch == '#')
        {
            input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }
        token.push_back(ch);
        break;
    }
    while (input.get(ch) && !std::isspace(static_cast<unsigned char>(ch)))
    {
        token.push_back(ch);
    }
    return !token.empty();
}

inline bool load_ppm(const std::string &path, Image &image)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::string magic, width_token, height_token, max_value;
    if (!next_token(input, magic) || magic != "P6" ||
        !next_token(input, width_token) || !next_token(input, height_token) ||
        !next_token(input, max_value) || max_value != "255")
    {
        return false;
    }

    try
    {
        const int width  = std::stoi(width_token);
        const int height = std::stoi(height_token);
        if (width <= 0 || height <= 0) return false;
        const auto w = static_cast<std::size_t>(width);
        const auto h = static_cast<std::size_t>(height);
        if (w > std::numeric_limits<std::size_t>::max() / h / 3) return false;
        Image loaded;
        loaded.width  = width;
        loaded.height = height;
        loaded.pixels.resize(w * h * 3);
        input.read(reinterpret_cast<char *>(loaded.pixels.data()),
                   static_cast<std::streamsize>(loaded.pixels.size()));
        if (input.gcount() != static_cast<std::streamsize>(loaded.pixels.size()))
        {
            return false;
        }
        image = std::move(loaded);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

inline bool write_ppm(const std::string &path, const Image &image)
{
    if (!valid(image)) return false;
    std::ofstream output(path, std::ios::binary);
    if (!output) return false;
    output << "P6\n"
           << image.width << ' ' << image.height << "\n255\n";
    output.write(reinterpret_cast<const char *>(image.pixels.data()),
                 static_cast<std::streamsize>(image.pixels.size()));
    return output.good();
}

inline float luma(const Image &image, int x, int y)
{
    if (!valid(image) || x < 0 || y < 0 || x >= image.width || y >= image.height)
        return 0.0f;
    const std::size_t offset =
        (static_cast<std::size_t>(y) * image.width + x) * 3;
    return (0.2126f * image.pixels[offset] +
            0.7152f * image.pixels[offset + 1] +
            0.0722f * image.pixels[offset + 2]) /
           255.0f;
}

} // namespace geTestImage

#endif /* GE007_TEST_RGB_IMAGE_H */
