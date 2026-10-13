/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "dlg/CDlgFontFallback.h"
#include "util/shell.h"
#include "util/window.h"
#include "util/os.h"
#include "CSelectLang.h"
#include <cmath>
#include "sakura_rc.h"

bool CDlgFontFallback::DoModal(HINSTANCE instance, HWND parent, CommonSetting_View& settings)
{
	return DoModal(instance, parent, settings.m_lf, settings.m_nPointSize, settings.m_fallbackFonts);
}

bool CDlgFontFallback::DoModal(HINSTANCE instance, HWND parent, LOGFONT& font, INT& pointSize, SFontFallbacks& fallbacks)
{
	m_settings.m_lf = font;
	m_settings.m_nPointSize = pointSize;
	m_settings.m_fallbackFonts = fallbacks;
	if (CDialog::DoModal(instance, parent, IDD_FONT_FALLBACK, 0) != IDOK) return false;
	font = m_settings.m_lf;
	pointSize = m_settings.m_nPointSize;
	fallbacks = m_settings.m_fallbackFonts;
	return true;
}

void CDlgFontFallback::SetData()
{
	m_displayPointSize = m_settings.m_nPointSize;
	if (m_displayPointSize <= 0) m_displayPointSize = std::max<LONG>(10, std::abs(DpiPixelsToPoints(m_settings.m_lf.lfHeight, 10)));
	wchar_t size[32];
	::swprintf_s(size, L"%d.%d", m_displayPointSize / 10, m_displayPointSize % 10);
	::SetDlgItemTextW(GetHwnd(), IDC_EDIT_FONT_SIZE, size);
	::SendDlgItemMessageW(GetHwnd(), IDC_EDIT_FONT_SIZE, EM_SETLIMITTEXT, 10, 0);
	HWND list = GetItemHwnd(IDC_LIST_FALLBACK_FONTS);
	::SendMessageW(list, LB_RESETCONTENT, 0, 0);
	::SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(m_settings.m_lf.lfFaceName));
	for (const auto& name : m_settings.m_fallbackFonts) {
		if (name[0] != L'\0') ::SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.data()));
	}
	::SendMessageW(list, LB_SETCURSEL, 0, 0);
	UpdateButtons();
}

void CDlgFontFallback::ReadList()
{
	m_settings.m_fallbackFonts = {};
	HWND list = GetItemHwnd(IDC_LIST_FALLBACK_FONTS);
	const int count = static_cast<int>(::SendMessageW(list, LB_GETCOUNT, 0, 0));
	wchar_t first[LF_FACESIZE]{};
	::SendMessageW(list, LB_GETTEXT, 0, reinterpret_cast<LPARAM>(first));
	if (::wcscmp(first, m_settings.m_lf.lfFaceName) != 0) {
		LOGFONT& lf = m_settings.m_lf;
		::wcscpy_s(lf.lfFaceName, first);
		// A reordered family must not inherit the old family's width or character set.
		lf.lfWidth = 0;
		lf.lfCharSet = DEFAULT_CHARSET;
		lf.lfPitchAndFamily = DEFAULT_PITCH;
		lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
		HFONT font = ::CreateFontIndirectW(&lf);
		HDC dc = ::CreateCompatibleDC(nullptr);
		HGDIOBJ old = ::SelectObject(dc, font);
		TEXTMETRIC metrics{};
		if (::GetTextMetricsW(dc, &metrics)) {
			lf.lfCharSet = metrics.tmCharSet;
			lf.lfPitchAndFamily = (metrics.tmPitchAndFamily & TMPF_FIXED_PITCH) ? VARIABLE_PITCH : FIXED_PITCH;
			if (metrics.tmPitchAndFamily & TMPF_TRUETYPE) lf.lfOutPrecision = OUT_STROKE_PRECIS;
		}
		::SelectObject(dc, old);
		::DeleteDC(dc);
		::DeleteObject(font);
	}
	for (int i = 1; i < count && i <= static_cast<int>(m_settings.m_fallbackFonts.size()); ++i) {
		::SendMessageW(list, LB_GETTEXT, i, reinterpret_cast<LPARAM>(m_settings.m_fallbackFonts[i - 1].data()));
	}
}

