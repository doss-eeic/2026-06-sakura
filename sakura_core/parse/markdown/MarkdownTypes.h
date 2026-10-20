/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#pragma once

#include <cstddef>

namespace Markdown {
enum Style : unsigned char { Normal = 0, Bold = 1, Italic = 2, Strike = 4 };

struct BlockState {
	wchar_t fence = 0;
	size_t fenceLength = 0;
};

struct BlockInfo {
	bool code;
	bool boundary;
};
}
