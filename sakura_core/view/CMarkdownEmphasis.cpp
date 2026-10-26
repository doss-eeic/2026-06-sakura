/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "view/CMarkdownEmphasis.h"
#include "parse/markdown/CMarkdownBlockParser.h"
#include "parse/markdown/CMarkdownInlineParser.h"
#include "doc/logic/CDocLine.h"
#include "types/CType.h"
#include <string>

namespace {
std::wstring_view LineText(const CDocLine* line)
{
	if (line->GetLengthWithoutEOL() == 0) return {};
	return {line->GetPtr(), static_cast<size_t>(static_cast<int>(line->GetLengthWithoutEOL()))};
}
}

bool CMarkdownEmphasis::IsEnabled(const STypeConfig& type) {
	return type.m_eSyntaxDisplay == SYNTAX_DISPLAY_MARKDOWN;
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
		const auto block = CMarkdownBlockParser::ScanBlock(LineText(m_nextLine), m_block);
		m_nextLine = m_nextLine->GetNextLine();
		if (block.boundary) first = m_nextLine;
	}
	if (!m_nextLine) return;
	const auto block = CMarkdownBlockParser::ScanBlock(LineText(line), m_block);
	m_nextLine = line->GetNextLine();
	if (block.code) return;
	if (block.boundary) first = line;
	else {
		// Parse a whole paragraph, including soft line breaks and offscreen delimiters.
		while (m_nextLine) {
			auto nextState = m_block;
			if (CMarkdownBlockParser::ScanBlock(LineText(m_nextLine), nextState).boundary) break;
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
	m_styles = CMarkdownInlineParser::Parse(paragraph);
	m_current = m_lines.at(line);
}

unsigned char CMarkdownEmphasis::GetStyle(size_t position) const
{
	return position < m_current.length ? m_styles[m_current.offset + position] : static_cast<unsigned char>(Markdown::Normal);
}
