/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "view/CEditView.h"
#include "window/CEditWnd.h"
#include "types/CTypeSupport.h"
#include "CSelectLang.h"
#include "sakura_rc.h"

namespace {
std::vector<NoteView::Block> ScanBlocks(CEditDoc& doc, CLogicInt first, CLogicInt last)
{
	std::vector<NoteView::Block> blocks;
	CLayoutPoint position;
	doc.m_cLayoutMgr.LogicToLayout(CLogicPoint(0, first), &position);
	const CLayout* layout = doc.m_cLayoutMgr.SearchLineByLayoutY(position.y);
	auto row = static_cast<Int>(position.y);
	while (layout && layout->GetLogicLineNo() <= last) {
		const auto line = layout->GetDocLineRef();
		const auto logical = layout->GetLogicLineNo();
		auto link = ImageLink::FindStandalone(std::wstring_view(line->GetPtr(), static_cast<size_t>(line->GetLengthWithoutEOL())));
		do {
			layout = layout->GetNextLayout();
			++row;
		} while (layout && layout->GetLogicLineNo() == logical);
		if (link) blocks.push_back({static_cast<Int>(logical), row - 1, std::move(link->path)});
	}
	return blocks;
}
}

bool CEditView::IsNoteView() const
{
	return !m_bMiniMap && GetEditWnd().IsNoteView();
}

CLayoutInt CEditView::ScrollDisplayByV(CLayoutInt rows)
{
	return ScrollDisplayAtV(NoteView::DisplayRow(NoteView::Projection::Add(GetTextArea().GetDisplayTop().value, static_cast<Int>(rows))));
}

CLayoutInt CEditView::ScrollDisplayAtV(NoteView::DisplayRow row, BOOL redraw)
{
	if (!HasNoteBlocks()) return ScrollAtV(CLayoutInt(std::clamp<std::int64_t>(row.value, 0, INT_MAX)), redraw);
	CancelImagePreview();
	auto& area = GetTextArea();
	const auto total = area.GetNoteProjection().FromLayout(static_cast<Int>(GetDocument()->m_cLayoutMgr.GetLineCount())).value;
	const auto maximum = std::max<std::int64_t>(0, NoteView::Projection::Add(total, 2) - static_cast<Int>(area.m_nViewRowNum));
	row.value = std::clamp<std::int64_t>(row.value, 0, maximum);
	const auto delta = row.value - area.GetDisplayTop().value;
	if (!delta) return CLayoutInt(0);
	area.SetDisplayTop(row);
	if (GetHwnd()) {
		RECT rect = area.GetAreaRect();
		rect.left = 0;
		InvalidateRect(&rect, FALSE);
		AdjustScrollBars(redraw);
		if (GetDrawSwitch()) UpdateWindow();
		GetCaret().ShowEditCaret();
		MiniMapRedraw(true);
	}
	return CLayoutInt(std::clamp<std::int64_t>(delta, -INT_MAX, INT_MAX));
}

void CEditWnd::SetNoteView(bool enabled)
{
	if (m_noteView == enabled) return;
	for (int i = 0; i < GetAllViewCount(); ++i) GetView(i).OnBeforeViewLayout();
	m_noteImageCache.Reset();
	m_noteView = enabled;
	for (int i = 0; i < GetAllViewCount(); ++i) GetView(i).ApplyNoteView();
}

void CEditView::OnBeforeViewLayout()
{
	if (m_bMiniMap || m_noteLoading || m_noteAnchor.valid) return;
	const auto& area = GetTextArea();
	m_noteAnchor.valid = true;
	m_noteAnchor.relativeRow = area.RelativeRow(GetCaret().GetCaretLayoutPos().y);
	m_noteAnchor.caretVisible = m_noteAnchor.relativeRow >= 0 &&
		m_noteAnchor.relativeRow < static_cast<Int>(area.m_nViewRowNum);
	m_noteAnchor.path.clear();
	m_noteAnchor.blockLine = -1;
	m_noteAnchor.blockOffset = -1;
	if (m_noteAnchor.caretVisible) {
		m_noteAnchor.text = GetCaret().GetCaretLogicPos();
	} else {
		const auto hit = area.GetNoteProjection().ToLayout(area.GetDisplayTop());
		GetDocument()->m_cLayoutMgr.LayoutToLogic(CLayoutPoint(CLayoutInt(0),
			CLayoutInt(hit.layoutLine + (hit.block ? 1 : 0))), &m_noteAnchor.text);
		m_noteAnchor.relativeRow = 0;
		if (hit.block) {
			m_noteAnchor.path = hit.block->path;
			m_noteAnchor.blockLine = CLogicInt(hit.block->logicalLine);
			m_noteAnchor.blockOffset = hit.offset;
		}
	}
}

