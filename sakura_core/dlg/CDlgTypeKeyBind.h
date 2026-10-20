/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#pragma once

#include "dlg/CDialog.h"
#include "func/CFuncLookup.h"
#include "types/CType.h"
#include <string>
#include <vector>

class CPropTypes;

class CDlgTypeKeyBind final : public CDialog {
public:
	using Bindings = decltype(STypeConfig::m_typeKeyBindings);

	static INT_PTR CALLBACK PageProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

protected:
	void SetData() override;
	BOOL OnBnClicked(int id) override;
	BOOL OnLbnSelChange(HWND control, int id) override;
	BOOL OnCbnSelChange(HWND control, int id) override;

private:
	int GetSelectedKeyIndex();
	int GetModifierState();
	void RefreshKeyList(int selectedIndex);
	void RefreshFunctionList(int category);
	void UpdateControls(bool selectFunction);
	void SelectFunction(EFunctionCode function);
	std::wstring GetFunctionName(EFunctionCode function);

	Bindings m_bindings{};
	CPropTypes* m_owner = nullptr;
	std::vector<KEYDATA> m_keys;
	CFuncLookup m_lookup;
};
