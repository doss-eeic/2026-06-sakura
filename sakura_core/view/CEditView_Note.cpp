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
#include "view/NoteImageGeometry.h"
#include "dlg/CDlgInput1.h"
#include "cmd/COpeBlk.h"

namespace {
bool FitBlock(NoteView::Block& block, int width, int height, UINT dpi)
{
	const int rows = block.failed ? 3 : NoteView::FitImage(block.sourceWidth, block.sourceHeight,
		width, height, block.maxRows, dpi).rows;
	return std::exchange(block.rows, rows) != rows;
}

std::vector<NoteView::Block> ScanBlocks(CEditDoc& doc, CLogicInt first, CLogicInt last,
	const NoteView::ImageCache& cache)
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
		if (link) {
			const auto source = cache.GetSourceInfo(link->path);
			blocks.push_back({static_cast<Int>(logical), row - 1, std::move(link->path), link->maxRows, link->maxRows,
				source.width, source.height, source.state == NoteView::ImageCache::State::Failed});
		}
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
	const auto total = area.GetNoteProjection().FromLayout(static_cast<int>(GetDisplayLineCount())).value;
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
	for (int i = 0; i < GetAllViewCount(); ++i) {
		GetView(i).CancelNoteResize();
		GetView(i).OnBeforeViewLayout();
	}
	m_noteImageCache.Reset();
	m_noteView = enabled;
	for (int i = 0; i < GetAllViewCount(); ++i) GetView(i).ApplyNoteView();
}

void CEditView::OnBeforeViewLayout()
{
	if (m_bMiniMap || m_noteLoading || m_noteAnchor.valid) return;
	const auto& area = GetTextArea();
	m_noteAnchor.valid = true;
	m_noteAnchor.relativeRow = area.RelativeRow(GetDisplayCaretLayoutPos().y);
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
			DisplayToDocumentLayoutLine(CLayoutInt(hit.layoutLine + (hit.block ? 1 : 0)))), &m_noteAnchor.text);
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
	m_noteImeBaseProjection.reset();
	auto& projection = GetTextArea().GetNoteProjection();
	if (IsNoteView()) {
		auto blocks = ScanBlocks(*GetDocument(), CLogicInt(0), GetDocument()->m_cDocLineMgr.GetLineCount(), GetEditWnd().GetNoteImageCache());
		PrepareNoteBlocks(blocks);
		projection.Reset(std::move(blocks));
	}
	else projection.Clear();
	RefreshNoteImeProjection();
}

void CEditView::RestoreNoteDocumentProjection()
{
	if (m_noteImeBaseProjection) {
		GetTextArea().GetNoteProjection() = std::move(*m_noteImeBaseProjection);
		m_noteImeBaseProjection.reset();
		InvalidateNoteRequests();
	}
}

void CEditView::RefreshNoteImeProjection()
{
	RestoreNoteDocumentProjection();
	if (!IsNoteView() || !IsImeInlineDisplay() || !m_imeLayout || !m_imeLine) return;
	auto& projection = GetTextArea().GetNoteProjection();
	m_noteImeBaseProjection = projection;
	std::vector<NoteView::Block> replacement;
	const auto link = ImageLink::FindStandalone(std::wstring_view(m_imeLine->GetPtr(),
		static_cast<size_t>(m_imeLine->GetLengthWithoutEOL())));
	const auto count = m_imeLayout->GetLineCount();
	if (link && count > 0) {
		const auto source = GetEditWnd().GetNoteImageCache().GetSourceInfo(link->path);
		replacement.push_back({static_cast<int>(m_ime.begin.y),
			static_cast<std::int64_t>(static_cast<int>(m_ime.firstLayoutLine)) + static_cast<int>(count) - 1, link->path,
			link->maxRows, link->maxRows, source.width, source.height, source.state == NoteView::ImageCache::State::Failed});
	}
	// Logical anchors remain document positions; only the displayed text rows are replaced.
	// This hides links covered by an IME replacement selection and shifts subsequent blocks.
	PrepareNoteBlocks(replacement);
	projection.Replace(m_ime.begin.y, m_ime.end.y, 0,
		static_cast<int>(count) - static_cast<int>(m_ime.originalLayoutLines), std::move(replacement));
	InvalidateNoteRequests();
}

