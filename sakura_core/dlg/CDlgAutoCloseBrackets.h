/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#pragma once

#include "dlg/CDialog.h"
#include "types/CType.h"

class CDlgAutoCloseBrackets final : public CDialog {
public:
	using Rules = std::array<SAutoCloseBracket, 16>;

	bool DoModal(HINSTANCE instance, HWND parent, Rules& rules);

protected:
	void SetData() override;
	BOOL OnBnClicked(int id) override;
	BOOL OnLbnSelChange(HWND control, int id) override;

private:
	void RefreshList(int selectedRule);
	void UpdateControls();
	int GetSelectedRuleIndex();
	bool ReadCharacter(int controlId, wchar_t& value);
	void AddRule();
	Rules m_rules{};
};
