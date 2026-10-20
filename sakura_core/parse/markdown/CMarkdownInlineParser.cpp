/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "parse/markdown/CMarkdownInlineParser.h"
#include "parse/markdown/MarkdownParseUtil.h"
#include <algorithm>
#include <array>

using namespace Markdown;
using Markdown::Detail::IsSpace;
using Markdown::Detail::RunEnd;

namespace {
bool IsPunctuation(wchar_t c)
{
	if (c < 0x80) return (L'!' <= c && c <= L'/') || (L':' <= c && c <= L'@')
		|| (L'[' <= c && c <= L'`') || (L'{' <= c && c <= L'~');
	// Include Unicode punctuation (not just the process's C locale).
	WORD category = 0;
	::GetStringTypeW(CT_CTYPE1, &c, 1, &category);
	return (category & C1_PUNCT) != 0;
}
}

std::vector<unsigned char> CMarkdownInlineParser::Parse(std::wstring_view text)
{
	struct Delimiter {
		size_t start, length, remaining;
		wchar_t marker;
		bool open, close;
	};
	std::vector<Delimiter> delimiters;
	std::vector<bool> literal(text.size(), false);
	const auto markLiteral = [&](size_t from, size_t to) {
		std::fill(literal.begin() + from, literal.begin() + to, true);
	};
	for (size_t pos = 0; pos < text.size();) {
		const wchar_t c = text[pos];
		if (c == L'\\' && pos + 1 < text.size() && IsPunctuation(text[pos + 1])) {
			pos += 2;
			continue;
		}
		if (c == L'`') {
			const size_t end = RunEnd(text, pos);
			size_t close = end;
			while ((close = text.find(L'`', close)) != std::wstring_view::npos) {
				const size_t closeEnd = RunEnd(text, close);
				if (closeEnd - close == end - pos) {
					markLiteral(pos, closeEnd);
					pos = closeEnd;
					break;
				}
				close = closeEnd;
			}
			if (close == std::wstring_view::npos) pos = end;
			continue;
		}
		// Autolinks and HTML tags are literal. Link labels remain eligible for emphasis.
		const wchar_t next = pos + 1 < text.size() ? text[pos + 1] : 0;
		if (c == L'<' && ((L'a' <= next && next <= L'z') || (L'A' <= next && next <= L'Z')
			|| next == L'/' || next == L'!' || next == L'?')) {
			const auto end = text.find(L'>', pos + 1);
			if (end != std::wstring_view::npos && text.substr(pos, end - pos).find(L'\n') == std::wstring_view::npos) {
				markLiteral(pos, end + 1);
				pos = end + 1;
				continue;
			}
		}
		if (c == L']' && pos + 1 < text.size() && text[pos + 1] == L'(') {
			size_t end = pos + 2;
			int depth = 1;
			for (; end < text.size() && depth; ++end) {
				if (text[end] == L'\\' && end + 1 < text.size()) ++end;
				else if (text[end] == L'(') ++depth;
				else if (text[end] == L')') --depth;
			}
			if (depth == 0) {
				markLiteral(pos + 1, end);
				pos = end;
				continue;
			}
		}
		if (c != L'*' && c != L'_' && c != L'~') { ++pos; continue; }
		const size_t end = RunEnd(text, pos);
		// Only paired tildes are strikethrough; single tildes and fence runs stay literal.
		if (c == L'~' && end - pos != 2) { pos = end; continue; }
		const wchar_t before = pos ? text[pos - 1] : 0;
		const wchar_t after = end < text.size() ? text[end] : 0;
		const bool left = !IsSpace(after) && (!IsPunctuation(after) || IsSpace(before) || IsPunctuation(before));
		const bool right = !IsSpace(before) && (!IsPunctuation(before) || IsSpace(after) || IsPunctuation(after));
		delimiters.push_back({pos, end - pos, end - pos, c,
			left && (c != L'_' || !right || IsPunctuation(before)),
			right && (c != L'_' || !left || IsPunctuation(after))});
		pos = end;
	}

	std::array<std::vector<size_t>, 3> openers;
	std::vector<int> bold(text.size() + 1), italic(text.size() + 1), strike(text.size() + 1);
	std::vector<bool> syntax(text.size(), false);
	for (size_t index = 0; index < delimiters.size(); ++index) {
		auto& closer = delimiters[index];
		const bool strikethrough = closer.marker == L'~';
		auto& candidates = openers[strikethrough ? 2 : closer.marker == L'*' ? 0 : 1];
		while (closer.close && closer.remaining) {
			size_t match = candidates.size();
			while (match > 0) {
				const auto& opener = delimiters[candidates[match - 1]];
				// CommonMark's rule of three prevents ambiguous intraword pairings.
				if (strikethrough || !((opener.close || closer.open) && (opener.length + closer.length) % 3 == 0
					&& (opener.length % 3 != 0 || closer.length % 3 != 0))) break;
				--match;
			}
			if (match == 0) break;
			const size_t openerIndex = candidates[match - 1];
			auto& opener = delimiters[openerIndex];
			const size_t used = opener.remaining >= 2 && closer.remaining >= 2 ? 2 : 1;
			const size_t from = opener.start + opener.remaining;
			const size_t to = closer.start;
			auto& changes = strikethrough ? strike : used == 2 ? bold : italic;
			++changes[from];
			--changes[to];
			std::fill(syntax.begin() + from - used, syntax.begin() + from, true);
			std::fill(syntax.begin() + to, syntax.begin() + to + used, true);
			opener.remaining -= used;
			closer.start += used;
			closer.remaining -= used;
			// A match closes its interior delimiter stack, preventing crossing spans.
			for (auto& stack : openers) {
				while (!stack.empty() && stack.back() > openerIndex) stack.pop_back();
			}
			if (!opener.remaining) candidates.pop_back();
		}
		if (closer.open && closer.remaining) candidates.push_back(index);
	}
	std::vector<unsigned char> styles(text.size(), Normal);
	int boldDepth = 0, italicDepth = 0, strikeDepth = 0;
	for (size_t pos = 0; pos < text.size(); ++pos) {
		boldDepth += bold[pos];
		italicDepth += italic[pos];
		strikeDepth += strike[pos];
		if (!literal[pos] && !syntax[pos]) {
			styles[pos] = static_cast<unsigned char>((boldDepth ? Bold : Normal) | (italicDepth ? Italic : Normal)
				| (strikeDepth ? Strike : Normal));
		}
	}
	return styles;
}
