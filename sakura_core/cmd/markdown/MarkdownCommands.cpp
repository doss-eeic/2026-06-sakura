/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "cmd/markdown/MarkdownCommands.h"
#include "parse/markdown/MarkdownParseUtil.h"
#include <algorithm>
#include <array>
#include <utility>

namespace Markdown::Commands {
namespace {
constexpr std::array<Style, 3> emphasisStyles = {Bold, Italic, Strike};

struct EmphasisText {
	std::wstring text;
	std::vector<unsigned char> styles;
	std::vector<size_t> sourcePositions;
};

// 成立した装飾記号だけを除く。エスケープや未成立の記号は原文のまま保持する。
EmphasisText ReadEmphasisText(std::wstring_view source,
	const std::vector<CMarkdownInlineParser::EmphasisSpan>& spans)
{
	std::vector<bool> syntax(source.size(), false);
	std::array<std::vector<int>, 3> changes;
	for (auto& change : changes) change.resize(source.size() + 1);
	for (const auto& span : spans) {
		std::fill(syntax.begin() + span.openingBegin, syntax.begin() + span.contentBegin, true);
		std::fill(syntax.begin() + span.contentEnd, syntax.begin() + span.closingEnd, true);
		for (size_t style = 0; style < emphasisStyles.size(); ++style) {
			if (span.style != emphasisStyles[style]) continue;
			++changes[style][span.contentBegin];
			--changes[style][span.contentEnd];
		}
	}

	EmphasisText result;
	std::array<int, 3> depth{};
	for (size_t pos = 0; pos < source.size(); ++pos) {
		unsigned char mask = Normal;
		for (size_t style = 0; style < emphasisStyles.size(); ++style) {
			depth[style] += changes[style][pos];
			if (depth[style]) mask |= emphasisStyles[style];
		}
		if (syntax[pos]) continue;
		result.text += source[pos];
		result.styles.push_back(mask);
		result.sourcePositions.push_back(pos);
	}
	return result;
}

size_t TextPosition(const EmphasisText& text, size_t sourcePosition)
{
	return static_cast<size_t>(std::lower_bound(text.sourcePositions.begin(),
		text.sourcePositions.end(), sourcePosition) - text.sourcePositions.begin());
}
}

std::optional<EmphasisEdit> ToggleEmphasis(std::wstring_view source,
	const std::vector<CMarkdownInlineParser::EmphasisSpan>& spans,
	size_t from, size_t to, Markdown::Style requested)
{
	if (from >= to || to > source.size()
		|| (requested != Bold && requested != Italic && requested != Strike)) return std::nullopt;

	auto text = ReadEmphasisText(source, spans);
	const size_t selectedFrom = TextPosition(text, from);
	const size_t selectedTo = TextPosition(text, to);
	if (selectedFrom == selectedTo) return std::nullopt;

	// 全部に付いていれば解除、混在していれば選択全体へ付与する。
	bool remove = true;
	bool hasText = false;
	for (size_t pos = selectedFrom; pos < selectedTo; ++pos) {
		if (Detail::IsSpace(text.text[pos])) continue;
		hasText = true;
		if (!(text.styles[pos] & requested)) remove = false;
	}
	if (!hasText) return std::nullopt;
	for (size_t pos = selectedFrom; pos < selectedTo; ++pos) {
		if (remove) text.styles[pos] &= static_cast<unsigned char>(~requested);
		else text.styles[pos] |= requested;
	}

	// 選択境界と交差する装飾も、記号を配置し直せる範囲まで含める。
	size_t editFrom = from, editTo = to;
	bool expanded;
	do {
		expanded = false;
		for (const auto& span : spans) {
			if (span.openingBegin >= editTo || span.closingEnd <= editFrom) continue;
			const size_t first = std::min(editFrom, span.openingBegin);
			const size_t last = std::max(editTo, span.closingEnd);
			if (first != editFrom || last != editTo) {
				editFrom = first;
				editTo = last;
				expanded = true;
			}
		}
	} while (expanded);
	const size_t first = TextPosition(text, editFrom);
	const size_t last = TextPosition(text, editTo);

	// Markdownの開き記号直後・閉じ記号直前には空白を置けない。
	// 分割で端に残った空白は記号の外に置き、本文内の空白は維持する。
	for (const auto style : emphasisStyles) {
		for (size_t pos = first; pos < last;) {
			if (!(text.styles[pos] & style)) { ++pos; continue; }
			const size_t start = pos;
			while (pos < last && (text.styles[pos] & style)) ++pos;
			size_t begin = start, end = pos;
			while (begin < end && Detail::IsSpace(text.text[begin])) {
				text.styles[begin++] &= static_cast<unsigned char>(~style);
			}
			while (end > begin && Detail::IsSpace(text.text[end - 1])) {
				text.styles[--end] &= static_cast<unsigned char>(~style);
			}
		}
	}

	// 各装飾がどこまで続くか。長く続くものを外側に置いて交差を解消する。
	std::vector<std::array<size_t, 3>> ends(last - first + 1);
	for (size_t pos = last; pos > first;) {
		--pos;
		for (size_t style = 0; style < emphasisStyles.size(); ++style) {
			if (text.styles[pos] & emphasisStyles[style]) {
				ends[pos - first][style] = std::max(pos + 1, ends[pos - first + 1][style]);
			}
		}
	}

	// 連続した記号や単語内の区切りで解釈が変わるため、生成後にも解析する。
	// 通常は * を使い、それで成立しない場合に _ の組み合わせを試す。
	for (unsigned int spelling = 0; spelling < 4; ++spelling) {
		const std::array<std::wstring_view, 3> markers = {
			spelling & 1 ? L"__" : L"**",
			spelling & 2 ? L"_" : L"*",
			L"~~"
		};
		EmphasisEdit edit{editFrom, editTo, {}, 0, 0};
		std::vector<size_t> active;
		for (size_t pos = first; pos <= last; ++pos) {
			std::vector<size_t> wanted;
			if (pos < last) {
				for (size_t style = 0; style < emphasisStyles.size(); ++style) {
					if (text.styles[pos] & emphasisStyles[style]) wanted.push_back(style);
				}
				std::stable_sort(wanted.begin(), wanted.end(), [&](size_t a, size_t b) {
					if (ends[pos - first][a] != ends[pos - first][b]) {
						return ends[pos - first][a] > ends[pos - first][b];
					}
					// 終点が同じなら、既に開いている装飾の順序を維持する。
					return std::find(active.begin(), active.end(), a)
						< std::find(active.begin(), active.end(), b);
				});
			}
			size_t common = 0;
			while (common < active.size() && common < wanted.size()
				&& active[common] == wanted[common]) ++common;
			for (size_t index = active.size(); index > common; --index) edit.text += markers[active[index - 1]];
			for (size_t index = common; index < wanted.size(); ++index) edit.text += markers[wanted[index]];
			active = std::move(wanted);
			if (pos == last) break;
			if (pos == selectedFrom) edit.selectionFrom = edit.text.size();
			edit.text += text.text[pos];
			if (pos + 1 == selectedTo) edit.selectionTo = edit.text.size();
		}

		std::wstring candidate(source.substr(0, editFrom));
		candidate += edit.text;
		candidate += source.substr(editTo);
		std::vector<CMarkdownInlineParser::EmphasisSpan> candidateSpans;
		CMarkdownInlineParser::Parse(candidate, &candidateSpans);
		const auto actual = ReadEmphasisText(candidate, candidateSpans);
		if (actual.text == text.text && actual.styles == text.styles) return edit;
	}
	return std::nullopt;
}

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
