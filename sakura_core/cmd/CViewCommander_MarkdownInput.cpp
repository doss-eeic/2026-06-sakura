/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "cmd/CViewCommander.h"
#include "cmd/CViewCommander_inline.h"
#include "cmd/markdown/MarkdownInput.h"
#include "doc/logic/CDocLine.h"
#include <algorithm>
#include <string>
#include <vector>

namespace {
std::vector<std::wstring_view> MarkdownInputLines(const CDocLineMgr& manager)
{
	std::vector<std::wstring_view> lines;
	const CDocLine* last = nullptr;
	for (CLogicInt y(0); y < manager.GetLineCount(); ++y) {
		last = manager.GetLine(y);
		lines.emplace_back(last->GetPtr(), static_cast<int>(last->GetLengthWithoutEOL()));
	}
	if (!last || last->GetEol().GetLen() != 0) lines.emplace_back();
	return lines;
}

Markdown::SourcePosition MarkdownPosition(const CLogicPoint& point)
{
	return {static_cast<size_t>(point.GetY2()), static_cast<size_t>(point.GetX2())};
}

CLogicPoint LogicPosition(Markdown::SourcePosition point)
{
	return CLogicPoint(CLogicInt(static_cast<int>(point.offset)), CLogicInt(static_cast<int>(point.line)));
}
}

void CViewCommander::ApplyMarkdownInput(const CLogicRange& range, const std::wstring& text,
	const CLogicPoint& caret, const CLogicRange* selected)
{
	auto& selection = m_pCommanderView->GetSelectionInfo();
	auto& layout = GetDocument()->m_cLayoutMgr;
	selection.DisableSelectArea(true);
	COpeBlk* const opeBlk = m_pCommanderView->m_bDoing_UndoRedo ? nullptr : GetOpeBlk();
	m_pCommanderView->ReplaceData_CEditView2(range, text.c_str(), CLogicInt(static_cast<int>(text.size())), true, opeBlk);
	const auto afterReplace = GetCaret().GetCaretLogicPos();
	CLayoutPoint target;
	layout.LogicToLayout(caret, &target);
	GetCaret().MoveCursor(target, true);
	if (selected) {
		CLayoutPoint anchor;
		layout.LogicToLayout(caret == selected->GetFrom() ? selected->GetTo() : selected->GetFrom(), &anchor);
		selection.BeginSelectArea(&anchor);
		selection.ChangeSelectAreaByCurrentCursor(target);
	}
	const auto finalCaret = GetCaret().GetCaretLogicPos();
	if (opeBlk && finalCaret != afterReplace) {
		opeBlk->AppendOpe(new CMoveCaretOpe(afterReplace, finalCaret));
		GetDocument()->m_cDocEditor.m_nOpeBlkRedawCount++;
	}
	GetCaret().m_nCaretPosX_Prev = GetCaret().GetCaretLayoutPos().GetX2();
	m_pCommanderView->PostprocessCommand_hokan();
}

bool CViewCommander::TryMarkdownEnter()
{
	if (m_pCommanderView->m_pTypeData->m_eSmartIndent != SMARTINDENT_MARKDOWN) return false;
	if (!GetDocument()->IsEditable()) return true;
	auto& selection = m_pCommanderView->GetSelectionInfo();
	if (selection.IsMouseSelecting()) { ErrorBeep(); return true; }
	if (selection.IsBoxSelecting()) return false;
	CLogicRange range(GetCaret().GetCaretLogicPos(), GetCaret().GetCaretLogicPos());
	if (selection.IsTextSelected()) GetDocument()->m_cLayoutMgr.LayoutToLogic(selection.m_sSelect, &range);
	const auto lines = MarkdownInputLines(GetDocument()->m_cDocLineMgr);
	const int tabWidth = GetDocument()->m_cLayoutMgr.GetTabSpaceKetas();
	const auto edit = Markdown::Input::Enter(lines, {MarkdownPosition(range.GetFrom()), MarkdownPosition(range.GetTo())},
		tabWidth > 0 ? static_cast<size_t>(tabWidth) : 0, GetDocument()->m_cDocEditor.GetNewLineCode().GetValue2());
	if (!edit) return false;
	ApplyMarkdownInput(CLogicRange(LogicPosition(edit->range.begin), LogicPosition(edit->range.end)), edit->text, LogicPosition(edit->caret));
	return true;
}

bool CViewCommander::TryMarkdownIndent(bool unindent)
{
	if (m_pCommanderView->m_pTypeData->m_eSmartIndent != SMARTINDENT_MARKDOWN) return false;
	if (!GetDocument()->IsEditable()) return true;
	auto& selection = m_pCommanderView->GetSelectionInfo();
	if (selection.IsMouseSelecting()) { ErrorBeep(); return true; }
	if (selection.IsBoxSelecting()) return false;
	const bool selected = selection.IsTextSelected();
	const auto caret = GetCaret().GetCaretLogicPos();
	CLogicRange range(caret, caret);
	if (selected) GetDocument()->m_cLayoutMgr.LayoutToLogic(selection.m_sSelect, &range);
	size_t first = static_cast<size_t>(range.GetFrom().GetY2());
	size_t last = static_cast<size_t>(range.GetTo().GetY2());
	if (selected && last > first && range.GetTo().GetX2() == 0) --last;
	const auto lines = MarkdownInputLines(GetDocument()->m_cDocLineMgr);
	const int tabWidth = GetDocument()->m_cLayoutMgr.GetTabSpaceKetas();
	const auto edits = Markdown::Input::Indent(lines, first, last, tabWidth > 0 ? static_cast<size_t>(tabWidth) : 0, unindent);
	if (!edits) return false;
	if (edits->empty()) return true;
	first = edits->front().line;
	last = edits->back().line;
	std::wstring replacement;
	size_t index = 0;
	for (size_t y = first; y <= last; ++y) {
		if (index < edits->size() && (*edits)[index].line == y) {
			const auto& edit = (*edits)[index++];
			replacement += edit.text;
			replacement += lines[y].substr(edit.oldLength);
		} else replacement += lines[y];
		if (y < last) replacement += GetDocument()->m_cDocLineMgr.GetLine(CLogicInt(static_cast<int>(y)))->GetEol().GetValue2();
	}
	const auto map = [&](CLogicPoint point) {
		for (const auto& edit : *edits) {
			if (static_cast<size_t>(point.GetY2()) != edit.line) continue;
			const size_t oldX = static_cast<size_t>(point.GetX2());
			// 本文内は文字位置を保持。行頭の選択端は行頭のままにする。
			const size_t x = selected && oldX == 0 ? 0 : oldX >= edit.oldLength
				? oldX - edit.oldLength + edit.text.size() : std::min(oldX, edit.text.size());
			point.SetX(CLogicInt(static_cast<int>(x)));
			break;
		}
		return point;
	};
	const CLogicRange mapped(map(range.GetFrom()), map(range.GetTo()));
	ApplyMarkdownInput(CLogicRange(LogicPosition({first, 0}), LogicPosition({last, lines[last].size()})),
		replacement, map(caret), selected ? &mapped : nullptr);
	return true;
}
