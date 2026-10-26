/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "parse/markdown/CMarkdownBlockParser.h"
#include "parse/markdown/MarkdownParseUtil.h"
#include <algorithm>
#include <limits>
#include <regex>
#include <string>

using Markdown::Detail::IsSpace;
using Markdown::Detail::RunEnd;

namespace {
// コードブロックの字下げ量。タブ幅（ParseLineの引数）とは別の構文規則。
constexpr size_t IndentedCodeColumns = 4;

bool IsIndentSpace(wchar_t c)
{
	return c == L' ' || c == L'\t';
}

bool IsBlank(std::wstring_view text)
{
	return text.find_first_not_of(L" \t") == std::wstring_view::npos;
}

bool IsThematicBreak(std::wstring_view text)
{
	if (text.empty() || (text.front() != L'-' && text.front() != L'*' && text.front() != L'_')) return false;
	size_t count = 0;
	for (wchar_t c : text) {
		if (c == text.front()) ++count;
		else if (!IsIndentSpace(c)) return false;
	}
	return count >= 3;
}

// ATX見出しの本文開始位置。見出しでなければ0。
size_t HeadingPrefixLength(std::wstring_view text)
{
	if (text.empty() || text.front() != L'#') return 0;
	const size_t end = RunEnd(text, 0);
	if (end > 6 || (end < text.size() && !IsIndentSpace(text[end]))) return 0;
	return end < text.size() ? end + 1 : end;
}

bool IsSetextUnderline(std::wstring_view text)
{
	if (text.empty() || (text.front() != L'=' && text.front() != L'-')) return false;
	// 「- 」は本文のないリスト項目として扱う。
	if (text.substr(0, 2) == L"- ") return false;
	return IsBlank(text.substr(RunEnd(text, 0)));
}

size_t OpeningFenceLength(std::wstring_view text)
{
	if (text.empty() || (text.front() != L'`' && text.front() != L'~')) return 0;
	const size_t length = RunEnd(text, 0);
	if (length < 3) return 0;
	if (text.front() == L'`' && text.substr(length).find(L'`') != std::wstring_view::npos) return 0;
	return length;
}
}

