/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#pragma once
#include <Windows.h>
#include <optional>
#include <string>
#include <vector>

namespace ImageDecoder {
struct Image {
	UINT width;
	UINT height;
	std::vector<BYTE> pixels;
	UINT sourceWidth = 0;
	UINT sourceHeight = 0;
};
bool IsAllowedSize(UINT width, UINT height);
// Caller initializes COM. Output is opaque BGRA, with transparency composited on white.
std::optional<Image> Decode(const std::wstring& path, UINT maxWidth, UINT maxHeight, bool allowUpscale = false);
}
