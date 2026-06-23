#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace dw {

using Glyph = std::array<std::uint8_t, 7>;

Glyph GlyphFor(char ch);
int TextWidth(std::string_view text, int scale);

}  // namespace dw
