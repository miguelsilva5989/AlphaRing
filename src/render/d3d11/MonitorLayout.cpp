#include "MonitorLayout.h"

#include <algorithm>
#include <cmath>

namespace AlphaRing::Render::D3d11 {
    RectI FitOutputToAspect(const RectI& output, const RectI& aspect_source) {
        RectI content = output;
        if (aspect_source.w <= 0 || aspect_source.h <= 0 || content.w <= 0 || content.h <= 0)
            return content;

        const double target_aspect = static_cast<double>(aspect_source.w) / aspect_source.h;
        const double output_aspect = static_cast<double>(content.w) / content.h;
        if (output_aspect < target_aspect) {
            const int fitted_height = std::max(1, static_cast<int>(std::lround(content.w / target_aspect)));
            content.y += (content.h - fitted_height) / 2;
            content.h = fitted_height;
        } else if (output_aspect > target_aspect) {
            const int fitted_width = std::max(1, static_cast<int>(std::lround(content.h * target_aspect)));
            content.x += (content.w - fitted_width) / 2;
            content.w = fitted_width;
        }
        return content;
    }

    SourceCrop CalculateSourceCrop(
            std::uint32_t source_width,
            std::uint32_t source_height,
            int player,
            const RectI& output
    ) {
        const std::uint32_t player_height = source_height / 2;
        SourceCrop crop {0, player == 0 ? 0u : player_height, source_width, player_height};

        if (source_width == 0 || player_height == 0 || output.w <= 0 || output.h <= 0)
            return crop;

        const double source_aspect = static_cast<double>(source_width) / player_height;
        const double output_aspect = static_cast<double>(output.w) / output.h;
        if (std::abs(source_aspect - output_aspect) <= source_aspect * 0.001)
            return crop;

        if (source_aspect > output_aspect) {
            crop.width = std::clamp(
                    static_cast<std::uint32_t>(std::lround(player_height * output_aspect)),
                    1u,
                    source_width
            );
            crop.left = (source_width - crop.width) / 2;
        } else {
            crop.height = std::clamp(
                    static_cast<std::uint32_t>(std::lround(source_width / output_aspect)),
                    1u,
                    player_height
            );
            crop.top += (player_height - crop.height) / 2;
        }
        return crop;
    }
}
