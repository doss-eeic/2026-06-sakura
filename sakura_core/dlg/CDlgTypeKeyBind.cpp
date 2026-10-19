/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "dlg/CDlgTypeKeyBind.h"
#include "config/system_constants.h"
#include "env/DLLSHAREDATA.h"
#include "typeprop/CPropTypes.h"
#include "CSelectLang.h"
#include "sakura_rc.h"
#include <commctrl.h>

INT_PTR CALLBACK CDlgTypeKeyBind::PageProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	auto* page = reinterpret_cast<CDlgTypeKeyBind*>(::GetWindowLongPtr(hwnd, DWLP_USER));
	if (message == WM_INITDIALOG) {
		const auto* info = reinterpret_cast<PROPSHEETPAGE*>(lParam);
		page = new CDlgTypeKeyBind;
		page->m_owner = reinterpret_cast<CPropTypes*>(info->lParam);
		page->m_bindings = page->m_owner->m_Types.m_typeKeyBindings;
		page->m_bModal = FALSE;
		page->m_hInstance = info->hInstance;
		page->m_hwndParent = ::GetParent(hwnd);
		page->m_hLangRsrcInstance = info->hInstance;
		return page->OnInitDialog(hwnd, wParam, reinterpret_cast<LPARAM>(page));
	}
	if (page == nullptr) return FALSE;

	switch (message) {
	case WM_COMMAND:
		// The property sheet owns its OK/Cancel buttons and the page lifetime.
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) return FALSE;
		return page->OnCommand(wParam, lParam);
	case WM_NOTIFY:
		switch (reinterpret_cast<NMHDR*>(lParam)->code) {
		case PSN_SETACTIVE:
			page->m_owner->m_nPageNum = ID_PROPTYPE_PAGENUM_KEYBIND;
			::SetWindowLongPtr(hwnd, DWLP_MSGRESULT, 0);
			return TRUE;
		case PSN_KILLACTIVE:
			::SetWindowLongPtr(hwnd, DWLP_MSGRESULT, FALSE);
			return TRUE;
		case PSN_APPLY:
			page->m_owner->m_Types.m_typeKeyBindings = page->m_bindings;
			::SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
			return TRUE;
		}
		break;
	case WM_NCDESTROY:
		::SetWindowLongPtr(hwnd, DWLP_USER, 0);
		page->_SetHwnd(nullptr);
		delete page;
		return FALSE;
	}
	return FALSE;
}

void CDlgTypeKeyBind::SetData()
{
	auto& common = m_pShareData->m_Common;
	m_lookup.Init(common.m_sMacro.m_MacroTable, &common);
	m_lookup.SetCategory2Combo(GetItemHwnd(IDC_COMBO_FUNCKIND));
	::SendDlgItemMessageW(GetHwnd(), IDC_COMBO_FUNCKIND, CB_SETCURSEL, 0, 0);
	RefreshFunctionList(0);

	m_keys.clear();
	for (int i = 0; i < common.m_sKeyBind.m_nKeyNameArrNum; ++i) {
		const auto& key = common.m_sKeyBind.m_pKeyNameArr[i];
		// This dialog edits keyboard shortcuts handled by Windows accelerators.
		if (0 < key.m_nKeyCode && key.m_nKeyCode <= 0xff) {
			m_keys.push_back(key);
		}
	}
	RefreshKeyList(m_keys.empty() ? -1 : 0);
}

int CDlgTypeKeyBind::GetSelectedKeyIndex()
{
	const auto index = ::SendDlgItemMessageW(GetHwnd(), IDC_LIST_KEY, LB_GETCURSEL, 0, 0);
	return 0 <= index && index < static_cast<LRESULT>(m_keys.size())
		? static_cast<int>(index) : -1;
}

