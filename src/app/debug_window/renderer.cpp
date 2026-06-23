#include "renderer.h"
#include "bitmap_font.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace dw {

void SetDrawColor(SDL_Renderer* renderer, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

void FillRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color) {
    SetDrawColor(renderer, color);
    SDL_RenderFillRect(renderer, &rect);
}

void DrawRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color) {
    SetDrawColor(renderer, color);
    SDL_RenderDrawRect(renderer, &rect);
}

void DrawGlyph(SDL_Renderer* renderer, int x, int y, int scale, SDL_Color color, char ch) {
    const Glyph glyph = GlyphFor(ch);
    SetDrawColor(renderer, color);

    for (int row = 0; row < static_cast<int>(glyph.size()); ++row) {
        for (int col = 0; col < 5; ++col) {
            const bool filled = (glyph[static_cast<std::size_t>(row)] & (1 << (4 - col))) != 0;
            if (!filled) continue;
            const SDL_Rect pixel_rect{x + (col * scale), y + (row * scale), scale, scale};
            SDL_RenderFillRect(renderer, &pixel_rect);
        }
    }
}

void DrawText(SDL_Renderer* renderer, int x, int y, int scale, SDL_Color color, std::string_view text) {
    int cursor_x = x;
    int cursor_y = y;
    for (const char ch : text) {
        if (ch == '\n') {
            cursor_x = x;
            cursor_y += 8 * scale;
            continue;
        }
        DrawGlyph(renderer, cursor_x, cursor_y, scale, color, ch);
        cursor_x += 6 * scale;
    }
}

void DrawGradientBackground(SDL_Renderer* renderer, int width, int height, SDL_Color top, SDL_Color bottom) {
    for (int y = 0; y < height; ++y) {
        const float t = height > 1 ? static_cast<float>(y) / static_cast<float>(height - 1) : 0.0f;
        SDL_Color blended{
            static_cast<std::uint8_t>(top.r + ((bottom.r - top.r) * t)),
            static_cast<std::uint8_t>(top.g + ((bottom.g - top.g) * t)),
            static_cast<std::uint8_t>(top.b + ((bottom.b - top.b) * t)),
            255,
        };
        SetDrawColor(renderer, blended);
        SDL_RenderDrawLine(renderer, 0, y, width, y);
    }
}

std::string FormatDouble(double value, int precision) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

std::uint8_t ScaleAlpha(std::uint8_t alpha, double opacity) {
    return static_cast<std::uint8_t>(
        std::clamp(std::lround(static_cast<double>(alpha) * opacity), 28L, 255L));
}

Palette ApplyOverlaySettings(const Palette& base, const framewatch::OverlaySettings& settings) {
    Palette adjusted = base;
    adjusted.panel.a = ScaleAlpha(base.panel.a, settings.panel_opacity);
    adjusted.panel_border.a = ScaleAlpha(base.panel_border.a, settings.panel_opacity);
    return adjusted;
}

bool PointInRect(int x, int y, const SDL_Rect& rect) {
    return x >= rect.x && y >= rect.y && x < (rect.x + rect.w) && y < (rect.y + rect.h);
}

// On HiDPI displays SDL renders in physical pixels while pointer events are
// reported in logical window points. Scale mouse coords into renderer pixel space.
void ScaleMouseToRender(SDL_Window* window, SDL_Renderer* renderer, int& x, int& y) {
    int window_w = 0, window_h = 0, output_w = 0, output_h = 0;
    SDL_GetWindowSize(window, &window_w, &window_h);
    SDL_GetRendererOutputSize(renderer, &output_w, &output_h);
    if (window_w > 0 && window_h > 0) {
        x = static_cast<int>(std::lround(static_cast<double>(x) * output_w / window_w));
        y = static_cast<int>(std::lround(static_cast<double>(y) * output_h / window_h));
    }
}

}  // namespace dw