void CEditView::RebuildNoteProjection()
{
	auto& projection = GetTextArea().GetNoteProjection();
	if (IsNoteView()) projection.Reset(ScanBlocks(*GetDocument(), CLogicInt(0), GetDocument()->m_cDocLineMgr.GetLineCount()));
	else projection.Clear();
}

void CEditView::OnAfterViewLayout(const SViewLayoutChange& change)
{
	if (m_bMiniMap) return;
	auto& area = GetTextArea();
	bool changed;
	if (IsNoteView() && change.firstLine >= 0) {
		changed = area.GetNoteProjection().Replace(change.firstLine, change.oldLastLine, change.logicalDelta,
			static_cast<Int>(change.layoutDelta), ScanBlocks(*GetDocument(), change.firstLine, change.newLastLine));
	} else {
		const auto previous = area.GetNoteProjection().Blocks();
		RebuildNoteProjection();
		changed = previous != area.GetNoteProjection().Blocks();
	}
	if (changed || change.firstLine < 0) InvalidateNoteRequests();
	if (m_noteAnchor.valid && (changed || (IsNoteView() && change.firstLine < 0))) {
		CLayoutPoint text;
		auto anchor = m_noteAnchor.text;
		if (change.firstLine >= 0 && anchor.y > change.oldLastLine) anchor.y += change.logicalDelta;
		else if (change.firstLine >= 0 && !m_noteAnchor.caretVisible && anchor.y > change.firstLine)
			anchor.y = change.newLastLine;
		GetDocument()->m_cLayoutMgr.LogicToLayout(anchor, &text);
		auto top = area.GetNoteProjection().FromLayout(static_cast<Int>(text.y)).value - m_noteAnchor.relativeRow;
		if (!m_noteAnchor.caretVisible && m_noteAnchor.blockOffset >= 0) {
			auto blockLine = m_noteAnchor.blockLine;
			if (change.firstLine >= 0) {
				if (blockLine > change.oldLastLine) blockLine += change.logicalDelta;
				else if (blockLine >= change.firstLine) blockLine = change.newLastLine;
			}
			const auto& blocks = area.GetNoteProjection().Blocks();
			for (size_t i = 0; i < blocks.size(); ++i) {
				if (blocks[i].logicalLine == static_cast<int>(blockLine) && blocks[i].path == m_noteAnchor.path) {
					top = area.GetNoteProjection().BlockStart(i).value + m_noteAnchor.blockOffset;
					break;
				}
			}
		}
		const auto maxTop = std::max<std::int64_t>(0, area.GetNoteProjection().FromLayout(
			static_cast<Int>(GetDocument()->m_cLayoutMgr.GetLineCount())).value + 2 - static_cast<Int>(area.m_nViewRowNum));
		area.SetDisplayTop(NoteView::DisplayRow(std::clamp<std::int64_t>(top, 0, maxTop)));
	}
	m_noteAnchor.valid = false;
	if (changed && GetHwnd()) {
		AdjustScrollBars();
		InvalidateRect(nullptr, FALSE);
	}
}

void CEditView::ApplyNoteView()
{
	CancelImagePreview();
	::GetCursorPos(&m_imageMousePoint);
	OnAfterViewLayout({});
	if (GetHwnd()) {
		RedrawAll();
		GetCaret().ShowEditCaret();
	}
}

bool CEditView::IsNoteBlockPoint(POINT point) const
{
	const auto rect = GetTextArea().GetAreaRect();
	return IsNoteView() && ::PtInRect(&rect, point) && GetTextArea().HitClientRow(point.y).block;
}

CLayoutPoint CEditView::NoteBlockTextPosition(POINT point, bool selecting)
{
	const auto hit = GetTextArea().HitClientRow(point.y);
	CLayoutPoint text(CLayoutInt(0), CLayoutInt(hit.layoutLine));
	if (!hit.block) return text;
	const auto logical = CLogicInt(hit.block->logicalLine);
	if (!selecting) GetDocument()->m_cLayoutMgr.LogicToLayout(CLogicPoint(0, logical), &text);
	else if (static_cast<int>(GetSelectionInfo().m_sSelectBgn.GetFrom().y) <= hit.layoutLine) {
		text.y = hit.layoutLine + 1;
		GetCaret().GetAdjustCursorPos(&text);
	} else {
		const auto line = GetDocument()->m_cDocLineMgr.GetLine(logical);
		GetDocument()->m_cLayoutMgr.LogicToLayout(CLogicPoint(line->GetLengthWithoutEOL(), logical), &text);
	}
	return text;
}

