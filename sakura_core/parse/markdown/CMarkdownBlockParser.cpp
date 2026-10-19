/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "parse/markdown/CMarkdownBlockParser.h"
#include "parse/markdown/MarkdownParseUtil.h"
#include <algorithm>

using Markdown::Detail::IsSpace;
using Markdown::Detail::RunEnd;

Markdown::BlockInfo CMarkdownBlockParser::ScanBlock(std::wstring_view text, Markdown::BlockState& state)
{
	// Strip quote prefixes so that fenced examples inside blockquotes stay literal.
	size_t indent = text.find_first_not_of(L' ');
	while (indent != std::wstring_view::npos && indent <= 3 && text[indent] == L'>') {
		text.remove_prefix(indent + 1);
		if (!text.empty() && text.front() == L' ') text.remove_prefix(1);
		indent = text.find_first_not_of(L' ');
	}
	if (indent == std::wstring_view::npos) return {state.fence != 0, true};
	if (indent <= 3 && (text[indent] == L'`' || text[indent] == L'~')) {
		const size_t end = RunEnd(text, indent);
		const size_t length = end - indent;
		if (state.fence) {
			if (text[indent] == state.fence && length >= state.fenceLength
				&& text.substr(end).find_first_not_of(L" \t") == std::wstring_view::npos) state = {};
			return {true, true};
		}
		if (length >= 3 && (text[indent] != L'`' || text.substr(end).find(L'`') == std::wstring_view::npos)) {
			state = {text[indent], length};
			return {true, true};
		}
	}
	if (state.fence || indent >= 4 || text[indent] == L'\t') return {true, true};
	text.remove_prefix(indent);
	const bool heading = text.front() == L'#' || text.front() == L'=';
	const bool list = text.size() > 1 && (text[0] == L'-' || text[0] == L'+' || text[0] == L'*') && IsSpace(text[1]);
	const auto digits = text.find_first_not_of(L"0123456789");
	const bool ordered = digits > 0 && digits <= 9 && digits + 1 < text.size()
		&& (text[digits] == L'.' || text[digits] == L')') && IsSpace(text[digits + 1]);
	const bool rule = (text.front() == L'-' || text.front() == L'*' || text.front() == L'_')
		&& std::all_of(text.begin(), text.end(), [&](wchar_t c) { return c == text.front() || IsSpace(c); });
	return {false, heading || list || ordered || rule};
}

size_t CMarkdownBlockParser::IndentLength(std::wstring_view text)
{
	const auto pos = text.find_first_not_of(L" \t");
	return pos == std::wstring_view::npos ? text.size() : pos;
}

// Return the existing list marker's length, including an optional task checkbox.
size_t CMarkdownBlockParser::ListMarkerLength(std::wstring_view text, size_t start, bool& ordered, bool& task)
{
	ordered = task = false;
	size_t end = start;
	if (end < text.size() && (text[end] == L'-' || text[end] == L'+' || text[end] == L'*')) {
		++end;
	} else {
		while (end < text.size() && L'0' <= text[end] && text[end] <= L'9') ++end;
		if (end == start || end - start > 9 || end == text.size() || (text[end] != L'.' && text[end] != L')')) return 0;
		++end;
		ordered = true;
	}
	if (end == text.size() || (text[end] != L' ' && text[end] != L'\t')) return 0;
	while (end < text.size() && (text[end] == L' ' || text[end] == L'\t')) ++end;
	if (end + 3 <= text.size() && text[end] == L'[' && text[end + 2] == L']'
		&& (text[end + 1] == L' ' || text[end + 1] == L'x' || text[end + 1] == L'X')
		&& (end + 3 == text.size() || text[end + 3] == L' ' || text[end + 3] == L'\t')) {
		task = true;
		end += 3;
		if (end < text.size()) ++end;
	}
	return end - start;
}

size_t CMarkdownBlockParser::HeadingMarkerLength(std::wstring_view text, size_t indent)
{
	size_t length = 0;
	while (indent + length < text.size() && text[indent + length] == L'#') ++length;
	if (length > 0 && length <= 6 && indent + length < text.size() && text[indent + length] == L' ') ++length;
	else length = 0;
	return length;
}

bool CMarkdownBlockParser::IsBacktickFence(std::wstring_view firstLine, std::wstring_view lastLine)
{
	const size_t fenceLen = firstLine.find_first_not_of(L'`');
	const size_t count = fenceLen == std::wstring_view::npos ? firstLine.size() : fenceLen;
	return count >= 3 && lastLine.size() >= count && lastLine.find_first_not_of(L'`') == std::wstring_view::npos;
}
