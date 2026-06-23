#pragma once
#include "types.h"

#include <optional>
#include <string>
#include <string_view>

#include "framewatch/overlay/overlay_settings.h"

namespace dw {

std::string ToLower(std::string_view input);
bool ContainsIgnoreCase(std::string_view text, std::string_view needle);
std::string SanitizeUiText(std::string_view text, std::size_t max_chars = 30);
std::string TrimWhitespace(std::string_view text);

std::string TargetLabel(const framewatch::DesktopWindowInfo& window);
std::string TargetQueryForPersistence(const framewatch::DesktopWindowInfo& window);
bool IsSelfWindow(const framewatch::DesktopWindowInfo& window, std::string_view marker);

void RefreshTargets(TargetingState& targeting, std::string_view self_marker);
void PickFrontmostTarget(TargetingState& targeting, std::string_view self_marker);
void CycleTarget(TargetingState& targeting, int direction);
std::optional<framewatch::DesktopWindowInfo> CurrentTarget(const TargetingState& targeting);
void DockWindowToTarget(SDL_Window* window,
                        const framewatch::DesktopWindowInfo& target,
                        const framewatch::OverlaySettings& settings);

}  // namespace dw