bool CDlgFontFallback::ReadSize()
{
	wchar_t text[32]{};
	::GetDlgItemTextW(GetHwnd(), IDC_EDIT_FONT_SIZE, text, static_cast<int>(std::size(text)));
	wchar_t* end = nullptr;
	const double points = std::wcstod(text, &end);
	if (end == text || *end != L'\0' || !std::isfinite(points) || points < 1 || points > 720) {
		ErrorMessage(GetHwnd(), LS(STR_FONT_SIZE_INVALID));
		::SetFocus(GetItemHwnd(IDC_EDIT_FONT_SIZE));
		return false;
	}
	const int tenths = static_cast<int>(std::round(points * 10));
	if (tenths != m_displayPointSize) {
		m_settings.m_nPointSize = tenths;
		m_settings.m_lf.lfHeight = DpiPointsToPixels(-tenths, 10);
	}
	return true;
}

void CDlgFontFallback::UpdateButtons()
{
	HWND list = GetItemHwnd(IDC_LIST_FALLBACK_FONTS);
	const int count = static_cast<int>(::SendMessageW(list, LB_GETCOUNT, 0, 0));
	const int selected = static_cast<int>(::SendMessageW(list, LB_GETCURSEL, 0, 0));
	::EnableWindow(GetItemHwnd(IDC_BUTTON_ADD), count <= static_cast<int>(m_settings.m_fallbackFonts.size()));
	::EnableWindow(GetItemHwnd(IDC_BUTTON_CHANGE), selected != LB_ERR);
	::EnableWindow(GetItemHwnd(IDC_BUTTON_DELETE), selected != LB_ERR && count > 1);
	::EnableWindow(GetItemHwnd(IDC_BUTTON_UP), selected > 0);
	::EnableWindow(GetItemHwnd(IDC_BUTTON_DOWN), selected != LB_ERR && selected + 1 < count);
}

BOOL CDlgFontFallback::OnLbnSelChange(HWND, int id)
{
	if (id != IDC_LIST_FALLBACK_FONTS) return FALSE;
	UpdateButtons();
	return TRUE;
}

BOOL CDlgFontFallback::OnBnClicked(int id)
{
	HWND list = GetItemHwnd(IDC_LIST_FALLBACK_FONTS);
	int selected = static_cast<int>(::SendMessageW(list, LB_GETCURSEL, 0, 0));
	const int count = static_cast<int>(::SendMessageW(list, LB_GETCOUNT, 0, 0));
	switch (id) {
	case IDC_BUTTON_ADD:
	case IDC_BUTTON_CHANGE:
		if ((id == IDC_BUTTON_ADD && count <= static_cast<int>(m_settings.m_fallbackFonts.size()))
			|| (id == IDC_BUTTON_CHANGE && selected != LB_ERR)) {
			LOGFONT lf = m_settings.m_lf;
			if (selected != LB_ERR) ::SendMessageW(list, LB_GETTEXT, selected, reinterpret_cast<LPARAM>(lf.lfFaceName));
			INT size = m_settings.m_nPointSize;
			if (MySelectFont(&lf, &size, GetHwnd(), false, true)) {
				const int existing = static_cast<int>(::SendMessageW(list, LB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(lf.lfFaceName)));
				if (existing != LB_ERR) {
					selected = existing;
				} else if (id == IDC_BUTTON_CHANGE) {
					::SendMessageW(list, LB_DELETESTRING, selected, 0);
					::SendMessageW(list, LB_INSERTSTRING, selected, reinterpret_cast<LPARAM>(lf.lfFaceName));
				} else {
					selected = static_cast<int>(::SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(lf.lfFaceName)));
				}
			}
		}
		break;
	case IDC_BUTTON_DELETE:
		if (selected != LB_ERR && count > 1) {
			::SendMessageW(list, LB_DELETESTRING, selected, 0);
			selected = std::min(selected, count - 2);
		}
		break;
	case IDC_BUTTON_UP:
	case IDC_BUTTON_DOWN:
		if (selected != LB_ERR) {
			const int destination = selected + (id == IDC_BUTTON_UP ? -1 : 1);
			if (0 <= destination && destination < count) {
				wchar_t name[LF_FACESIZE]{};
				::SendMessageW(list, LB_GETTEXT, selected, reinterpret_cast<LPARAM>(name));
				::SendMessageW(list, LB_DELETESTRING, selected, 0);
				::SendMessageW(list, LB_INSERTSTRING, destination, reinterpret_cast<LPARAM>(name));
				selected = destination;
			}
		}
		break;
	case IDOK:
		if (!ReadSize()) return TRUE;
		ReadList();
		return CDialog::OnBnClicked(id);
	default:
		return CDialog::OnBnClicked(id);
	}
	::SendMessageW(list, LB_SETCURSEL, selected, 0);
	UpdateButtons();
	return TRUE;
}
