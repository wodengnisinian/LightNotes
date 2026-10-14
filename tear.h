#pragma once
#include <windows.h>
#include <memory>
#include <vector>
#include <cstdint>
namespace tear {
struct Animation {
    int width{}, height{}, canvas{}, left{}, top{}, seam{};
    double horizontalVelocity = 140;
    uint64_t started{};
    std::vector<uint32_t> paper;
    HDC dc{};
    HBITMAP bitmap{};
    HGDIOBJ previous{};
    uint32_t *pixels{};
    ~Animation();
};
std::unique_ptr<Animation> capture(HWND window, double horizontalVelocity);
// Return false when the paper has fallen away. No data deletion happens here.
bool step(HWND window, Animation &state);
} // namespace tear