Markdown::LineContext CMarkdownBlockParser::ParseLine(
	std::wstring_view text,
	size_t lineIndex,
	size_t tabWidth,
	Markdown::ParseState& state
) {
	Markdown::LineContext result{};

	if (tabWidth == 0) {
		return result;
	}

	size_t offset = 0;
	size_t column = 0;

	const auto advance = [&]() {
		if (text[offset] == L'\t') {
			column += tabWidth - column % tabWidth;
		}
		else {
			++column;
		}
		++offset;
	};

	const auto readQuote = [&]() -> bool {
		if (text.substr(offset, 2) != L"> ") {
			return false;
		}

		const size_t prefixOffset = offset;

		advance();
		advance();

		Markdown::ContainerInfo quote{};
		quote.kind = Markdown::ContainerKind::Quote;
		quote.start = {lineIndex, prefixOffset};
		quote.prefixRange = Markdown::SourceRange{
			{lineIndex, prefixOffset},
			{lineIndex, offset}
		};
		quote.contentColumn = column;

		for (auto it = result.containers.rbegin(); it != result.containers.rend(); ++it) {
			if (it->kind == Markdown::ContainerKind::ListItem) {
				quote.parentItem = it->start;
				break;
			}
		}

		result.containers.push_back(quote);
		return true;
	};

	const auto readList = [&]() -> bool {
		static const std::wregex BULLET_MARKER(LR"(^[-+*] )");
		static const std::wregex TASK_MARKER(LR"(^[-+*] \[[x ]\])");
		static const std::wregex ENUM_MARKER(L"^[0-9]{1,"
			+ std::to_wstring(std::numeric_limits<unsigned int>::digits10 + 1) + L"}\\. ");
		const auto remaining = text.substr(offset);
		std::match_results<std::wstring_view::const_iterator> match;
		Markdown::ContainerInfo container{};
		if (std::regex_search(
			remaining.begin(), remaining.end(), match, BULLET_MARKER)) {
			container.kind = Markdown::ContainerKind::ListItem;
			container.list.emplace();
			container.list->kind = Markdown::ListKind::Bullet;
		}
		else if (std::regex_search(
			remaining.begin(), remaining.end(), match, ENUM_MARKER)) {
			container.kind = Markdown::ContainerKind::ListItem;
			container.list.emplace();
			container.list->kind = Markdown::ListKind::Ordered;
		}
		else {
			return false;
		}
		const size_t prefixOffset = offset;
		container.start = {lineIndex, prefixOffset};
		const size_t prefixEnd = prefixOffset
			+ static_cast<size_t>(match.length());

		// 一致範囲の末尾にある半角スペース1文字を除く。
		const size_t markerEnd = prefixEnd - 1;

		container.list->range = Markdown::SourceRange{
			{lineIndex, prefixOffset},
			{lineIndex, markerEnd}
		};
		container.list->marker = text[markerEnd - 1];

		if (container.list->kind == Markdown::ListKind::Ordered) {
			unsigned int number = 0;
			for (size_t i = prefixOffset; i < markerEnd - 1; ++i) {
				const auto digit = static_cast<unsigned int>(text[i] - L'0');
				if (number > ((std::numeric_limits<unsigned int>::max)() - digit) / 10) return false;
				number = number * 10 + digit;
			}
			container.list->number = number;
		}

		while (offset < prefixEnd) {
			advance();
		}
		container.contentColumn = column;

		if (std::regex_search(
			remaining.begin(), remaining.end(), match, TASK_MARKER)) {
			const size_t taskEnd = prefixOffset
				+ static_cast<size_t>(match.length());
			const auto tail = text.substr(taskEnd);

			// チェックボックスの後は空白・タブ・行末。
			if (tail.empty() || tail.front() == L' ' || tail.front() == L'\t') {
				container.list->task.emplace();
				container.list->task->range = Markdown::SourceRange{
					{lineIndex, offset},
					{lineIndex, taskEnd}
				};
				container.list->task->checked = text[offset + 1] == L'x';

				while (offset < taskEnd) {
					advance();
				}

				// 本文との区切りを1文字消費する。
				if (!tail.empty()) {
					advance();
				}
			}
		}

		container.prefixRange = Markdown::SourceRange{
			{lineIndex, prefixOffset},
			{lineIndex, offset}
		};

		for (auto it = result.containers.rbegin();
			it != result.containers.rend(); ++it) {
			if (it->kind == Markdown::ContainerKind::ListItem) {
				container.parentItem = it->start;
				break;
			}
		}

		result.containers.push_back(container);
		return true;
	};

	const auto skipIndent = [&]() {
		while (offset < text.size() && IsIndentSpace(text[offset])) advance();
	};
	const auto consumeIndentTo = [&](size_t targetColumn) {
		while (column < targetColumn && offset < text.size() && IsIndentSpace(text[offset])) {
			if (text[offset] == L'\t' && column + tabWidth - column % tabWidth > targetColumn) {
				// タブの一部だけが字下げに使われる場合、残りの列を次の解析へ渡す。
				// 原文範囲でタブを分割できないので、offsetはそのタブ上に残す。
				column = targetColumn;
				break;
			}
			advance();
		}
	};
	const auto contentColumn = [&]() -> size_t {
		return result.containers.empty() ? 0 : result.containers.back().contentColumn;
	};
	const auto finish = [&](Markdown::LineKind kind) {
		result.kind = kind;
		result.bodyRange = Markdown::SourceRange{{lineIndex, offset}, {lineIndex, text.size()}};
		result.bodyEmpty = IsBlank(text.substr(offset));
		state.containers = result.containers;
		state.paragraphOpen = !result.bodyEmpty && (kind == Markdown::LineKind::Paragraph
			|| kind == Markdown::LineKind::Continuation || kind == Markdown::LineKind::ListItem);
		state.previousLineBlank = kind == Markdown::LineKind::Blank
			|| (result.bodyEmpty && (kind == Markdown::LineKind::FencedCode || kind == Markdown::LineKind::IndentedCode));
		state.indentedCodeOpen = kind == Markdown::LineKind::IndentedCode;
		return result;
	};

	// 前行のコンテナを外側から照合する。継続項目のstartは変更しない。
	size_t matched = 0;
	for (; matched < state.containers.size(); ++matched) {
		const auto& previous = state.containers[matched];
		const size_t savedOffset = offset;
		const size_t savedColumn = column;
		const size_t baseColumn = contentColumn();
		if (previous.kind == Markdown::ContainerKind::Quote) {
			skipIndent();
			if (column - baseColumn >= IndentedCodeColumns || !readQuote()) {
				offset = savedOffset;
				column = savedColumn;
				break;
			}
			result.containers.back().start = previous.start;
		} else {
			const size_t previousBase = matched == 0 ? 0 : state.containers[matched - 1].contentColumn;
			const size_t targetColumn = baseColumn + previous.contentColumn - previousBase;
			consumeIndentTo(targetColumn);
			if (column < targetColumn && !IsBlank(text.substr(offset))) {
				offset = savedOffset;
				column = savedColumn;
				break;
			}
			auto continued = previous;
			continued.prefixRange.reset();
			// 空行でも、次行の照合に必要な内容開始列を維持する。
			continued.contentColumn = targetColumn;
			result.containers.push_back(continued);
		}
	}

	// 所属コンテナが閉じたフェンスは、外側の行には引き継がない。
	if (state.fence && matched < state.fence->containerDepth) state.fence.reset();
	if (state.fence) {
		const size_t savedOffset = offset;
		const size_t savedColumn = column;
		const size_t baseColumn = contentColumn();
		skipIndent();
		if (offset < text.size() && column >= baseColumn && column - baseColumn < IndentedCodeColumns
			&& text[offset] == state.fence->marker) {
			const size_t end = RunEnd(text, offset);
			if (end - offset >= state.fence->length && IsBlank(text.substr(end))) {
				state.fence.reset();
				return finish(Markdown::LineKind::Fence);
			}
		}
		offset = savedOffset;
		column = savedColumn;
		const size_t targetColumn = baseColumn + state.fence->indentColumns;
		consumeIndentTo(targetColumn);
		return finish(Markdown::LineKind::FencedCode);
	}

	// 記号を省略した行は、開いている段落の通常の本文なら継続できる。
	if (matched < state.containers.size() && state.paragraphOpen && !state.previousLineBlank) {
		const size_t savedOffset = offset;
		const size_t savedColumn = column;
		const size_t baseColumn = contentColumn();
		skipIndent();
		const auto rest = text.substr(offset);
		bool interrupts = false;
		if (!rest.empty() && column - baseColumn < IndentedCodeColumns) {
			interrupts = HeadingPrefixLength(rest) != 0 || IsThematicBreak(rest)
				|| IsSetextUnderline(rest) || OpeningFenceLength(rest) != 0;
			if (!interrupts) {
				interrupts = readQuote() || readList();
				result.containers.resize(matched);
			}
		}
		offset = savedOffset;
		column = savedColumn;
		if (!rest.empty() && !interrupts) {
			for (size_t i = matched; i < state.containers.size(); ++i) {
				const size_t previousBase = i == 0 ? 0 : state.containers[i - 1].contentColumn;
				auto continued = state.containers[i];
				continued.prefixRange.reset();
				continued.contentColumn = contentColumn() + continued.contentColumn - previousBase;
				result.containers.push_back(continued);
			}
			return finish(Markdown::LineKind::Continuation);
		}
	}

	const bool sameContainers = matched == state.containers.size();
	bool paragraphOpen = sameContainers && state.paragraphOpen;
	bool newItem = false;
	bool newContainer = false;
	for (;;) {
		const size_t savedOffset = offset;
		const size_t savedColumn = column;
		const size_t baseColumn = contentColumn();
		skipIndent();
		const auto rest = text.substr(offset);
		if (rest.empty()) {
			offset = savedOffset;
			column = savedColumn;
			if (sameContainers && !newContainer && state.indentedCodeOpen) return finish(Markdown::LineKind::IndentedCode);
			return finish(newItem ? Markdown::LineKind::ListItem : Markdown::LineKind::Blank);
		}
		// チェックボックスなど、既に消費した接頭辞の幅は本文の字下げに含めない。
		const size_t indentBase = std::max(baseColumn, savedColumn);
		const size_t indent = column - indentBase;
		if (indent >= IndentedCodeColumns) {
			offset = savedOffset;
			column = savedColumn;
			if (!paragraphOpen) {
				consumeIndentTo(indentBase + IndentedCodeColumns);
				return finish(Markdown::LineKind::IndentedCode);
			}
			// 字下げだけでは、開いている段落をコードブロックに変えない。
			break;
		}
		if (const size_t length = OpeningFenceLength(rest)) {
			state.fence = Markdown::FenceInfo{rest.front(), length, {lineIndex, offset}, result.containers.size(), indent};
			return finish(Markdown::LineKind::Fence);
		}
		if (paragraphOpen && IsSetextUnderline(rest)) {
			result.heading = Markdown::HeadingInfo{rest.front() == L'=' ? 1u : 2u,
				{{lineIndex, offset}, {lineIndex, offset + RunEnd(rest, 0)}}, std::nullopt};
			return finish(Markdown::LineKind::Heading);
		}
		if (IsThematicBreak(rest)) return finish(Markdown::LineKind::ThematicBreak);
		if (const size_t length = HeadingPrefixLength(rest)) {
			const size_t level = RunEnd(rest, 0);
			const auto markerRange = Markdown::SourceRange{{lineIndex, offset}, {lineIndex, offset + level}};
			const size_t end = offset + length;
			while (offset < end) advance();
			// アウトライン用の本文範囲。編集用のbodyRangeは変更しない。
			size_t titleBegin = offset;
			size_t titleEnd = text.size();
			while (titleBegin < titleEnd && IsIndentSpace(text[titleBegin])) ++titleBegin;
			while (titleEnd > titleBegin && IsIndentSpace(text[titleEnd - 1])) --titleEnd;
			size_t closing = titleEnd;
			while (closing > titleBegin && text[closing - 1] == L'#') --closing;
			if (closing < titleEnd && (closing == titleBegin || IsIndentSpace(text[closing - 1]))) {
				titleEnd = closing;
				while (titleEnd > titleBegin && IsIndentSpace(text[titleEnd - 1])) --titleEnd;
			}
			result.heading = Markdown::HeadingInfo{static_cast<unsigned int>(level), markerRange,
				Markdown::SourceRange{{lineIndex, titleBegin}, {lineIndex, titleEnd}}};
			return finish(Markdown::LineKind::Heading);
		}
		if (readQuote()) {
			newContainer = true;
			paragraphOpen = false;
			continue;
		}
		if (readList()) {
			newContainer = newItem = true;
			paragraphOpen = false;
			continue;
		}
		// 接頭辞ではなかった空白は本文に残す。
		offset = savedOffset;
		column = savedColumn;
		break;
	}
	return finish(newItem ? Markdown::LineKind::ListItem
		: (!newContainer && !result.containers.empty() ? Markdown::LineKind::Continuation : Markdown::LineKind::Paragraph));
}

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
