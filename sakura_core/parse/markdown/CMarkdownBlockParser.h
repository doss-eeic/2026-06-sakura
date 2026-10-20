/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#pragma once

#include "parse/markdown/MarkdownTypes.h"
#include <string_view>

class CMarkdownBlockParser {
public:
	static Markdown::BlockInfo ScanBlock(std::wstring_view text, Markdown::BlockState& state);

	// Preserve the existing edit commands' marker recognition, which differs
	// from the display scanner's block-boundary rules.
	static size_t IndentLength(std::wstring_view text);
	static size_t ListMarkerLength(std::wstring_view text, size_t start, bool& ordered, bool& task);
	static size_t HeadingMarkerLength(std::wstring_view text, size_t indent);
	static bool IsBacktickFence(std::wstring_view firstLine, std::wstring_view lastLine);
};
