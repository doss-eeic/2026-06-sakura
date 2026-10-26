/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "cmd/markdown/MarkdownCommands.h"
#include <algorithm>
#include <utility>

namespace Markdown::Commands {
size_t LongestRun(const std::wstring& text, wchar_t marker)
{
	size_t longest = 0, run = 0;
	for (wchar_t c : text) {
		run = c == marker ? run + 1 : 0;
		longest = std::max(longest, run);
	}
	return longest;
}

std::wstring JoinLines(const std::vector<MarkdownLine>& lines)
{
	std::wstring result;
	for (const auto& line : lines) result += line.text + line.eol;
	return result;
}

std::wstring ToggleFence(std::vector<MarkdownLine>& lines, const std::wstring& original,
	const std::wstring& eol, size_t& caretOffset)
{
	const auto& firstLine = lines.front().text;
	const auto& lastLine = lines.back().text;
	std::wstring result;
	if (lines.size() >= 3 && IsBacktickFence(firstLine, lastLine)) {
		lines.erase(lines.begin());
		lines.pop_back();
		lines.back().eol.clear();
		result = JoinLines(lines);
	} else {
		const std::wstring fence(std::max<size_t>(3, LongestRun(original, L'`') + 1), L'`');
		result = fence + eol + original + eol + fence;
		caretOffset = fence.size() + eol.size();
	}
	return result;
}

std::wstring MakeTable(const std::vector<MarkdownLine>& lines, const std::wstring& eol)
{
	std::vector<std::vector<std::wstring>> rows;
	size_t columns = 2;
	for (const auto& line : lines) {
		std::vector<std::wstring> cells(1);
		for (wchar_t c : line.text) {
			if (c == L'\t') cells.emplace_back();
			else {
				if (c == L'|' || c == L'\\') cells.back() += L'\\';
				cells.back() += c;
			}
		}
		columns = std::max(columns, cells.size());
		rows.push_back(std::move(cells));
	}
	if (rows.size() == 1) rows.emplace_back();
	std::wstring result;
	for (size_t row = 0; row < rows.size(); ++row) {
		rows[row].resize(columns);
		result += L"|";
		for (const auto& cell : rows[row]) result += L" " + cell + L" |";
		if (row + 1 < rows.size()) result += eol;
		if (row == 0) {
			result += L"|";
			for (size_t col = 0; col < columns; ++col) result += L" --- |";
			result += eol;
		}
	}
	return result;
}

std::wstring ToggleTaskChecks(std::vector<MarkdownLine>& lines)
{
	bool allChecked = true;
	for (const auto& line : lines) {
		bool ordered, task;
		const size_t indent = IndentLength(line.text);
		ListMarkerLength(line.text, indent, ordered, task);
		const size_t box = line.text.find(L'[', indent);
		allChecked &= task && box != std::wstring::npos && line.text[box + 1] != L' ';
	}
	for (auto& line : lines) {
		bool ordered, task;
		const size_t indent = IndentLength(line.text);
		const size_t length = ListMarkerLength(line.text, indent, ordered, task);
		if (task) line.text[line.text.find(L'[', indent) + 1] = allChecked ? L' ' : L'x';
		else line.text.replace(indent, length, L"- [x] ");
	}
	return JoinLines(lines);
}

std::wstring ToggleLinePrefixes(EFunctionCode command, std::vector<MarkdownLine>& lines,
	size_t& caretOffset)
{
	const bool heading = F_MARKDOWN_H1 <= command && command <= F_MARKDOWN_H6;
	const std::wstring prefix = heading ? std::wstring(command - F_MARKDOWN_H1 + 1, L'#') + L" "
		: command == F_MARKDOWN_QUOTE ? L"> " : command == F_MARKDOWN_TASK ? L"- [ ] " : L"- ";
	bool remove = true;
	for (const auto& line : lines) {
		const size_t indent = IndentLength(line.text);
		if (heading || command == F_MARKDOWN_QUOTE) remove &= line.text.compare(indent, prefix.size(), prefix) == 0;
		else {
			bool ordered, task;
			const size_t length = ListMarkerLength(line.text, indent, ordered, task);
			remove &= length > 0 && (command == F_MARKDOWN_TASK ? task : command == F_MARKDOWN_ORDERED ? ordered && !task : !ordered && !task);
		}
	}
	int number = 1;
	for (auto& line : lines) {
		const size_t indent = IndentLength(line.text);
		size_t length = 0;
		if (heading) {
			length = HeadingMarkerLength(line.text, indent);
		} else if (command == F_MARKDOWN_QUOTE) {
			if (remove) length = prefix.size();
		} else {
			bool ordered, task;
			length = ListMarkerLength(line.text, indent, ordered, task);
		}
		const std::wstring marker = remove ? L"" : command == F_MARKDOWN_ORDERED ? std::to_wstring(number++) + L". " : prefix;
		line.text.replace(indent, length, marker);
	}
	std::wstring result = JoinLines(lines);
	caretOffset = IndentLength(lines.front().text);
	if (!remove) caretOffset += command == F_MARKDOWN_ORDERED ? 3 : prefix.size();
	return result;
}

size_t IndentLength(std::wstring_view text)
{
	const auto pos = text.find_first_not_of(L" \t");
	return pos == std::wstring_view::npos ? text.size() : pos;
}

// Return the existing list marker's length, including an optional task checkbox.
size_t ListMarkerLength(std::wstring_view text, size_t start, bool& ordered, bool& task)
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

size_t HeadingMarkerLength(std::wstring_view text, size_t indent)
{
	size_t length = 0;
	while (indent + length < text.size() && text[indent + length] == L'#') ++length;
	if (length > 0 && length <= 6 && indent + length < text.size() && text[indent + length] == L' ') ++length;
	else length = 0;
	return length;
}

bool IsBacktickFence(std::wstring_view firstLine, std::wstring_view lastLine)
{
	const size_t fenceLen = firstLine.find_first_not_of(L'`');
	const size_t count = fenceLen == std::wstring_view::npos ? firstLine.size() : fenceLen;
	return count >= 3 && lastLine.size() >= count && lastLine.find_first_not_of(L'`') == std::wstring_view::npos;
}
}