void CEditView::OnAfterViewLayout(const SViewLayoutChange& change)
{
	if (m_bMiniMap) return;
	if (m_noteResize) CancelNoteResize();
	auto& area = GetTextArea();
	bool changed;
	if (IsNoteView() && change.firstLine >= 0) {
		const bool hadImeProjection = m_noteImeBaseProjection.has_value();
		RestoreNoteDocumentProjection();
		auto replacement = ScanBlocks(*GetDocument(), change.firstLine, change.newLastLine, GetEditWnd().GetNoteImageCache());
		PrepareNoteBlocks(replacement);
		changed = area.GetNoteProjection().Replace(change.firstLine, change.oldLastLine, change.logicalDelta,
			static_cast<Int>(change.layoutDelta), std::move(replacement));
		RefreshNoteImeProjection();
		changed = changed || hadImeProjection || m_noteImeBaseProjection.has_value();
	} else {
		const auto previous = area.GetNoteProjection().Blocks();
		RebuildNoteProjection();
		changed = previous != area.GetNoteProjection().Blocks();
	}
	if (changed || change.firstLine < 0) InvalidateNoteRequests();
	if (m_noteAnchor.valid && (changed || (IsNoteView() && change.firstLine < 0))) {
		RestoreNoteViewportAnchor(change);
	}
	m_noteAnchor.valid = false;
	if (changed && GetHwnd()) {
		AdjustScrollBars();
		InvalidateRect(nullptr, FALSE);
	}
}

void CEditView::PrepareNoteBlocks(std::vector<NoteView::Block>& blocks) const
{
	if (blocks.empty()) return;
	const auto& area = GetTextArea();
	const auto& old = area.GetNoteProjection().Blocks();
	auto previous = std::lower_bound(old.begin(), old.end(), blocks.front().logicalLine,
		[](const auto& block, auto logicalLine) { return block.logicalLine < logicalLine; });
	const int height = GetTextMetrics().GetHankakuDy();
	const UINT dpi = GetHwnd() ? ::GetDpiForWindow(GetHwnd()) : 96;
	for (auto& block : blocks) {
		while (previous != old.end() && previous->logicalLine < block.logicalLine) ++previous;
		if (previous != old.end() && previous->logicalLine == block.logicalLine && previous->path == block.path &&
			!block.sourceWidth && !block.failed) {
			block.sourceWidth = previous->sourceWidth;
			block.sourceHeight = previous->sourceHeight;
			block.failed = previous->failed;
		}
		FitBlock(block, area.GetAreaWidth(), height, dpi);
	}
}

void CEditView::RefitNoteImages()
{
	auto& area = GetTextArea();
	const int height = GetTextMetrics().GetHankakuDy();
	const UINT dpi = GetHwnd() ? ::GetDpiForWindow(GetHwnd()) : 96;
	const auto fit = [&](auto& block) { return FitBlock(block, area.GetAreaWidth(), height, dpi); };
	area.GetNoteProjection().UpdateBlocks(fit);
	if (m_noteImeBaseProjection) m_noteImeBaseProjection->UpdateBlocks(fit);
}

void CEditView::RestoreNoteViewportAnchor(const SViewLayoutChange& change)
{
	auto& area = GetTextArea();
	CLayoutPoint text;
	auto anchor = m_noteAnchor.text;
	if (change.firstLine >= 0 && anchor.y > change.oldLastLine) anchor.y += change.logicalDelta;
	else if (change.firstLine >= 0 && !m_noteAnchor.caretVisible && anchor.y > change.firstLine)
		anchor.y = change.newLastLine;
	GetDocument()->m_cLayoutMgr.LogicToLayout(anchor, &text);
	text.y = m_noteAnchor.caretVisible && IsImeInlineDisplay()
		? GetDisplayCaretLayoutPos().y : DocumentToDisplayLayoutLine(text.y);
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
				top = area.GetNoteProjection().BlockStart(i).value + std::min(m_noteAnchor.blockOffset, blocks[i].rows - 1);
				break;
			}
		}
	}
	const auto maxTop = std::max<std::int64_t>(0, area.GetNoteProjection().FromLayout(
		static_cast<int>(GetDisplayLineCount())).value + 2 - static_cast<Int>(area.m_nViewRowNum));
	area.SetDisplayTop(NoteView::DisplayRow(std::clamp<std::int64_t>(top, 0, maxTop)));
}