int CDlgTypeKeyBind::GetModifierState()
{
	int state = 0;
	if (::IsDlgButtonChecked(GetHwnd(), IDC_CHECK_SHIFT) == BST_CHECKED) state |= _SHIFT;
	if (::IsDlgButtonChecked(GetHwnd(), IDC_CHECK_CTRL) == BST_CHECKED) state |= _CTRL;
	if (::IsDlgButtonChecked(GetHwnd(), IDC_CHECK_ALT) == BST_CHECKED) state |= _ALT;
	return state;
}

void CDlgTypeKeyBind::RefreshKeyList(int selectedIndex)
{
	const HWND list = GetItemHwnd(IDC_LIST_KEY);
	const auto top = ::SendMessageW(list, LB_GETTOPINDEX, 0, 0);
	const int state = GetModifierState();
	std::wstring prefix;
	if (state & _SHIFT) prefix += L"Shift+";
	if (state & _CTRL) prefix += L"Ctrl+";
	if (state & _ALT) prefix += L"Alt+";

	::SendMessageW(list, WM_SETREDRAW, FALSE, 0);
	::SendMessageW(list, LB_RESETCONTENT, 0, 0);
	for (const auto& key : m_keys) {
		std::wstring text = m_bindings[key.m_nKeyCode].m_bOverride[state] ? L"* " : L"  ";
		text += prefix;
		text += key.m_szKeyName;
		::SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
	}
	::SendMessageW(list, LB_SETCURSEL, selectedIndex, 0);
	if (top != LB_ERR) ::SendMessageW(list, LB_SETTOPINDEX, top, 0);
	::SendMessageW(list, WM_SETREDRAW, TRUE, 0);
	::InvalidateRect(list, nullptr, TRUE);
	UpdateControls(true);
}

std::wstring CDlgTypeKeyBind::GetFunctionName(EFunctionCode function)
{
	if (function == F_DEFAULT) return LS(STR_PROPCOMKEYBIND_UNASSIGN);
	wchar_t name[256]{};
	m_lookup.Funccode2Name(function, name, std::size(name));
	return name;
}

void CDlgTypeKeyBind::RefreshFunctionList(int category)
{
	const HWND list = GetItemHwnd(IDC_LIST_FUNC);
	m_lookup.SetListItem(list, category);
	int row = 0;
	for (int item = 0; item < m_lookup.GetItemCount(category); ++item) {
		const auto function = m_lookup.Pos2FuncCode(category, item);
		// SetListItem omits undefined commands; keep the displayed row's actual code.
		if (function == F_DEFAULT) continue;
		::SendMessageW(list, LB_SETITEMDATA, row++, static_cast<LPARAM>(function));
	}
}