void CEditView::ExcludeNoteBlocks(HDC dc) const
{
	const auto& area = GetTextArea();
	const auto& projection = area.GetNoteProjection();
	const auto height = GetTextMetrics().GetHankakuDy();
	for (size_t i = projection.FirstVisibleBlock(area.GetDisplayTop()); i < projection.Blocks().size(); ++i) {
		const auto row = projection.BlockStart(i).value - area.GetDisplayTop().value;
		if (row + NoteView::Projection::BlockRows <= 0) continue;
		if (row > static_cast<int>(area.m_nViewRowNum)) break;
		::ExcludeClipRect(dc, 0, area.GetAreaTop() + static_cast<int>(row) * height, area.GetAreaRight(),
			area.GetAreaTop() + static_cast<int>(row + NoteView::Projection::BlockRows) * height);
	}
}

void CEditView::MoveNotePage(CLayoutInt rows, bool selecting, bool scrollPage)
{
	auto& area = GetTextArea();
	const auto oldTop = area.GetDisplayTop();
	const auto caret = GetCaret().GetCaretLayoutPos();
	const auto target = area.GetNoteProjection().ToLayout(NoteView::DisplayRow(NoteView::Projection::Add(
		area.GetNoteProjection().FromLayout(static_cast<int>(caret.y)).value, static_cast<int>(rows))));
	auto line = target.layoutLine + (target.block && rows > 0 ? 1 : 0);
	line = std::clamp<std::int64_t>(line, 0, INT_MAX);
	const bool drawing = SetDrawSwitch(false);
	GetCaret().Cursor_UPDOWN(CLayoutInt(line) - caret.y, selecting);
	if (scrollPage) ScrollDisplayAtV(NoteView::DisplayRow(NoteView::Projection::Add(oldTop.value, static_cast<int>(rows))));
	SetDrawSwitch(drawing);
	GetCaret().MoveCursor(GetCaret().GetCaretLayoutPos(), true);
	if (GetHwnd()) RedrawAll();
}

