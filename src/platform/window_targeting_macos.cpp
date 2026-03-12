#include "framewatch/platform/window_targeting.h"

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace framewatch {

namespace {

std::string ToLower(std::string_view input) {
    std::string lowered;
    lowered.reserve(input.size());
    for (const unsigned char ch : input) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lowered;
}

bool ContainsIgnoreCase(std::string_view text, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }

    const std::string lowered_text = ToLower(text);
    const std::string lowered_needle = ToLower(needle);
    return lowered_text.find(lowered_needle) != std::string::npos;
}

std::string CopyCfString(CFStringRef value) {
    if (value == nullptr) {
        return {};
    }

    const CFIndex length = CFStringGetLength(value);
    const CFIndex max_size =
        CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;

    std::string output(static_cast<std::size_t>(max_size), '\0');
    if (!CFStringGetCString(value,
                            output.data(),
                            max_size,
                            kCFStringEncodingUTF8)) {
        return {};
    }

    output.resize(std::strlen(output.c_str()));
    return output;
}

bool GetIntValue(CFDictionaryRef dictionary, const void* key, std::int64_t& value) {
    if (dictionary == nullptr) {
        return false;
    }

    CFNumberRef number = static_cast<CFNumberRef>(CFDictionaryGetValue(dictionary, key));
    if (number == nullptr) {
        return false;
    }

    return CFNumberGetValue(number, kCFNumberSInt64Type, &value) == true;
}

bool GetDoubleValue(CFDictionaryRef dictionary, const void* key, double& value) {
    if (dictionary == nullptr) {
        return false;
    }

    CFNumberRef number = static_cast<CFNumberRef>(CFDictionaryGetValue(dictionary, key));
    if (number == nullptr) {
        return false;
    }

    return CFNumberGetValue(number, kCFNumberDoubleType, &value) == true;
}

bool ShouldKeepWindow(const DesktopWindowInfo& window) {
    if (window.width < 140 || window.height < 80) {
        return false;
    }

    if (window.owner_name.empty() && window.title.empty()) {
        return false;
    }

    if (window.owner_name == "Window Server" || window.owner_name == "Dock") {
        return false;
    }

    return true;
}

}  // namespace

bool WindowTargetingSupported() noexcept {
    return true;
}

std::vector<DesktopWindowInfo> EnumerateDesktopWindows() {
    std::vector<DesktopWindowInfo> windows;

    CFArrayRef window_list = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
        kCGNullWindowID);
    if (window_list == nullptr) {
        return windows;
    }

    const CFIndex count = CFArrayGetCount(window_list);
    windows.reserve(static_cast<std::size_t>(count));

    for (CFIndex index = 0; index < count; ++index) {
        CFDictionaryRef entry =
            static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(window_list, index));
        if (entry == nullptr) {
            continue;
        }

        std::int64_t layer = 0;
        if (!GetIntValue(entry, kCGWindowLayer, layer) || layer != 0) {
            continue;
        }

        double alpha = 1.0;
        if (GetDoubleValue(entry, kCGWindowAlpha, alpha) && alpha <= 0.0) {
            continue;
        }

        CFDictionaryRef bounds_dictionary =
            static_cast<CFDictionaryRef>(CFDictionaryGetValue(entry, kCGWindowBounds));
        CGRect bounds{};
        if (bounds_dictionary == nullptr ||
            !CGRectMakeWithDictionaryRepresentation(bounds_dictionary, &bounds)) {
            continue;
        }

        std::int64_t window_id = 0;
        if (!GetIntValue(entry, kCGWindowNumber, window_id)) {
            continue;
        }

        DesktopWindowInfo window;
        window.id = static_cast<std::uint64_t>(window_id);
        window.owner_name = CopyCfString(
            static_cast<CFStringRef>(CFDictionaryGetValue(entry, kCGWindowOwnerName)));
        window.title = CopyCfString(
            static_cast<CFStringRef>(CFDictionaryGetValue(entry, kCGWindowName)));
        window.x = static_cast<int>(std::lround(bounds.origin.x));
        window.y = static_cast<int>(std::lround(bounds.origin.y));
        window.width = static_cast<int>(std::lround(bounds.size.width));
        window.height = static_cast<int>(std::lround(bounds.size.height));

        if (!ShouldKeepWindow(window)) {
            continue;
        }

        windows.push_back(std::move(window));
    }

    CFRelease(window_list);
    return windows;
}

std::optional<DesktopWindowInfo> FindDesktopWindowByTitle(std::string_view title_substring) {
    for (const DesktopWindowInfo& window : EnumerateDesktopWindows()) {
        if (ContainsIgnoreCase(window.title, title_substring) ||
            ContainsIgnoreCase(window.owner_name, title_substring)) {
            return window;
        }
    }

    return std::nullopt;
}

std::optional<DesktopWindowInfo> FindTopDesktopWindow(std::string_view exclude_title_substring) {
    for (const DesktopWindowInfo& window : EnumerateDesktopWindows()) {
        if (!exclude_title_substring.empty() &&
            (ContainsIgnoreCase(window.title, exclude_title_substring) ||
             ContainsIgnoreCase(window.owner_name, exclude_title_substring))) {
            continue;
        }

        return window;
    }

    return std::nullopt;
}

}  // namespace framewatch
