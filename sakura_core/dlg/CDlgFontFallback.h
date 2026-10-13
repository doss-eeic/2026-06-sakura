/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#pragma once

#include "dlg/CDialog.h"
#include "env/CommonSetting.h"

class CDlgFontFallback final : public CDialog {
public:
	bool DoModal(HINSTANCE instance, HWND parent, CommonSetting_View& settings);

protected:
	void SetData() override;
	BOOL OnBnClicked(int id) override;
	BOOL OnLbnSelChange(HWND control, int id) override;

private:
	void UpdateButtons();
	void ReadList();
	bool ReadSize();
	CommonSetting_View m_settings{};
	int m_displayPointSize = 0;
};
