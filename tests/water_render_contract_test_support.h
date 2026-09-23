#ifndef GE007_WATER_RENDER_CONTRACT_TEST_SUPPORT_H
#define GE007_WATER_RENDER_CONTRACT_TEST_SUPPORT_H

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>

namespace geWaterContractTest {

namespace fs = std::filesystem;

struct TestState {
    int failures = 0;

    void expect(bool condition, std::string_view message)
    {
        if (!condition) {
            ++failures;
            std::cerr << "RED: " << message << '\n';
        }
    }
};

inline std::string read_text(const fs::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return {std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
}

inline bool contains(std::string_view text, std::string_view needle)
{
    return text.find(needle) != std::string_view::npos;
}

inline bool contains_any(std::string_view text,
                         std::initializer_list<std::string_view> needles)
{
    for (const std::string_view needle : needles) {
        if (contains(text, needle)) {
            return true;
        }
    }
    return false;
}

inline std::size_t count_occurrences(std::string_view text,
                                     std::string_view needle)
{
    if (needle.empty()) {
        return 0;
    }

    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = text.find(needle, offset)) != std::string_view::npos) {
        ++count;
        offset += needle.size();
    }
    return count;
}

inline std::string_view between(std::string_view text,
                                std::string_view begin,
                                std::string_view end)
{
    const std::size_t begin_pos = text.find(begin);
    if (begin_pos == std::string_view::npos) {
        return {};
    }
    const std::size_t content_pos = begin_pos + begin.size();
    const std::size_t end_pos = text.find(end, content_pos);
    if (end_pos == std::string_view::npos) {
        return text.substr(content_pos);
    }
    return text.substr(content_pos, end_pos - content_pos);
}

} // namespace geWaterContractTest

#endif /* GE007_WATER_RENDER_CONTRACT_TEST_SUPPORT_H */