void CEditView::SaveNoteReflowAnchor()
{
	m_noteReflowAnchor.reset();
	if (!IsNoteView() || (!HasNoteBlocks() &&
		(!m_noteImeBaseProjection || m_noteImeBaseProjection->Blocks().empty()))) return;
	const auto previous = m_noteAnchor;
	m_noteAnchor.valid = false;
	OnBeforeViewLayout();
	if (m_noteAnchor.valid) m_noteReflowAnchor = m_noteAnchor;
	m_noteAnchor = previous;
}

bool CEditView::RestoreNoteReflowAnchor()
{
	if (!m_noteReflowAnchor) return false;
	const auto saved = std::move(*m_noteReflowAnchor);
	m_noteReflowAnchor.reset();
	if (!IsNoteView()) return false;
	const auto previous = m_noteAnchor;
	m_noteAnchor = saved;
	RestoreNoteViewportAnchor({});
	m_noteAnchor = previous;
	return true;
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
		if (row + projection.Blocks()[i].rows <= 0) continue;
		if (row > static_cast<int>(area.m_nViewRowNum)) break;
		::ExcludeClipRect(dc, 0, area.GetAreaTop() + static_cast<int>(row) * height, area.GetAreaRight(),
			area.GetAreaTop() + static_cast<int>(row + projection.Blocks()[i].rows) * height);
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
		if (row + blocks[i].rows <= 0) continue;
		if (row > static_cast<Int>(area.m_nViewRowNum)) break;
		RECT rect{0, area.GetAreaTop() + static_cast<int>(row) * height, area.GetAreaRight(),
			area.GetAreaTop() + static_cast<int>(row + blocks[i].rows) * height};
		RECT intersection{};
		if (!::IntersectRect(&intersection, &clip, &rect)) continue;
		const auto brush = ::CreateSolidBrush(CTypeSupport(this, COLORIDX_TEXT).GetBackColor());
		::FillRect(dc, &rect, brush);
		::DeleteObject(brush);
		if (area.GetAreaLeft() > 0 && m_pTypeData->m_ColorInfoArr[COLORIDX_GYOU].m_bDisp) {
			const auto gutter = CTypeSupport(this, COLORIDX_GYOU).GetBackColor();
			RECT band{0, rect.top, area.GetAreaLeft(), rect.bottom};
			const auto gutterBrush = ::CreateSolidBrush(gutter);
			::FillRect(dc, &band, gutterBrush);
			::DeleteObject(gutterBrush);
			const auto marker = ::CreatePen(PS_SOLID, 2, CTypeSupport(this, COLORIDX_GYOU).GetTextColor());
			const auto oldPen = ::SelectObject(dc, marker);
			const auto oldBrush = ::SelectObject(dc, ::GetStockObject(NULL_BRUSH));
			const int x = area.GetAreaLeft() - 14, y = std::max<int>(rect.top, area.GetAreaTop()) + 7;
			if (y + 9 < area.GetAreaBottom()) {
				::Rectangle(dc, x, y, x + 9, y + 8);
				::MoveToEx(dc, x + 1, y + 6, nullptr);
				::LineTo(dc, x + 4, y + 3);
				::LineTo(dc, x + 8, y + 7);
			}
			::SelectObject(dc, oldPen);
			::SelectObject(dc, oldBrush);
			::DeleteObject(marker);
		}
		rect.left = area.GetAreaLeft();
		if (rect.left >= rect.right || rect.top >= rect.bottom) continue;
		::SetTextColor(dc, CTypeSupport(this, COLORIDX_TEXT).GetTextColor());
		::SetBkMode(dc, TRANSPARENT);
		const UINT dpi = GetHwnd() ? ::GetDpiForWindow(GetHwnd()) : 96;
		const int cap = ::MulDiv(640, dpi ? dpi : 96, 96);
		const int padding = std::min(::MulDiv(8, dpi ? dpi : 96, 96), blocks[i].maxRows * height - 1);
		NoteView::ImageKey key{blocks[i].path, static_cast<UINT>(std::max(1, std::min<int>(rect.right - rect.left, cap))),
			static_cast<UINT>(std::max(1, std::min<int>(blocks[i].maxRows * height - padding, cap)))};
		auto result = GetEditWnd().GetNoteImageCache().Get(key);
		std::erase_if(m_notePending, [&](const auto& pending) { return pending.logicalLine == blocks[i].logicalLine; });
		const bool needsGeometry = result.state == NoteView::ImageCache::State::Ready
			? blocks[i].failed || blocks[i].sourceWidth != result.image->sourceWidth || blocks[i].sourceHeight != result.image->sourceHeight
			: result.state == NoteView::ImageCache::State::Failed && !blocks[i].failed;
		if (needsGeometry || result.state == NoteView::ImageCache::State::Loading)
			m_notePending.push_back({blocks[i].logicalLine, key, m_noteGeneration});
		if (result.state == NoteView::ImageCache::State::Ready) {
			const auto& image = *result.image;
			BITMAPINFO info{};
			info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			info.bmiHeader.biWidth = image.width;
			info.bmiHeader.biHeight = -static_cast<LONG>(image.height);
			info.bmiHeader.biPlanes = 1;
			info.bmiHeader.biBitCount = 32;
			info.bmiHeader.biCompression = BI_RGB;
			// An updated image may have a different aspect ratio. Clip to the old block
			// until the timer atomically updates geometry and the viewport anchor.
			const int imageDC = ::SaveDC(dc);
			::IntersectClipRect(dc, rect.left, rect.top, rect.right, rect.bottom);
			::SetDIBitsToDevice(dc, rect.left, rect.top + (rect.bottom - rect.top - static_cast<int>(image.height)) / 2,
				image.width, image.height, 0, 0, 0, image.height, image.pixels.data(), &info, DIB_RGB_COLORS);
			::RestoreDC(dc, imageDC);
		} else {
			const bool loading = result.state == NoteView::ImageCache::State::Loading;
			::DrawTextW(dc, LS(loading ? STR_NOTE_LOADING : STR_NOTE_ERROR), -1, &rect,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		}
		if (blocks[i].sourceWidth && !blocks[i].failed && rect.bottom <= area.GetAreaBottom()) {
			const int handle = std::max(8, ::MulDiv(12, dpi ? dpi : 96, 96));
			RECT knob{rect.right - handle, rect.bottom - handle, rect.right, rect.bottom};
			::DrawFrameControl(dc, &knob, DFC_SCROLL, DFCS_SCROLLSIZEGRIP);
		}
		if (m_noteResize && m_noteResize->line == blocks[i].logicalLine && m_noteResize->path == blocks[i].path) {
			const int bottom = rect.top + m_noteResize->candidate * height;
			const auto pen = ::CreatePen(PS_DOT, 1, CTypeSupport(this, COLORIDX_TEXT).GetTextColor());
			const auto oldPen = ::SelectObject(dc, pen);
			::MoveToEx(dc, rect.left, bottom, nullptr);
			::LineTo(dc, rect.right, bottom);
			::SelectObject(dc, oldPen);
			::DeleteObject(pen);
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
	CancelNoteResize();
	m_noteLoading = true;
	m_noteAnchor.valid = false;
	InvalidateNoteRequests();
	GetEditWnd().GetNoteImageCache().Reset();
}

void CEditView::PollNoteImages()
{
	if (!IsNoteView() || m_noteLoading || !GetHwnd() || !GetDrawSwitch()) return;
	const auto pending = m_notePending; // Painting updates the pending list.
	auto& area = GetTextArea();
	auto& projection = area.GetNoteProjection();
	for (const auto& request : pending) {
		if (request.generation != m_noteGeneration) continue;
		const auto& blocks = projection.Blocks();
		auto block = std::lower_bound(blocks.begin(), blocks.end(), request.logicalLine,
			[](const auto& b, auto line) { return b.logicalLine < line; });
		if (block == blocks.end() || block->logicalLine != request.logicalLine || block->path != request.key.path) continue;
		const auto row = projection.BlockStart(block - blocks.begin()).value - area.GetDisplayTop().value;
		if (row + block->rows <= 0 || row > static_cast<int>(area.m_nViewRowNum)) continue;
		const auto result = GetEditWnd().GetNoteImageCache().Get(request.key);
		if (result.state == NoteView::ImageCache::State::Loading) continue;
		if ((result.state == NoteView::ImageCache::State::Ready &&
			(block->sourceWidth != result.image->sourceWidth || block->sourceHeight != result.image->sourceHeight || block->failed)) ||
			(result.state == NoteView::ImageCache::State::Failed && !block->failed)) {
			OnBeforeViewLayout();
			// Update every reference to this image in this pane, then rebuild cumulative offsets.
			const int height = GetTextMetrics().GetHankakuDy();
			const UINT dpi = ::GetDpiForWindow(GetHwnd());
			const auto update = [&](auto& b) {
				if (b.path != request.key.path) return false;
				b.failed = result.state == NoteView::ImageCache::State::Failed;
				b.sourceWidth = b.failed ? 0 : result.image->sourceWidth;
				b.sourceHeight = b.failed ? 0 : result.image->sourceHeight;
				FitBlock(b, area.GetAreaWidth(), height, dpi);
				return true;
			};
			projection.UpdateBlocks(update);
			if (m_noteImeBaseProjection) m_noteImeBaseProjection->UpdateBlocks(update);
			RestoreNoteViewportAnchor({});
			m_noteAnchor.valid = false;
			AdjustScrollBars();
			InvalidateRect(nullptr, FALSE);
			GetCaret().ShowEditCaret();
			return;
		}
		const auto height = GetTextMetrics().GetHankakuDy();
		RECT rect{area.GetAreaLeft(), area.GetAreaTop() + static_cast<int>(row) * height, area.GetAreaRight(),
			area.GetAreaTop() + static_cast<int>(row + block->rows) * height};
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

bool CEditView::IsNoteResizeHandle(POINT point) const
{
	if (!IsNoteBlockPoint(point)) return false;
	const auto& area = GetTextArea();
	const auto* block = area.HitClientRow(point.y).block;
	if (!block || !block->sourceWidth || block->failed || !CanChangeNoteRows(block->logicalLine)) return false;
	const int height = GetTextMetrics().GetHankakuDy();
	const int size = std::max(8, ::MulDiv(12, GetHwnd() ? ::GetDpiForWindow(GetHwnd()) : 96, 96));
	const auto index = block - area.GetNoteProjection().Blocks().data();
	const auto bottom = area.GetAreaTop() + static_cast<int>(area.GetNoteProjection().BlockStart(index).value -
		area.GetDisplayTop().value + block->rows) * height;
	return bottom <= area.GetAreaBottom() && point.y >= bottom - size && point.y < bottom &&
		point.x >= area.GetAreaRight() - size;
}

void CEditView::CancelNoteResize()
{
	if (!m_noteResize) return;
	m_noteResize.reset();
	if (GetHwnd() && ::GetCapture() == GetHwnd()) ::ReleaseCapture();
	if (GetHwnd()) InvalidateRect(nullptr, FALSE);
}

void CEditView::OpenNoteBlockInPaint(const std::wstring& path)
{
	std::wstring nativePath(path);
	std::replace(nativePath.begin(), nativePath.end(), L'/', L'\\');
	if (!ImageLink::IsReadableFile(nativePath)) {
		::MessageBoxW(GetHwnd(), L"画像ファイルが見つかりません。", L"画像", MB_OK | MB_ICONWARNING);
		return;
	}
	const auto args = L"\"" + nativePath + L"\"";
	if (reinterpret_cast<INT_PTR>(::ShellExecuteW(GetHwnd(), L"open", L"mspaint.exe", args.c_str(), nullptr, SW_SHOWNORMAL)) <= 32)
		::MessageBoxW(GetHwnd(), L"ペイントを起動できませんでした。", L"画像", MB_OK | MB_ICONWARNING);
}

void CEditView::ShowNoteBlockMenu(POINT point)
{
	const auto* block = GetTextArea().HitClientRow(point.y).block;
	if (!block) return;
	const auto path = block->path;
	const auto logicalLine = block->logicalLine;
	const auto maxRows = block->maxRows;
	const auto actualRows = block->rows;
	const bool editable = CanChangeNoteRows(logicalLine);
	const HMENU menu = ::CreatePopupMenu();
	if (!menu) return;
	::AppendMenuW(menu, MF_STRING | (ImageLink::IsReadableFile(path) ? 0 : MF_GRAYED), 1, L"ペイントで開く");
	::AppendMenuW(menu, MF_STRING, 2, L"画像を更新");
	::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(menu, MF_STRING | (editable ? 0 : MF_GRAYED), 3, L"高さを変更...");
	::AppendMenuW(menu, MF_STRING, 4, L"画像のパスをコピー");
	::ClientToScreen(GetHwnd(), &point);
	const auto choice = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, GetHwnd(), nullptr);
	::DestroyMenu(menu);
	if (choice == 1) OpenNoteBlockInPaint(path);
	if (choice == 2) {
		GetEditWnd().GetNoteImageCache().Invalidate(path);
		for (int i = 0; i < GetEditWnd().GetAllViewCount(); ++i) GetEditWnd().GetView(i).RefreshNoteImage(path);
	}
	if (choice == 3 && editable) {
		wchar_t input[16]{};
		swprintf_s(input, L"%d", maxRows);
		int rows = maxRows;
		const auto message = L"現在の表示：" + std::to_wstring(actualRows) + L"行／設定上限：" +
			std::to_wstring(maxRows) + L"行\n表示高さの上限（3～40行）";
		if (CDlgInput1().DoModal(GetHwnd(), L"画像の高さを変更", message, std::span(input),
			[&rows](HWND dialog, std::wstring_view value, size_t) {
				if (const auto parsed = ImageLink::ParseRowLimit(value)) { rows = *parsed; return 1; }
				::MessageBoxW(dialog, L"3～40の整数を入力してください。", L"画像の高さ", MB_OK | MB_ICONWARNING);
				return -1;
			})) ChangeNoteRows(logicalLine, path, rows);
	}
	if (choice == 4 && ::OpenClipboard(GetHwnd())) {
		if (::EmptyClipboard()) {
			const auto bytes = (path.size() + 1) * sizeof(wchar_t);
			const auto memory = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
			if (memory) {
				if (auto data = ::GlobalLock(memory)) {
					memcpy(data, path.c_str(), bytes);
					::GlobalUnlock(memory);
					if (!::SetClipboardData(CF_UNICODETEXT, memory)) ::GlobalFree(memory);
				} else ::GlobalFree(memory);
			}
		}
		::CloseClipboard();
	}
}

void CEditView::RefreshNoteImage(const std::wstring& path)
{
	if (!IsNoteView()) return;
	const auto& blocks = GetTextArea().GetNoteProjection().Blocks();
	if (std::none_of(blocks.begin(), blocks.end(), [&](const auto& b) { return b.path == path; })) return;
	// Keep the last known aspect ratio and row count until the new asynchronous result arrives.
	InvalidateNoteRequests();
	if (GetHwnd()) InvalidateRect(nullptr, FALSE);
}

bool CEditView::ChangeNoteRows(std::int64_t logicalLine, const std::wstring& path, int rows)
{
	if (rows < 3 || rows > 40 || !CanChangeNoteRows(logicalLine)) return false;
	auto* line = GetDocument()->m_cDocLineMgr.GetLine(CLogicInt(logicalLine));
	if (!line) return false;
	const std::wstring old(line->GetPtr(), line->GetLengthWithoutEOL());
	const auto link = ImageLink::FindStandalone(old);
	if (!link || link->path != path) return false;
	if (rows == link->maxRows && link->rowsValid) return false;
	const int begin = static_cast<int>(link->end);
	const int end = static_cast<int>(old.size());
	const std::wstring annotation = rows == 12 ? L"" : L" <!-- sakura:rows=" + std::to_wstring(rows) + L" -->";
	struct PanePosition {
		CEditView* view;
		CLogicPoint caret;
		CViewSelect selection;
		CLogicRange range, origin;
		NoteAnchor viewport;
		CLayoutInt left;
	};
	std::vector<PanePosition> positions;
	for (int i = 0; i < GetEditWnd().GetAllViewCount(); ++i) {
		auto& pane = GetEditWnd().GetView(i);
		if (pane.m_bMiniMap) continue;
		pane.OnBeforeViewLayout();
		PanePosition saved{&pane, pane.GetCaret().GetCaretLogicPos(), pane.GetSelectionInfo(), {}, {},
			pane.m_noteAnchor, pane.GetTextArea().GetViewLeftCol()};
		if (saved.selection.IsTextSelected()) {
			GetDocument()->m_cLayoutMgr.LayoutToLogic(saved.selection.m_sSelect, &saved.range);
			GetDocument()->m_cLayoutMgr.LayoutToLogic(saved.selection.m_sSelectBgn, &saved.origin);
		}
		positions.push_back(saved);
	}
	const CLogicRange range(CLogicPoint(begin, CLogicInt(logicalLine)), CLogicPoint(end, CLogicInt(logicalLine)));
	auto* undo = GetCommander().GetOpeBlk();
	if (!undo) { undo = new COpeBlk; GetCommander().SetOpeBlk(undo); }
	undo->AddRef();
	GetDocument()->m_cDocEditor.SetModified(true, true);
	ReplaceData_CEditView2(range, annotation.c_str(), CLogicInt(static_cast<int>(annotation.size())), true, undo);
	SetUndoBuffer(true);
	const int delta = static_cast<int>(annotation.size()) - (end - begin);
	const auto adjust = [&](CLogicPoint& point) {
		if (point.y == logicalLine) {
			if (point.x >= end) point.x += delta;
			else if (point.x > begin) point.x = begin + static_cast<int>(annotation.size());
		}
	};
	for (auto& saved : positions) {
		adjust(saved.caret);
		CLayoutPoint position;
		GetDocument()->m_cLayoutMgr.LogicToLayout(saved.caret, &position);
		saved.view->GetCaret().MoveCursor(position, false);
		saved.view->GetCaret().m_nCaretPosX_Prev = position.x;
		auto& selection = saved.view->GetSelectionInfo();
		selection = saved.selection;
		if (saved.selection.IsTextSelected()) {
			adjust(*saved.range.GetFromPointer());
			adjust(*saved.range.GetToPointer());
			adjust(*saved.origin.GetFromPointer());
			adjust(*saved.origin.GetToPointer());
			GetDocument()->m_cLayoutMgr.LogicToLayout(saved.range, &selection.m_sSelect);
			GetDocument()->m_cLayoutMgr.LogicToLayout(saved.origin, &selection.m_sSelectBgn);
		}
		selection.m_sSelectOld = selection.m_sSelect;
		// ReplaceData moves the caret to the annotation. Restore the viewport as well
		// as the logical caret, so a distant selection cannot cause a scroll jump.
		adjust(saved.viewport.text);
		saved.view->m_noteAnchor = saved.viewport;
		saved.view->RestoreNoteViewportAnchor({});
		saved.view->m_noteAnchor.valid = false;
		saved.view->GetTextArea().SetViewLeftCol(saved.left);
		if (saved.view->GetHwnd()) {
			saved.view->AdjustScrollBars();
			saved.view->InvalidateRect(nullptr, FALSE);
			saved.view->GetCaret().ShowEditCaret();
		}
	}
	return true;
}

bool CEditView::CanChangeNoteRows(std::int64_t logicalLine) const
{
	if (!IsNoteView() || !GetDocument()->IsEditable()) return false;
	const auto line = GetDocument()->m_cDocLineMgr.GetLine(CLogicInt(logicalLine));
	if (!line) return false;
	const auto link = ImageLink::FindStandalone(std::wstring_view(line->GetPtr(), line->GetLengthWithoutEOL()));
	if (!link) return false;
	for (int i = 0; i < GetEditWnd().GetAllViewCount(); ++i) {
		const auto& pane = GetEditWnd().GetView(i);
		const auto& selection = pane.GetSelectionInfo();
		if (selection.IsBoxSelecting()) return false;
		if (!selection.IsTextSelected()) continue;
		CLogicPoint first, last;
		GetDocument()->m_cLayoutMgr.LayoutToLogic(selection.m_sSelect.GetFrom(), &first);
		GetDocument()->m_cLayoutMgr.LayoutToLogic(selection.m_sSelect.GetTo(), &last);
		if (first.y <= logicalLine && last.y >= logicalLine &&
			(first.y < logicalLine || first.x < line->GetLengthWithoutEOL()) &&
			(last.y > logicalLine || last.x > link->end)) return false;
	}
	return true;
}

std::wstring CEditView::PrepareStandaloneNoteInsertion(std::wstring text, bool& reuseEol)
{
	// Freeboards are standalone in both modes, and always leave a text position
	// after the image. Reuse an existing delimiter rather than adding a blank line.
	CLogicPoint first, last;
	if (GetSelectionInfo().IsTextSelected()) {
		GetDocument()->m_cLayoutMgr.LayoutToLogic(GetSelectionInfo().m_sSelect.GetFrom(), &first);
		GetDocument()->m_cLayoutMgr.LayoutToLogic(GetSelectionInfo().m_sSelect.GetTo(), &last);
		if (first.y > last.y || (first.y == last.y && first.x > last.x)) std::swap(first, last);
	} else first = last = GetCaret().GetCaretLogicPos();
	const auto eol = GetDocument()->m_cDocEditor.GetNewLineCode().GetValue2();
	const auto before = GetDocument()->m_cDocLineMgr.GetLine(first.y);
	if (before && std::wstring_view(before->GetPtr(), std::clamp<int>(first.x, 0, before->GetLengthWithoutEOL()))
		.find_first_not_of(L" \t") != std::wstring_view::npos) text.insert(0, eol);
	const auto after = GetDocument()->m_cDocLineMgr.GetLine(last.y);
	const auto suffix = after ? std::wstring_view(after->GetPtr(), after->GetLengthWithoutEOL()).substr(
		std::clamp<int>(last.x, 0, after->GetLengthWithoutEOL())) : std::wstring_view{};
	reuseEol = after && after->GetLengthWithEOL() > after->GetLengthWithoutEOL() &&
		suffix.find_first_not_of(L" \t") == std::wstring_view::npos;
	if (!reuseEol) text += eol;
	return text;
}

bool CEditView::InsertStandaloneNoteLink(const std::wstring& link)
{
	if (!GetDocument()->IsEditable() || GetSelectionInfo().IsBoxSelecting() ||
		GetSelectionInfo().IsMouseSelecting() || !ImageLink::FindStandalone(link)) return false;
	bool reuseEol;
	const auto text = PrepareStandaloneNoteInsertion(link, reuseEol);
	auto* undo = GetCommander().GetOpeBlk();
	if (!undo) { undo = new COpeBlk; GetCommander().SetOpeBlk(undo); }
	undo->AddRef();
	struct UndoScope {
		CEditView& view;
		~UndoScope() { view.SetUndoBuffer(true); }
	} scope{*this};
	GetCommander().Command_INSTEXT(false, text.c_str(), text.size(), true);
	if (reuseEol) {
		const auto before = GetCaret().GetCaretLogicPos();
		CLayoutPoint after;
		GetDocument()->m_cLayoutMgr.LogicToLayout(CLogicPoint(0, before.y + 1), &after);
		GetCaret().MoveCursor(after, false);
		undo->AppendOpe(new CMoveCaretOpe(before, GetCaret().GetCaretLogicPos()));
	}
	GetCaret().m_nCaretPosX_Prev = GetCaret().GetCaretLayoutPos().x;
	return true;
}
