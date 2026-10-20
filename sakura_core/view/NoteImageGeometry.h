/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#pragma once
#include <algorithm>
#include <cmath>

namespace NoteView {
struct ImageGeometry {
	int rows;
	unsigned width;
	unsigned height;
};

// Pure per-pane calculation; source dimensions, not cached thumbnail dimensions, determine rows.
inline ImageGeometry FitImage(unsigned sourceWidth, unsigned sourceHeight, int width, int lineHeight,
	int maxRows, int dpi = 96)
{
	maxRows = std::clamp(maxRows, 3, 40);
	if (!sourceWidth || !sourceHeight || width <= 0 || lineHeight <= 0) return {maxRows, 0, 0};
	const int cap = std::max(1, static_cast<int>(640LL * std::clamp(dpi, 1, 960) / 96));
	const int padding = std::min(std::max(1, 8 * std::clamp(dpi, 1, 960) / 96), maxRows * lineHeight - 1);
	const double scale = std::min({static_cast<double>(std::min(width, cap)) / sourceWidth,
		static_cast<double>(std::min(maxRows * lineHeight - padding, cap)) / sourceHeight});
	const unsigned fittedWidth = std::max(1u, static_cast<unsigned>(std::floor(sourceWidth * scale)));
	const unsigned fittedHeight = std::max(1u, static_cast<unsigned>(std::floor(sourceHeight * scale)));
	return {std::clamp(static_cast<int>(std::ceil((fittedHeight + padding) / static_cast<double>(lineHeight))), 3, maxRows),
		fittedWidth, fittedHeight};
}
}