void CEditView::DrawNoteBlocks(HDC dc)
{
	const auto& area = GetTextArea();
	const auto& projection = area.GetNoteProjection();
	const auto height = GetTextMetrics().GetHankakuDy();
	const auto& blocks = projection.Blocks();
	const int saved = ::SaveDC(dc);
	::IntersectClipRect(dc, 0, area.GetAreaTop(), area.GetAreaRight(), area.GetAreaBottom());
	RECT clip{};
	::GetClipBox(dc, &clip);
	for (size_t i = projection.FirstVisibleBlock(area.GetDisplayTop()); i < blocks.size(); ++i) {
		const auto row = projection.BlockStart(i).value - area.GetDisplayTop().value;
		if (row + NoteView::Projection::BlockRows <= 0) continue;
		if (row > static_cast<Int>(area.m_nViewRowNum)) break;
		RECT rect{0, area.GetAreaTop() + static_cast<int>(row) * height, area.GetAreaRight(),
			area.GetAreaTop() + static_cast<int>(row + NoteView::Projection::BlockRows) * height};
		RECT intersection{};
		if (!::IntersectRect(&intersection, &clip, &rect)) continue;
		const auto brush = ::CreateSolidBrush(CTypeSupport(this, COLORIDX_TEXT).GetBackColor());
		::FillRect(dc, &rect, brush);
		::DeleteObject(brush);
		rect.left = area.GetAreaLeft();
		if (rect.left >= rect.right || rect.top >= rect.bottom) continue;
		::SetTextColor(dc, CTypeSupport(this, COLORIDX_TEXT).GetTextColor());
		::SetBkMode(dc, TRANSPARENT);
		const UINT dpi = GetHwnd() ? ::GetDpiForWindow(GetHwnd()) : 96;
		const int cap = ::MulDiv(640, dpi ? dpi : 96, 96);
		NoteView::ImageKey key{blocks[i].path, static_cast<UINT>(std::min<int>(rect.right - rect.left, cap)),
			static_cast<UINT>(std::min<int>(rect.bottom - rect.top, cap))};
		auto result = GetEditWnd().GetNoteImageCache().Get(key);
		std::erase_if(m_notePending, [&](const auto& pending) { return pending.logicalLine == blocks[i].logicalLine; });
		if (result.state == NoteView::ImageCache::State::Ready) {
			const auto& image = *result.image;
			BITMAPINFO info{};
			info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			info.bmiHeader.biWidth = image.width;
			info.bmiHeader.biHeight = -static_cast<LONG>(image.height);
			info.bmiHeader.biPlanes = 1;
			info.bmiHeader.biBitCount = 32;
			info.bmiHeader.biCompression = BI_RGB;
			::SetDIBitsToDevice(dc, rect.left, rect.top + (rect.bottom - rect.top - image.height) / 2,
				image.width, image.height, 0, 0, 0, image.height, image.pixels.data(), &info, DIB_RGB_COLORS);
		} else {
			const bool loading = result.state == NoteView::ImageCache::State::Loading;
			::DrawTextW(dc, LS(loading ? STR_NOTE_LOADING : STR_NOTE_ERROR), -1, &rect,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			if (loading) m_notePending.push_back({blocks[i].logicalLine, std::move(key), m_noteGeneration});
		}
	}
	::RestoreDC(dc, saved);
}

void CEditView::InvalidateNoteRequests()
{
	++m_noteGeneration;
	m_notePending.clear();
}

void CEditView::BeginNoteLoad()
{
	m_noteLoading = true;
	m_noteAnchor.valid = false;
	InvalidateNoteRequests();
	GetEditWnd().GetNoteImageCache().Reset();
}

void CEditView::PollNoteImages()
{
	if (!IsNoteView() || m_noteLoading || !GetHwnd() || !GetDrawSwitch()) return;
	const auto pending = m_notePending; // Painting updates the pending list.
	const auto& area = GetTextArea();
	const auto& projection = area.GetNoteProjection();
	for (const auto& request : pending) {
		if (request.generation != m_noteGeneration) continue;
		const auto& blocks = projection.Blocks();
		auto block = std::lower_bound(blocks.begin(), blocks.end(), request.logicalLine,
			[](const auto& b, auto line) { return b.logicalLine < line; });
		if (block == blocks.end() || block->logicalLine != request.logicalLine || block->path != request.key.path) continue;
		const auto row = projection.BlockStart(block - blocks.begin()).value - area.GetDisplayTop().value;
		if (row + NoteView::Projection::BlockRows <= 0 || row > static_cast<int>(area.m_nViewRowNum)) continue;
		const auto result = GetEditWnd().GetNoteImageCache().Get(request.key);
		if (result.state == NoteView::ImageCache::State::Loading) continue;
		const auto height = GetTextMetrics().GetHankakuDy();
		RECT rect{area.GetAreaLeft(), area.GetAreaTop() + static_cast<int>(row) * height, area.GetAreaRight(),
			area.GetAreaTop() + static_cast<int>(row + NoteView::Projection::BlockRows) * height};
		RECT visible{};
		const auto areaRect = area.GetAreaRect();
		if (::IntersectRect(&visible, &rect, &areaRect)) {
			InvalidateRect(&visible, FALSE);
			// Paint before admitting deferred requests which could evict this completed image.
			UpdateWindow();
		}
	}
}

std::wstring CEditView::PrepareNoteInsertion(std::wstring text)
{
	if (!IsNoteView()) return text;
	CLogicPoint first, last;
	if (GetSelectionInfo().IsTextSelected()) {
		GetDocument()->m_cLayoutMgr.LayoutToLogic(GetSelectionInfo().m_sSelect.GetFrom(), &first);
		GetDocument()->m_cLayoutMgr.LayoutToLogic(GetSelectionInfo().m_sSelect.GetTo(), &last);
		if (first.y > last.y || (first.y == last.y && first.x > last.x)) std::swap(first, last);
	} else first = last = GetCaret().GetCaretLogicPos();
	const auto eol = GetDocument()->m_cDocEditor.GetNewLineCode().GetValue2();
	const auto firstLine = GetDocument()->m_cDocLineMgr.GetLine(first.y);
	if (firstLine) {
		const auto length = std::clamp<int>(first.x, 0, firstLine->GetLengthWithoutEOL());
		if (std::wstring_view(firstLine->GetPtr(), length).find_first_not_of(L" \t") != std::wstring_view::npos)
			text.insert(0, eol);
	}
	const auto line = GetDocument()->m_cDocLineMgr.GetLine(last.y);
	if (line && last.x < line->GetLengthWithoutEOL()) {
		const auto suffix = std::wstring_view(line->GetPtr(), line->GetLengthWithoutEOL()).substr(last.x);
		if (suffix.find_first_not_of(L" \t") != std::wstring_view::npos) text += eol;
	}
	return text;
}
