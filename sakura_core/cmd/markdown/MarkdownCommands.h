/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#pragma once

#include "Funccode_enum.h"
#include "parse/markdown/CMarkdownInlineParser.h"
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Markdown::Commands {
struct MarkdownLine {
	std::wstring text;
	std::wstring eol;
};

struct EmphasisEdit {
	size_t from;
	size_t to;
	std::wstring text;
	// 置換文字列内で選択し直す範囲。終了位置は含まない。
	size_t selectionFrom;
	size_t selectionTo;
};

std::optional<EmphasisEdit> ToggleEmphasis(std::wstring_view source,
	const std::vector<CMarkdownInlineParser::EmphasisSpan>& spans,
	size_t from, size_t to, Markdown::Style requested);

size_t LongestRun(const std::wstring& text, wchar_t marker);
std::wstring JoinLines(const std::vector<MarkdownLine>& lines);
std::wstring ToggleFence(std::vector<MarkdownLine>& lines, const std::wstring& original,
	const std::wstring& eol, size_t& caretOffset);
std::wstring MakeTable(const std::vector<MarkdownLine>& lines, const std::wstring& eol);
std::wstring ToggleTaskChecks(std::vector<MarkdownLine>& lines);
std::wstring ToggleLinePrefixes(EFunctionCode command, std::vector<MarkdownLine>& lines,
	size_t& caretOffset);

// Preserve the existing edit commands' marker recognition, which differs
// from the display scanner's block-boundary rules.
size_t IndentLength(std::wstring_view text);
size_t ListMarkerLength(std::wstring_view text, size_t start, bool& ordered, bool& task);
size_t HeadingMarkerLength(std::wstring_view text, size_t indent);
bool IsBacktickFence(std::wstring_view firstLine, std::wstring_view lastLine);
}
