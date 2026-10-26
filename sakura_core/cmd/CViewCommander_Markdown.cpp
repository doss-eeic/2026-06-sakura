/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "cmd/CViewCommander.h"
#include "cmd/CViewCommander_inline.h"
#include "cmd/markdown/MarkdownCommands.h"
#include "doc/logic/CDocLine.h"
#include "dlg/CDlgInput1.h"
#include "env/DLLSHAREDATA.h"
#include "CSelectLang.h"
#include <algorithm>
#include <string>
#include <vector>

namespace {
std::wstring EscapeLabel(const std::wstring& text)
{
	std::wstring result;
	for (wchar_t c : text) {
		if (c == L'\\' || c == L'[' || c == L']') result += L'\\';
		result += c;
	}
	return result;
}

std::wstring EscapeDestination(const std::wstring& text)
{
	std::wstring result;
	constexpr wchar_t hex[] = L"0123456789ABCDEF";
	for (wchar_t c : text) {
		if (c <= 0x20 || c == L'(' || c == L')' || c == L'<' || c == L'>' || c == L'\\') {
			result += L'%';
			result += hex[(c >> 4) & 15];
			result += hex[c & 15];
		} else result += c;
	}
	return result;
}
}

void CViewCommander::Command_MARKDOWN(EFunctionCode command)
{
	auto& selection = m_pCommanderView->GetSelectionInfo();
#ifdef _DEBUG
	const auto debugRange = selection.m_sSelect;
	MYTRACE(L"Markdown command=%d selection=(%d,%d)-(%d,%d) mouseSelecting=%d box=%d\n",
		static_cast<int>(command), static_cast<int>(debugRange.GetFrom().GetX2()),
		static_cast<int>(debugRange.GetFrom().GetY2()), static_cast<int>(debugRange.GetTo().GetX2()),
		static_cast<int>(debugRange.GetTo().GetY2()), selection.IsMouseSelecting(), selection.IsBoxSelecting());
#endif
	if (selection.IsMouseSelecting()) {
		ErrorBeep();
		return;
	}
	const bool selected = selection.IsTextSelected();
	const bool inlineCommand = command == F_MARKDOWN_BOLD || command == F_MARKDOWN_ITALIC
		|| command == F_MARKDOWN_STRIKE || command == F_MARKDOWN_CODE
		|| command == F_MARKDOWN_LINK || command == F_MARKDOWN_IMAGE;
	auto& layoutMgr = GetDocument()->m_cLayoutMgr;
	CLogicRange range;
	if (selected) {
		if (selection.IsBoxSelecting()) {
			CLayoutRange box;
			TwoPointToRange(&box, selection.m_sSelect.GetFrom(), selection.m_sSelect.GetTo());
			layoutMgr.LayoutToLogic(box, &range);
		} else {
			// Normal selections are ordered by position, not by rectangular columns.
			layoutMgr.LayoutToLogic(selection.m_sSelect, &range);
		}
	} else {
		range.SetFrom(GetCaret().GetCaretLogicPos());
		range.SetTo(range.GetFrom());
	}

	std::wstring result;
	size_t caretOffset = 0;
	if (inlineCommand) {
		CNativeW data;
		if (selected && !m_pCommanderView->GetSelectedDataSimple(data)) {
#ifdef _DEBUG
			MYTRACE(L"Markdown: GetSelectedDataSimple failed\n");
#endif
			return;
		}
		std::wstring text;
		if (data.GetStringLength() > 0) text.assign(data.GetStringPtr(), static_cast<size_t>(data.GetStringLength()));
		std::wstring opening, closing;
		switch (command) {
		case F_MARKDOWN_BOLD: opening = closing = L"**"; break;
		case F_MARKDOWN_ITALIC: opening = closing = L"*"; break;
		case F_MARKDOWN_STRIKE: opening = closing = L"~~"; break;
		case F_MARKDOWN_CODE:
			opening = closing = std::wstring(Markdown::Commands::LongestRun(text, L'`') + 1, L'`');
			if (!text.empty() && (text.front() == L'`' || text.back() == L'`'
				|| (text.front() == L' ' && text.back() == L' ' && text.find_first_not_of(L' ') != std::wstring::npos))) {
				opening += L' ';
				closing.insert(0, 1, L' ');
			}
			break;
		default:
			{
				wchar_t destination[2048]{};
				CDlgInput1 dialog;
				if (!dialog.DoModal(G_AppInstance(), m_pCommanderView->GetHwnd(), LS(command),
					L"URL", std::size(destination) - 1, destination) || destination[0] == L'\0') return;
				opening = command == F_MARKDOWN_IMAGE ? L"![" : L"[";
				closing = L"](" + EscapeDestination(destination) + L")";
				text = EscapeLabel(text);
			}
			break;
		}
		if (selection.IsBoxSelecting()) {
			// Use the existing rectangular-selection edit/Undo implementation.
			Command_ENCLOSE(opening, closing);
			return;
		}
		// Toggle emphasis when the selection includes the delimiters, or is inside them.
		const bool emphasis = command == F_MARKDOWN_BOLD || command == F_MARKDOWN_ITALIC || command == F_MARKDOWN_STRIKE;
		bool removed = false;
		if (emphasis && text.size() >= opening.size() + closing.size()
			&& text.compare(0, opening.size(), opening) == 0
			&& text.compare(text.size() - closing.size(), closing.size(), closing) == 0
			&& (command != F_MARKDOWN_ITALIC || text.size() < 4 || text[1] != L'*' || text[text.size() - 2] != L'*')) {
			result = text.substr(opening.size(), text.size() - opening.size() - closing.size());
			removed = true;
		} else if (emphasis && range.GetFrom().GetY2() == range.GetTo().GetY2()) {
			const auto* line = GetDocument()->m_cDocLineMgr.GetLine(range.GetFrom().GetY2());
			if (line) {
				const std::wstring body(line->GetPtr(), static_cast<int>(line->GetLengthWithoutEOL()));
				const size_t from = static_cast<int>(range.GetFrom().GetX2());
				const size_t to = static_cast<int>(range.GetTo().GetX2());
				if (from >= opening.size() && to + closing.size() <= body.size()
					&& body.compare(from - opening.size(), opening.size(), opening) == 0
					&& body.compare(to, closing.size(), closing) == 0
					&& (command != F_MARKDOWN_ITALIC || ((from < 2 || body[from - 2] != L'*') && (to + 1 == body.size() || body[to + 1] != L'*')))) {
					CLogicPoint fromPoint = range.GetFrom(), toPoint = range.GetTo();
					fromPoint.Offset(-static_cast<int>(opening.size()), 0);
					toPoint.Offset(static_cast<int>(closing.size()), 0);
					range.SetFrom(fromPoint);
					range.SetTo(toPoint);
					result = text;
					removed = true;
				}
			}
		}
		if (!removed) {
			// Single-line delimiters share the bracket insertion/wrapping path.
			if (command != F_MARKDOWN_LINK && command != F_MARKDOWN_IMAGE) {
#ifdef _DEBUG
				MYTRACE(L"Markdown: enclose selected=%d length=%d\n", selected, data.GetStringLength());
#endif
				Command_ENCLOSE(opening, closing);
				return;
			}
			result = opening + text + closing;
			caretOffset = opening.size();
		}
	} else {
		// Line commands operate on logical lines, including all rows touched by a box.
		CLogicInt first = range.GetFrom().GetY2(), last = range.GetTo().GetY2();
		if (selected && !selection.IsBoxSelecting() && last > first && range.GetTo().GetX2() == 0) --last;
		range.SetFrom(CLogicPoint(CLogicInt(0), first));
		std::vector<Markdown::Commands::MarkdownLine> lines;
		for (CLogicInt y = first; y <= last; ++y) {
			const auto* line = GetDocument()->m_cDocLineMgr.GetLine(y);
			const CLogicInt length = line ? line->GetLengthWithoutEOL() : CLogicInt(0);
			lines.push_back({line ? std::wstring(line->GetPtr(), static_cast<int>(length)) : L"",
				y < last && line ? line->GetEol().GetValue2() : L""});
			if (y == last) range.SetTo(CLogicPoint(length, y));
		}
		const std::wstring eol = GetDocument()->m_cDocEditor.GetNewLineCode().GetValue2();
		const std::wstring original = Markdown::Commands::JoinLines(lines);
		if (command == F_MARKDOWN_FENCE) {
			result = Markdown::Commands::ToggleFence(lines, original, eol, caretOffset);
		} else if (command == F_MARKDOWN_HR) {
			result = original;
			if (!result.empty()) result += eol + eol;
			result += L"---" + eol + eol;
			caretOffset = result.size();
		} else if (command == F_MARKDOWN_TABLE) {
			result = Markdown::Commands::MakeTable(lines, eol);
			caretOffset = 2;
		} else if (command == F_MARKDOWN_TASKCHECK) {
			result = Markdown::Commands::ToggleTaskChecks(lines);
			caretOffset = lines.front().text.size();
		} else {
			result = Markdown::Commands::ToggleLinePrefixes(command, lines, caretOffset);
		}
		// Tables and thematic breaks need a paragraph boundary before the block.
		if ((command == F_MARKDOWN_TABLE || command == F_MARKDOWN_HR) && first > 0) {
			const auto* previous = GetDocument()->m_cDocLineMgr.GetLine(first - CLogicInt(1));
			if (previous && previous->GetLengthWithoutEOL() > 0) {
				result.insert(0, eol);
				caretOffset += eol.size();
			}
		}
	}

	selection.DisableSelectArea(true);
	COpeBlk* const opeBlk = m_pCommanderView->m_bDoing_UndoRedo ? nullptr : GetOpeBlk();
	m_pCommanderView->ReplaceData_CEditView2(range, result.c_str(), CLogicInt(static_cast<int>(result.size())), true, opeBlk);
	const CLogicPoint afterReplace = GetCaret().GetCaretLogicPos();
	CLogicPoint target = range.GetFrom();
	for (size_t i = 0; i < std::min(caretOffset, result.size()); ++i) {
		if (WCODE::IsLineDelimiter(result[i], GetDllShareData().m_Common.m_sEdit.m_bEnableExtEol)) {
			if (result[i] == L'\r' && i + 1 < caretOffset && result[i + 1] == L'\n') ++i;
			target.Set(CLogicInt(0), target.GetY2() + CLogicInt(1));
		} else target.Offset(1, 0);
	}
	CLayoutPoint targetLayout;
	layoutMgr.LogicToLayout(selected ? afterReplace : target, &targetLayout);
	GetCaret().MoveCursor(targetLayout, true);
	if (selected) {
		CLayoutPoint startLayout;
		layoutMgr.LogicToLayout(range.GetFrom(), &startLayout);
		selection.BeginSelectArea(&startLayout);
		selection.ChangeSelectAreaByCurrentCursor(targetLayout);
	}
	const auto finalCaret = GetCaret().GetCaretLogicPos();
	if (opeBlk && finalCaret != afterReplace) {
		opeBlk->AppendOpe(new CMoveCaretOpe(afterReplace, finalCaret));
		GetDocument()->m_cDocEditor.m_nOpeBlkRedawCount++;
	}
	GetCaret().m_nCaretPosX_Prev = GetCaret().GetCaretLayoutPos().GetX2();
	m_pCommanderView->PostprocessCommand_hokan();
}
