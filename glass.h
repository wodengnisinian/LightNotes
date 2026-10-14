#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>

namespace glass {
struct Frame {
    int width = 0, height = 0;
    std::vector<uint32_t> pixels;
    uint64_t revision = 0;
};
// Acquire the real desktop behind an excluded note. Desktop pixels stay in RAM.
bool backdrop(RECT bounds, Frame &result);
void release();
long error();
std::vector<uint32_t> material(const Frame &source, int blurRadius, int transparency, COLORREF tint,
                               bool smooth);
} // namespace glass