void CDlgTypeKeyBind::SelectFunction(EFunctionCode function)
{
	if (function != F_DEFAULT) {
		for (int category = 0; category < m_lookup.GetCategoryCount(); ++category) {
			int row = 0;
			for (int item = 0; item < m_lookup.GetItemCount(category); ++item) {
				const auto code = m_lookup.Pos2FuncCode(category, item);
				if (code == F_DEFAULT) continue;
				if (code == function) {
					::SendDlgItemMessageW(GetHwnd(), IDC_COMBO_FUNCKIND, CB_SETCURSEL, category, 0);
					RefreshFunctionList(category);
					::SendDlgItemMessageW(GetHwnd(), IDC_LIST_FUNC, LB_SETCURSEL, row, 0);
					return;
				}
				++row;
			}
		}
	}
	::SendDlgItemMessageW(GetHwnd(), IDC_LIST_FUNC, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
}

void CDlgTypeKeyBind::UpdateControls(bool selectFunction)
{
	const int index = GetSelectedKeyIndex();
	bool overridden = false;
	if (index >= 0) {
		auto& key = m_keys[index];
		const int state = GetModifierState();
		const auto& binding = m_bindings[key.m_nKeyCode];
		overridden = binding.m_bOverride[state];
		const auto common = CKeyBind::GetFuncCodeAt(key, state);
		const auto effective = CKeyBind::GetFuncCodeAt(key, state, TRUE, &binding);
		::SetDlgItemTextW(GetHwnd(), IDC_EDIT_TYPEKEYBIND_COMMON, GetFunctionName(common).c_str());
		::SetDlgItemTextW(GetHwnd(), IDC_EDIT_KEYSFUNC, GetFunctionName(effective).c_str());
		::SetDlgItemTextW(GetHwnd(), IDC_STATIC_TYPEKEYBIND_SOURCE,
			LS(overridden ? STR_TYPEKEYBIND_OVERRIDE : STR_TYPEKEYBIND_INHERIT));
		if (selectFunction) SelectFunction(effective);
	} else {
		::SetDlgItemTextW(GetHwnd(), IDC_EDIT_TYPEKEYBIND_COMMON, L"");
		::SetDlgItemTextW(GetHwnd(), IDC_EDIT_KEYSFUNC, L"");
		::SetDlgItemTextW(GetHwnd(), IDC_STATIC_TYPEKEYBIND_SOURCE, L"");
	}

	const auto category = ::SendDlgItemMessageW(GetHwnd(), IDC_COMBO_FUNCKIND, CB_GETCURSEL, 0, 0);
	const auto function = ::SendDlgItemMessageW(GetHwnd(), IDC_LIST_FUNC, LB_GETCURSEL, 0, 0);
	::EnableWindow(GetItemHwnd(IDC_BUTTON_ASSIGN), index >= 0 && category != CB_ERR && function != LB_ERR);
	::EnableWindow(GetItemHwnd(IDC_BUTTON_RELEASE), index >= 0);
	::EnableWindow(GetItemHwnd(IDC_BUTTON_KEYBIND_INHERIT), index >= 0 && overridden);
}

BOOL CDlgTypeKeyBind::OnBnClicked(int id)
{
	switch (id) {
	case IDC_CHECK_SHIFT:
	case IDC_CHECK_CTRL:
	case IDC_CHECK_ALT:
		RefreshKeyList(GetSelectedKeyIndex());
		return TRUE;
	case IDC_BUTTON_ASSIGN:
	case IDC_BUTTON_RELEASE:
	case IDC_BUTTON_KEYBIND_INHERIT:
		{
			const int index = GetSelectedKeyIndex();
			if (index < 0) return TRUE;
			const int state = GetModifierState();
			auto& binding = m_bindings[m_keys[index].m_nKeyCode];
			EFunctionCode function = F_DEFAULT;
			if (id == IDC_BUTTON_ASSIGN) {
				const int category = static_cast<int>(::SendDlgItemMessageW(GetHwnd(), IDC_COMBO_FUNCKIND, CB_GETCURSEL, 0, 0));
				const int item = static_cast<int>(::SendDlgItemMessageW(GetHwnd(), IDC_LIST_FUNC, LB_GETCURSEL, 0, 0));
				if (category == CB_ERR || item == LB_ERR) return TRUE;
				const auto code = ::SendDlgItemMessageW(GetHwnd(), IDC_LIST_FUNC, LB_GETITEMDATA, item, 0);
				if (code == LB_ERR) return TRUE;
				function = static_cast<EFunctionCode>(code);
			}
			binding.m_bOverride[state] = id != IDC_BUTTON_KEYBIND_INHERIT;
			binding.m_nFuncCodeArr[state] = function;
			RefreshKeyList(index);
		}
		return TRUE;
	default:
		return CDialog::OnBnClicked(id);
	}
}

BOOL CDlgTypeKeyBind::OnLbnSelChange(HWND, int id)
{
	if (id != IDC_LIST_KEY && id != IDC_LIST_FUNC) return FALSE;
	UpdateControls(id == IDC_LIST_KEY);
	return TRUE;
}

BOOL CDlgTypeKeyBind::OnCbnSelChange(HWND, int id)
{
	if (id != IDC_COMBO_FUNCKIND) return FALSE;
	const int category = static_cast<int>(::SendDlgItemMessageW(GetHwnd(), id, CB_GETCURSEL, 0, 0));
	if (category != CB_ERR) RefreshFunctionList(category);
	UpdateControls(false);
	return TRUE;
}
