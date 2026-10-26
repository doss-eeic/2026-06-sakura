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
	struct EmphasisSpan {
		size_t openingBegin;
		size_t contentBegin;
		size_t contentEnd;
		size_t closingEnd;
		Markdown::Style style;
	};

	static std::vector<unsigned char> Parse(
		std::wstring_view text,
		std::vector<EmphasisSpan>* spans = nullptr);
};
