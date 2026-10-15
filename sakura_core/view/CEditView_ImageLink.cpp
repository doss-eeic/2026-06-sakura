/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "view/CEditView.h"
#include "window/CEditWnd.h"
#include "util/ImageLink.h"

void CEditView::CancelImagePreview()
{
	m_imageHover.reset();
	m_imageRequested = false;
	m_imagePreview.Hide();
}

void CEditView::UpdateImagePreview(bool mouseMoved)
{
	POINT screen{};
	::GetCursorPos(&screen);
	const bool moved = screen.x != m_imageMousePoint.x || screen.y != m_imageMousePoint.y;
	if (mouseMoved) m_imageMousePoint = screen;
	if (m_bMiniMap || m_bHokan || m_bDragMode || IsDragSource() || m_nAutoScrollMode ||
		GetSelectionInfo().IsMouseSelecting() || ::GetCapture() ||
		(::GetAsyncKeyState(VK_LBUTTON) & 0x8000) || (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) ||
		(::GetAsyncKeyState(VK_MBUTTON) & 0x8000) || ::GetFocus() != GetHwnd() ||
		::GetForegroundWindow() != GetEditWnd().GetHwnd() || ::WindowFromPoint(screen) != GetHwnd()) {
		CancelImagePreview();
		return;
	}
	if (m_imageHover && m_imageCommandNumber != GetDocument()->m_nCommandExecNum) {
		CancelImagePreview();
		return;
	}
	POINT client = screen;
	::ScreenToClient(GetHwnd(), &client);
	const RECT area = GetTextArea().GetAreaRect();
	if (!::PtInRect(&area, client)) { CancelImagePreview(); return; }
	CLayoutPoint layoutPoint;
	GetTextArea().ClientToLayout(CMyPoint(client.x, client.y), &layoutPoint);
	const auto layout = GetDocument()->m_cLayoutMgr.SearchLineByLayoutY(layoutPoint.y);
	if (!layout || layoutPoint.x < layout->GetIndent()) { CancelImagePreview(); return; }
	const auto index = LineColumnToIndex(layout, layoutPoint.x);
	// Do not clamp empty space, wrap indentation, or EOL to the last link character.
	if (index < 0 || index >= layout->GetLengthWithoutEOL() ||
		(client.y - area.top) % GetTextMetrics().GetHankakuDy() >= GetTextMetrics().GetHankakuHeight()) {
		CancelImagePreview();
		return;
	}
	const auto line = layout->GetDocLineRef();
	auto link = ImageLink::FindAt(std::wstring_view(line->GetPtr(), static_cast<size_t>(line->GetLengthWithoutEOL())),
		static_cast<size_t>(layout->GetLogicOffset() + index));
	if (!link) { CancelImagePreview(); return; }
	const bool same = m_imageHover && m_imageHoverLine == layout->GetLogicLineNo() &&
		m_imageHover->begin == link->begin && m_imageHover->end == link->end && m_imageHover->path == link->path;
	if (!same) {
		CancelImagePreview();
		// A timer or keyboard caret motion must not start a new hover.
		if (!mouseMoved || !moved) return;
		m_imageHover = std::move(link);
		m_imageHoverLine = layout->GetLogicLineNo();
		m_imageHoverStarted = ::GetTickCount();
		m_imageCommandNumber = GetDocument()->m_nCommandExecNum;
	}
	m_cTipWnd.Hide();
	m_dwTipTimer = ::GetTickCount();
	if (!m_imageRequested && ::GetTickCount() - m_imageHoverStarted >= 500) {
		m_imageRequested = true;
		m_imagePreview.Begin(GetHwnd(), m_imageHover->path, screen);
	}
	m_imagePreview.Poll();
}

bool CEditView::TryInsertImageFiles(HDROP hDrop, POINT clientPoint)
{
	const RECT area = GetTextArea().GetAreaRect();
	if (m_bMiniMap || !::PtInRect(&area, clientPoint)) return false;
	const UINT count = ::DragQueryFileW(hDrop, 0xffffffff, nullptr, 0);
	if (!count) return false;
	std::vector<std::wstring> paths;
	for (UINT i = 0; i < count; ++i) {
		const UINT length = ::DragQueryFileW(hDrop, i, nullptr, 0);
		std::wstring path(length + 1, L'\0');
		::DragQueryFileW(hDrop, i, path.data(), length + 1);
		path.resize(length);
		const DWORD attributes = ::GetFileAttributesW(path.c_str());
		if (!ImageLink::HasImageExtension(path) || path.size() < 3 || path[1] != L':' ||
			(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))) return false;
		paths.push_back(std::move(path));
	}
	if (!GetDocument()->IsEditable() || GetSelectionInfo().IsBoxSelecting()) return true;
	std::wstring text;
	for (const auto& path : paths) {
		const auto link = ImageLink::MakeLink(path);
		if (!link || !ImageLink::IsReadableFile(path)) return true;
		if (!text.empty()) text += GetDocument()->m_cDocEditor.GetNewLineCode().GetValue2();
		text += *link;
	}
	GetEditWnd().SetActivePane(m_nMyIndex);
	GetSelectionInfo().DisableSelectArea(true);
	GetCaret().MoveCursorToClientPoint(CMyPoint(clientPoint.x, clientPoint.y));
	GetCommander().HandleCommand(F_INSTEXT_W, true, reinterpret_cast<LPARAM>(text.c_str()), text.size(), TRUE, 0);
	return true;
}
