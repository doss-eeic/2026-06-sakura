/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "cmd/CViewCommander.h"
#include "cmd/CViewCommander_inline.h"
#include "doc/logic/CDocLine.h"
#include "dlg/CDlgInput1.h"
#include "env/DLLSHAREDATA.h"
#include "CSelectLang.h"
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace {
struct MarkdownLine {
	std::wstring text;
	std::wstring eol;
};

size_t LongestRun(const std::wstring& text, wchar_t marker)
{
	size_t longest = 0, run = 0;
	for (wchar_t c : text) {
		run = c == marker ? run + 1 : 0;
		longest = std::max(longest, run);
	}
	return longest;
}

size_t IndentLength(const std::wstring& text)
{
	const auto pos = text.find_first_not_of(L" \t");
	return pos == std::wstring::npos ? text.size() : pos;
}

// Return the existing list marker's length, including an optional task checkbox.
size_t ListMarkerLength(const std::wstring& text, size_t start, bool& ordered, bool& task)
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

std::wstring JoinLines(const std::vector<MarkdownLine>& lines)
{
	std::wstring result;
	for (const auto& line : lines) result += line.text + line.eol;
	return result;
}

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
		if (selected && !m_pCommanderView->GetSelectedDataSimple(data)) return;
		std::wstring text;
		if (data.GetStringLength() > 0) text.assign(data.GetStringPtr(), static_cast<size_t>(data.GetStringLength()));
		std::wstring opening, closing;
		switch (command) {
		case F_MARKDOWN_BOLD: opening = closing = L"**"; break;
		case F_MARKDOWN_ITALIC: opening = closing = L"*"; break;
		case F_MARKDOWN_STRIKE: opening = closing = L"~~"; break;
		case F_MARKDOWN_CODE:
			opening = closing = std::wstring(LongestRun(text, L'`') + 1, L'`');
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
		std::vector<MarkdownLine> lines;
		for (CLogicInt y = first; y <= last; ++y) {
			const auto* line = GetDocument()->m_cDocLineMgr.GetLine(y);
			const CLogicInt length = line ? line->GetLengthWithoutEOL() : CLogicInt(0);
			lines.push_back({line ? std::wstring(line->GetPtr(), static_cast<int>(length)) : L"",
				y < last && line ? line->GetEol().GetValue2() : L""});
			if (y == last) range.SetTo(CLogicPoint(length, y));
		}
		const std::wstring eol = GetDocument()->m_cDocEditor.GetNewLineCode().GetValue2();
		const std::wstring original = JoinLines(lines);
		if (command == F_MARKDOWN_FENCE) {
			const auto& firstLine = lines.front().text;
			const auto& lastLine = lines.back().text;
			const size_t fenceLen = firstLine.find_first_not_of(L'`');
			const size_t count = fenceLen == std::wstring::npos ? firstLine.size() : fenceLen;
			if (lines.size() >= 3 && count >= 3 && lastLine.size() >= count && lastLine.find_first_not_of(L'`') == std::wstring::npos) {
				lines.erase(lines.begin());
				lines.pop_back();
				lines.back().eol.clear();
				result = JoinLines(lines);
			} else {
				const std::wstring fence(std::max<size_t>(3, LongestRun(original, L'`') + 1), L'`');
				result = fence + eol + original + eol + fence;
				caretOffset = fence.size() + eol.size();
			}
		} else if (command == F_MARKDOWN_HR) {
			result = original;
			if (!result.empty()) result += eol + eol;
			result += L"---" + eol + eol;
			caretOffset = result.size();
		} else if (command == F_MARKDOWN_TABLE) {
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
			caretOffset = 2;
		} else if (command == F_MARKDOWN_TASKCHECK) {
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
			result = JoinLines(lines);
			caretOffset = lines.front().text.size();
		} else {
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
					while (indent + length < line.text.size() && line.text[indent + length] == L'#') ++length;
					if (length > 0 && length <= 6 && indent + length < line.text.size() && line.text[indent + length] == L' ') ++length;
					else length = 0;
				} else if (command == F_MARKDOWN_QUOTE) {
					if (remove) length = prefix.size();
				} else {
					bool ordered, task;
					length = ListMarkerLength(line.text, indent, ordered, task);
				}
				const std::wstring marker = remove ? L"" : command == F_MARKDOWN_ORDERED ? std::to_wstring(number++) + L". " : prefix;
				line.text.replace(indent, length, marker);
			}
			result = JoinLines(lines);
			caretOffset = IndentLength(lines.front().text);
			if (!remove) caretOffset += command == F_MARKDOWN_ORDERED ? 3 : prefix.size();
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
