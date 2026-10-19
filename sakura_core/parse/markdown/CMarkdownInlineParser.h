/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#pragma once

#include "parse/markdown/MarkdownTypes.h"
#include <string_view>
#include <vector>

class CMarkdownInlineParser {
public:
	static std::vector<unsigned char> Parse(std::wstring_view text);
};
