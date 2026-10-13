/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "dlg/CDlgAutoCloseBrackets.h"
#include "CSelectLang.h"
#include "util/MessageBoxF.h"
#include "sakura_rc.h"
#include <algorithm>
#include <cwctype>
#include <string>

bool CDlgAutoCloseBrackets::DoModal(HINSTANCE instance, HWND parent, Rules& rules)
{
	m_rules = rules;
	const auto result = CDialog::DoModal(instance, parent, IDD_AUTOCLOSEBRACKETS, 0);
	if (result != IDOK) {
		return false;
	}
	rules = m_rules;
	return true;
}

void CDlgAutoCloseBrackets::SetData()
{
	::SendDlgItemMessageW(GetHwnd(), IDC_EDIT_BRACKET_OPENING, EM_SETLIMITTEXT, 32, 0);
	::SendDlgItemMessageW(GetHwnd(), IDC_EDIT_BRACKET_CLOSING, EM_SETLIMITTEXT, 32, 0);
	RefreshList(-1);
}

void CDlgAutoCloseBrackets::RefreshList(int selectedRule)
{
	HWND list = GetItemHwnd(IDC_LIST_AUTOCLOSEBRACKETS);
	::SendMessageW(list, LB_RESETCONTENT, 0, 0);
	int selectedRow = -1;
	for (size_t i = 0; i < m_rules.size(); ++i) {
		const auto& rule = m_rules[i];
		if (rule.opening == L'\0' || rule.closing == L'\0') {
			continue;
		}
		std::wstring text = rule.enabled ? L"[x] " : L"[ ] ";
		text += rule.opening;
		text += L" → ";
		text += rule.closing;
		const auto row = ::SendMessageW(list, LB_ADDSTRING, 0,
			reinterpret_cast<LPARAM>(text.c_str()));
		if (row != LB_ERR && row != LB_ERRSPACE) {
			// Empty slots are omitted, so a displayed row needs its original array index.
			::SendMessageW(list, LB_SETITEMDATA, static_cast<WPARAM>(row), static_cast<LPARAM>(i));
			if (static_cast<int>(i) == selectedRule) selectedRow = static_cast<int>(row);
		}
	}
	if (selectedRow == -1 && ::SendMessageW(list, LB_GETCOUNT, 0, 0) > 0) selectedRow = 0;
	::SendMessageW(list, LB_SETCURSEL, selectedRow, 0);
	UpdateControls();
}

int CDlgAutoCloseBrackets::GetSelectedRuleIndex()
{
	HWND list = GetItemHwnd(IDC_LIST_AUTOCLOSEBRACKETS);
	const auto row = ::SendMessageW(list, LB_GETCURSEL, 0, 0);
	if (row == LB_ERR) return -1;
	const auto index = ::SendMessageW(list, LB_GETITEMDATA, row, 0);
	return index >= 0 && index < static_cast<LRESULT>(m_rules.size()) ? static_cast<int>(index) : -1;
}

void CDlgAutoCloseBrackets::UpdateControls()
{
	const int index = GetSelectedRuleIndex();
	::EnableWindow(GetItemHwnd(IDC_CHECK_BRACKET_ENABLED), index >= 0);
	::CheckDlgButton(GetHwnd(), IDC_CHECK_BRACKET_ENABLED,
		index >= 0 && m_rules[index].enabled ? BST_CHECKED : BST_UNCHECKED);
	const bool hasSpace = std::any_of(m_rules.begin(), m_rules.end(), [](const auto& rule) {
		return rule.opening == L'\0' || rule.closing == L'\0';
	});
	::EnableWindow(GetItemHwnd(IDC_BUTTON_ADD), hasSpace);
}

bool CDlgAutoCloseBrackets::ReadCharacter(int controlId, wchar_t& value)
{
	wchar_t text[2]{};
	const int length = ::GetWindowTextLengthW(GetItemHwnd(controlId));
	::GetDlgItemTextW(GetHwnd(), controlId, text, static_cast<int>(std::size(text)));
	if (length != 1 || text[0] == L'\0' || std::iswspace(text[0]) || std::iswcntrl(text[0])
		|| (0xd800 <= text[0] && text[0] <= 0xdfff)) {
		ErrorMessage(GetHwnd(), LS(STR_BRACKET_INVALID_CHAR));
		::SetFocus(GetItemHwnd(controlId));
		return false;
	}
	value = text[0];
	return true;
}

void CDlgAutoCloseBrackets::AddRule()
{
	wchar_t opening, closing;
	if (!ReadCharacter(IDC_EDIT_BRACKET_OPENING, opening) || !ReadCharacter(IDC_EDIT_BRACKET_CLOSING, closing)) return;
	if (opening == closing) {
		ErrorMessage(GetHwnd(), LS(STR_BRACKET_SAME_CHAR));
		return;
	}
	int emptySlot = -1;
	for (size_t i = 0; i < m_rules.size(); ++i) {
		const auto& rule = m_rules[i];
		if (rule.opening == L'\0' || rule.closing == L'\0') {
			if (emptySlot == -1) emptySlot = static_cast<int>(i);
		} else if (rule.opening == opening) {
			RefreshList(static_cast<int>(i));
			ErrorMessage(GetHwnd(), LS(STR_BRACKET_DUPLICATE));
			return;
		}
	}
	if (emptySlot == -1) return;
	m_rules[emptySlot] = { true, opening, closing };
	RefreshList(emptySlot);
	::SetDlgItemTextW(GetHwnd(), IDC_EDIT_BRACKET_OPENING, L"");
	::SetDlgItemTextW(GetHwnd(), IDC_EDIT_BRACKET_CLOSING, L"");
	::SetFocus(GetItemHwnd(IDC_EDIT_BRACKET_OPENING));
}

BOOL CDlgAutoCloseBrackets::OnLbnSelChange(HWND, int id)
{
	if (id != IDC_LIST_AUTOCLOSEBRACKETS) return FALSE;
	UpdateControls();
	return TRUE;
}

BOOL CDlgAutoCloseBrackets::OnBnClicked(int id)
{
	switch (id) {
	case IDC_BUTTON_ADD:
		AddRule();
		return TRUE;
	case IDC_CHECK_BRACKET_ENABLED:
		{
			const int index = GetSelectedRuleIndex();
			if (index >= 0) {
				m_rules[index].enabled = ::IsDlgButtonChecked(GetHwnd(), id) == BST_CHECKED;
				RefreshList(index);
			}
		}
		return TRUE;
	default:
		return CDialog::OnBnClicked(id);
	}
}
