/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "view/CMarkdownEmphasis.h"
#include "doc/logic/CDocLine.h"
#include "types/CType.h"
#include <algorithm>
#include <array>
#include <string>

namespace {
bool IsSpace(wchar_t c)
{
	if (c < 0x80) return c == 0 || c == L' ' || (L'\t' <= c && c <= L'\r');
	WORD category = 0;
	::GetStringTypeW(CT_CTYPE1, &c, 1, &category);
	return (category & C1_SPACE) != 0;
}

bool IsPunctuation(wchar_t c)
{
	if (c < 0x80) return (L'!' <= c && c <= L'/') || (L':' <= c && c <= L'@')
		|| (L'[' <= c && c <= L'`') || (L'{' <= c && c <= L'~');
	// Include Unicode punctuation (not just the process's C locale).
	WORD category = 0;
	::GetStringTypeW(CT_CTYPE1, &c, 1, &category);
	return (category & C1_PUNCT) != 0;
}

size_t RunEnd(std::wstring_view text, size_t start)
{
	size_t end = start + 1;
	while (end < text.size() && text[end] == text[start]) ++end;
	return end;
}

std::wstring_view LineText(const CDocLine* line)
{
	if (line->GetLengthWithoutEOL() == 0) return {};
	return {line->GetPtr(), static_cast<size_t>(static_cast<int>(line->GetLengthWithoutEOL()))};
}
}

bool CMarkdownEmphasis::IsEnabled(const STypeConfig& type)
{
	if (_wcsicmp(type.m_szTypeName, L"Markdown") == 0) return true;
	std::wstring_view extensions(type.m_szTypeExts);
	while (!extensions.empty()) {
		const auto end = extensions.find_first_of(L" ;,");
		auto ext = extensions.substr(0, end);
		if (ext.substr(0, 2) == L"*.") ext.remove_prefix(2);
		for (const auto known : {L"md", L"markdown", L"mdown"}) {
			if (ext.size() == wcslen(known) && _wcsnicmp(ext.data(), known, ext.size()) == 0) return true;
		}
		if (end == std::wstring_view::npos) break;
		extensions.remove_prefix(end + 1);
	}
	return false;
}

std::vector<unsigned char> CMarkdownEmphasis::Parse(std::wstring_view text)
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
		if (c != L'*' && c != L'_') { ++pos; continue; }
		const size_t end = RunEnd(text, pos);
		const wchar_t before = pos ? text[pos - 1] : 0;
		const wchar_t after = end < text.size() ? text[end] : 0;
		const bool left = !IsSpace(after) && (!IsPunctuation(after) || IsSpace(before) || IsPunctuation(before));
		const bool right = !IsSpace(before) && (!IsPunctuation(before) || IsSpace(after) || IsPunctuation(after));
		delimiters.push_back({pos, end - pos, end - pos, c,
			left && (c == L'*' || !right || IsPunctuation(before)),
			right && (c == L'*' || !left || IsPunctuation(after))});
		pos = end;
	}

	std::array<std::vector<size_t>, 2> openers;
	std::vector<int> bold(text.size() + 1), italic(text.size() + 1);
	std::vector<bool> syntax(text.size(), false);
	for (size_t index = 0; index < delimiters.size(); ++index) {
		auto& closer = delimiters[index];
		auto& candidates = openers[closer.marker == L'*' ? 0 : 1];
		while (closer.close && closer.remaining) {
			size_t match = candidates.size();
			while (match > 0) {
				const auto& opener = delimiters[candidates[match - 1]];
				// CommonMark's rule of three prevents ambiguous intraword pairings.
				if (!((opener.close || closer.open) && (opener.length + closer.length) % 3 == 0
					&& (opener.length % 3 != 0 || closer.length % 3 != 0))) break;
				--match;
			}
			if (match == 0) break;
			const size_t openerIndex = candidates[match - 1];
			auto& opener = delimiters[openerIndex];
			const size_t used = opener.remaining >= 2 && closer.remaining >= 2 ? 2 : 1;
			const size_t from = opener.start + opener.remaining;
			const size_t to = closer.start;
			auto& changes = used == 2 ? bold : italic;
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
	int boldDepth = 0, italicDepth = 0;
	for (size_t pos = 0; pos < text.size(); ++pos) {
		boldDepth += bold[pos];
		italicDepth += italic[pos];
		if (!literal[pos] && !syntax[pos]) {
			styles[pos] = static_cast<unsigned char>((boldDepth ? Bold : Normal) | (italicDepth ? Italic : Normal));
		}
	}
	return styles;
}

CMarkdownEmphasis::BlockInfo CMarkdownEmphasis::ScanBlock(std::wstring_view text, BlockState& state)
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

void CMarkdownEmphasis::Prepare(const CDocLine* line)
{
	m_current = {};
	if (!line) return;
	if (const auto found = m_lines.find(line); found != m_lines.end()) {
		m_current = found->second;
		return;
	}
	m_lines.clear();
	m_styles.clear();
	if (!m_nextLine) {
		m_nextLine = line;
		while (m_nextLine->GetPrevLine()) m_nextLine = m_nextLine->GetPrevLine();
		m_block = {};
	}
	const CDocLine* first = m_nextLine;
	while (m_nextLine && m_nextLine != line) {
		const auto block = ScanBlock(LineText(m_nextLine), m_block);
		m_nextLine = m_nextLine->GetNextLine();
		if (block.boundary) first = m_nextLine;
	}
	if (!m_nextLine) return;
	const auto block = ScanBlock(LineText(line), m_block);
	m_nextLine = line->GetNextLine();
	if (block.code) return;
	if (block.boundary) first = line;
	else {
		// Parse a whole paragraph, including soft line breaks and offscreen delimiters.
		while (m_nextLine) {
			auto nextState = m_block;
			if (ScanBlock(LineText(m_nextLine), nextState).boundary) break;
			m_block = nextState;
			m_nextLine = m_nextLine->GetNextLine();
		}
	}
	std::wstring paragraph;
	for (auto* current = first; current && current != m_nextLine; current = current->GetNextLine()) {
		const auto text = LineText(current);
		m_lines.emplace(current, LineRange{paragraph.size(), text.size()});
		paragraph.append(text);
		paragraph += L'\n';
	}
	m_styles = Parse(paragraph);
	m_current = m_lines.at(line);
}

unsigned char CMarkdownEmphasis::GetStyle(size_t position) const
{
	return position < m_current.length ? m_styles[m_current.offset + position] : static_cast<unsigned char>(Normal);
}
