#pragma once
#include "types.h"

#include <cstdint>
#include <string>
#include <string_view>

#include "framewatch/overlay/overlay_settings.h"

namespace dw {

void SetDrawColor(SDL_Renderer* renderer, SDL_Color color);
void FillRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color);
void DrawRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color);
void DrawGlyph(SDL_Renderer* renderer, int x, int y, int scale, SDL_Color color, char ch);
void DrawText(SDL_Renderer* renderer, int x, int y, int scale, SDL_Color color, std::string_view text);
void DrawGradientBackground(SDL_Renderer* renderer, int width, int height, SDL_Color top, SDL_Color bottom);
std::string FormatDouble(double value, int precision);
std::uint8_t ScaleAlpha(std::uint8_t alpha, double opacity);
Palette ApplyOverlaySettings(const Palette& base, const framewatch::OverlaySettings& settings);
bool PointInRect(int x, int y, const SDL_Rect& rect);
void ScaleMouseToRender(SDL_Window* window, SDL_Renderer* renderer, int& x, int& y);

}  // namespace dw
