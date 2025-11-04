#include "glyph_utils.h"

#include <algorithm>

namespace why {
namespace animations {

std::vector<std::string> parse_glyphs(const std::string& source) {
    std::vector<std::string> glyphs;
    glyphs.reserve(source.size());

    for (std::size_t i = 0; i < source.size();) {
        unsigned char lead = static_cast<unsigned char>(source[i]);
        std::size_t length = 1;
        if ((lead & 0x80u) == 0x00u) {
            length = 1;
        } else if ((lead & 0xE0u) == 0xC0u && i + 1 < source.size()) {
            length = 2;
        } else if ((lead & 0xF0u) == 0xE0u && i + 2 < source.size()) {
            length = 3;
        } else if ((lead & 0xF8u) == 0xF0u && i + 3 < source.size()) {
            length = 4;
        } else {
            ++i;
            continue;
        }

        glyphs.emplace_back(source.substr(i, length));
        i += length;
    }

    glyphs.erase(std::remove_if(glyphs.begin(), glyphs.end(), [](const std::string& g) {
                       return g.empty();
                   }),
                 glyphs.end());

    return glyphs;
}

} // namespace animations
} // namespace why

