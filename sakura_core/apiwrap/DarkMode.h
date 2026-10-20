/*! @file
	@brief ダークモード判定ヘルパー

	darkmodelib の判定APIをラップし、ダークモードの有効判定を
	一箇所に集約する。

	SPDX-License-Identifier: Zlib
*/
#pragma once

#include <DarkModeSubclass.h>

//! UIを実際にダーク表示しているか
inline bool IsDarkModeActive()
{
	return DarkMode::isEnabled() && DarkMode::isExperimentalActive();
}

//! 通常ダークまたはフルダークが指定されているか
inline bool IsDarkModeRequested(BOOL bDarkMode, BOOL bFullDarkMode)
{
	return bDarkMode != FALSE || bFullDarkMode != FALSE;
}

//! 本文にもダーク配色を適用するか
inline bool IsFullDarkModeActive(BOOL bFullDarkMode)
{
	return bFullDarkMode != FALSE && IsDarkModeActive();
}

//! 共有設定をUI用ダークモードライブラリに反映する
inline void ApplyDarkModeSetting(BOOL bDarkMode, BOOL bFullDarkMode)
{
	const auto dmType = IsDarkModeRequested(bDarkMode, bFullDarkMode)
		? DarkMode::DarkModeType::dark
		: DarkMode::DarkModeType::light;

	DarkMode::setDarkModeConfigEx(static_cast<UINT>(dmType));
	DarkMode::setDefaultColors(true);
}
