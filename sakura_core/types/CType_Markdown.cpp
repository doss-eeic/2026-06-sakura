/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "types/CType.h"
#include "config/system_constants.h"
#include "view/colors/EColorIndexType.h"
#include "doc/CEditDoc.h"
#include "doc/CDocOutline.h"
#include "doc/logic/CDocLine.h"
#include "outline/CFuncInfoArr.h"
#include "parse/markdown/CMarkdownBlockParser.h"
#include "CSelectLang.h"
#include <algorithm>
#include <string>
#include <vector>

void CType_Markdown::InitTypeConfigImp(STypeConfig* type)
{
	wcscpy(type->m_szTypeName, L"Markdown");
	wcscpy(type->m_szTypeExts, L"md,markdown,mdown");
	type->m_encoding.m_eDefaultCodetype = CODE_UTF8;
	type->m_eDefaultOutline = OUTLINE_MARKDOWN;
	type->m_eSmartIndent = SMARTINDENT_MARKDOWN;
	type->m_eSyntaxDisplay = SYNTAX_DISPLAY_MARKDOWN;
	type->m_ColorInfoArr[COLORIDX_SSTRING].m_bDisp = false;
	type->m_ColorInfoArr[COLORIDX_WSTRING].m_bDisp = false;
	const auto bind = [type](int key, int modifiers, EFunctionCode command) {
		type->m_typeKeyBindings[key].m_bOverride[modifiers] = true;
		type->m_typeKeyBindings[key].m_nFuncCodeArr[modifiers] = command;
	};
	bind('B', _CTRL, F_MARKDOWN_BOLD);
	bind('I', _CTRL, F_MARKDOWN_ITALIC);
	bind('K', _CTRL, F_MARKDOWN_LINK);
	bind('K', _CTRL | _SHIFT, F_MARKDOWN_IMAGE);
	bind('X', _CTRL | _SHIFT, F_MARKDOWN_STRIKE);
	bind('Q', _CTRL | _SHIFT, F_MARKDOWN_QUOTE);
	bind('7', _CTRL | _SHIFT, F_MARKDOWN_ORDERED);
	bind('8', _CTRL | _SHIFT, F_MARKDOWN_BULLET);
	bind('9', _CTRL | _SHIFT, F_MARKDOWN_TASK);
	bind(VK_RETURN, _CTRL, F_MARKDOWN_TASKCHECK);
	bind('E', _CTRL, F_MARKDOWN_CODE);
	bind('C', _CTRL | _SHIFT, F_MARKDOWN_FENCE);
	bind('T', _CTRL | _SHIFT, F_MARKDOWN_TABLE);
	bind('H', _CTRL | _SHIFT, F_MARKDOWN_HR);
	for (int level = 0; level < 6; ++level) {
		bind('1' + level, _CTRL, static_cast<EFunctionCode>(F_MARKDOWN_H1 + level));
	}
}

void CDocOutline::MakeTopicList_markdown(CFuncInfoArr* pcFuncInfoArr)
{
	const int tabWidth = m_pcDocRef->m_cLayoutMgr.GetTabSpaceKetas();
	if (tabWidth <= 0) return;
	Markdown::ParseState state;
	std::vector<unsigned int> levels;
	std::wstring paragraph;
	Markdown::SourcePosition paragraphStart;
	for (CLogicInt y(0); y < m_pcDocRef->m_cDocLineMgr.GetLineCount(); ++y) {
		const auto* line = m_pcDocRef->m_cDocLineMgr.GetLine(y);
		const std::wstring_view text(line->GetPtr(), static_cast<size_t>(line->GetLengthWithoutEOL()));
		const auto before = state;
		const auto context = CMarkdownBlockParser::ParseLine(text, static_cast<size_t>(y), tabWidth, state);
		if (context.heading) {
			const auto& heading = *context.heading;
			std::wstring title;
			Markdown::SourcePosition position;
			if (heading.titleRange) {
				const auto& range = *heading.titleRange;
				title = text.substr(range.begin.offset, range.end.offset - range.begin.offset);
				position = heading.markerRange.begin;
			} else {
				// 下線形式の見出しは、同じ段落を構成する前行までの本文を使う。
				title = paragraph;
				position = paragraphStart;
			}
			if (title.empty()) title = LS(STR_NO_TITLE1);
			while (!levels.empty() && levels.back() >= heading.level) levels.pop_back();
			const CLogicPoint logic(CLogicInt(static_cast<int>(position.offset)), CLogicInt(static_cast<int>(position.line)));
			CLayoutPoint layout;
			m_pcDocRef->m_cLayoutMgr.LogicToLayout(logic, &layout);
			pcFuncInfoArr->AppendData(logic.GetY2() + 1, logic.GetX2() + 1,
				layout.GetY2() + 1, layout.GetX2() + 1, title.c_str(), nullptr, 0, static_cast<int>(levels.size()));
			levels.push_back(heading.level);
			paragraph.clear();
		} else if (state.paragraphOpen && context.bodyRange) {
			const bool sameContainers = std::equal(before.containers.begin(), before.containers.end(),
				context.containers.begin(), context.containers.end(), [](const auto& a, const auto& b) {
					return a.kind == b.kind && a.start.line == b.start.line && a.start.offset == b.start.offset;
				});
			if (!before.paragraphOpen || !sameContainers) paragraph.clear();
			const auto& range = *context.bodyRange;
			auto body = text.substr(range.begin.offset, range.end.offset - range.begin.offset);
			const auto begin = body.find_first_not_of(L" \t");
			if (begin != std::wstring_view::npos) {
				if (paragraph.empty()) paragraphStart = {static_cast<size_t>(y), range.begin.offset + begin};
				else paragraph += L' ';
				paragraph += body.substr(begin, body.find_last_not_of(L" \t") - begin + 1);
			}
		} else {
			paragraph.clear();
		}
	}
}
