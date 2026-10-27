/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "cmd/CViewCommander.h"
#include "cmd/CViewCommander_inline.h"
#include "cmd/markdown/MarkdownCommands.h"
#include "parse/markdown/CMarkdownInlineParser.h"
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
bool GetEmphasisContentRange(
	const std::vector<CMarkdownInlineParser::EmphasisSpan>& spans,
	size_t& from,
	size_t& to
) {
	if (from >= to) {
		return false;
	}

	bool changed;
	do {
		changed = false;

		for (const auto& span : spans) {
			// 選択部分から外側の記号を除く
			if (from < span.openingBegin || to > span.closingEnd) {
				continue;
			}


			const size_t innerFrom = std::max(from, span.contentBegin);
			const size_t innerTo = std::min(to, span.contentEnd);

			if (innerFrom >= innerTo) {
				return false;
			}

			if (innerFrom != from || innerTo != to) {
				from = innerFrom;
				to = innerTo;
				changed = true;
			}
		}
	} while (changed);

	return true;
}

// 原文・置換文字列内のUTF-16位置を、改行を含めて論理位置へ戻す。
CLogicPoint PositionInText(CLogicPoint start, std::wstring_view text,
	size_t offset, bool enableExtEol)
{
	const size_t end = std::min(offset, text.size());
	for (size_t pos = 0; pos < end; ++pos) {
		if (WCODE::IsLineDelimiter(text[pos], enableExtEol)) {
			if (text[pos] == L'\r' && pos + 1 < end && text[pos + 1] == L'\n') ++pos;
			start.Set(CLogicInt(0), start.GetY2() + CLogicInt(1));
		} else {
			start.Offset(1, 0);
		}
	}
	return start;
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
	bool emphasisPrepared = false;
	size_t selectionPrefix = 0;
	size_t selectionSuffix = 0;
	const bool caretAtStart = selected && GetCaret().GetCaretLogicPos() == range.GetFrom();
	const bool enableExtEol = GetDllShareData().m_Common.m_sEdit.m_bEnableExtEol;
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
		if (emphasis && selected) {
			const auto first = range.GetFrom().GetY2();
			const auto last = range.GetTo().GetY2();
			const CLogicPoint origin(CLogicInt(0), first);
			std::wstring body;
			size_t from = static_cast<int>(range.GetFrom().GetX2());
			size_t to = 0;
			// 選択がかかる各原文行を、元の改行コードでつなぐ。
			// 最初と最後の行も全文を含め、選択外にある記号を解析する。
			for (auto y = first; y <= last; ++y) {
				const auto* line = GetDocument()->m_cDocLineMgr.GetLine(y);
				if (!line) {
					// 末尾改行の直後の空行は、CDocLineが存在しないことがある。
					if (y == last && y > first && range.GetTo().GetX2() == 0) {
						to = body.size();
						break;
					}
					return;
				}
				const size_t length = static_cast<int>(line->GetLengthWithoutEOL());
				if (y == first && from > length) return;
				if (y == last) {
					const size_t endOffset = static_cast<int>(range.GetTo().GetX2());
					if (endOffset > length) return;
					to = body.size() + endOffset;
				}
				if (length) body.append(line->GetPtr(), length);
				if (y < last) body += line->GetEol().GetValue2();
			}

			std::vector<CMarkdownInlineParser::EmphasisSpan> spans;
			CMarkdownInlineParser::Parse(body, &spans);
			if (!GetEmphasisContentRange(spans, from, to)) return;

			const Markdown::Style requested =
				command == F_MARKDOWN_BOLD ? Markdown::Bold
				: command == F_MARKDOWN_ITALIC ? Markdown::Italic
				: Markdown::Strike;

			const auto edit = Markdown::Commands::ToggleEmphasis(body, spans, from, to, requested);
			if (!edit) return;
			result = edit->text;
			selectionPrefix = edit->selectionFrom;
			selectionSuffix = result.size() - edit->selectionTo;
			range.SetFrom(PositionInText(origin, body, edit->from, enableExtEol));
			range.SetTo(PositionInText(origin, body, edit->to, enableExtEol));
			emphasisPrepared = true;
		} else if (emphasis) {
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
		if (!emphasisPrepared && !removed) {
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
	const CLogicPoint target = PositionInText(range.GetFrom(), result, caretOffset, enableExtEol);
	CLogicPoint targetPoint = selected ? afterReplace : target;
	CLogicPoint anchorPoint = range.GetFrom();

	if (emphasisPrepared) {
		const CLogicPoint innerFrom = PositionInText(range.GetFrom(), result, selectionPrefix, enableExtEol);
		const CLogicPoint innerTo = PositionInText(range.GetFrom(), result,
			result.size() - selectionSuffix, enableExtEol);

		anchorPoint = caretAtStart ? innerTo : innerFrom;
		targetPoint = caretAtStart ? innerFrom : innerTo;
	}

	CLayoutPoint targetLayout;
	layoutMgr.LogicToLayout(targetPoint, &targetLayout);
	GetCaret().MoveCursor(targetLayout, true);
	if (selected) {
		CLayoutPoint anchorLayout;
		layoutMgr.LogicToLayout(anchorPoint, &anchorLayout);
		selection.BeginSelectArea(&anchorLayout);
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
