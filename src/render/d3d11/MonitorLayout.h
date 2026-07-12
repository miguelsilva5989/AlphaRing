#pragma once

#include <cstdint>

namespace AlphaRing::Render::D3d11 {
    struct RectI {
        int x;
        int y;
        int w;
        int h;
    };

    struct SourceCrop {
        std::uint32_t left;
        std::uint32_t top;
        std::uint32_t width;
        std::uint32_t height;
    };

    RectI FitOutputToAspect(const RectI& output, const RectI& aspect_source);
    SourceCrop CalculateSourceCrop(
            std::uint32_t source_width,
            std::uint32_t source_height,
            int player,
            const RectI& output
    );
}
