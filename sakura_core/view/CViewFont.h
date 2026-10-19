/*! @file */
/*
	Copyright (C) 2008, kobake
	Copyright (C) 2018-2022, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_CVIEWFONT_650DC4AC_DEA2_4652_B47A_6CA998C2F9EF_H_
#define SAKURA_CVIEWFONT_650DC4AC_DEA2_4652_B47A_6CA998C2F9EF_H_
#pragma once

#include "doc/CDocTypeSetting.h" // ColorInfo !!
#include "view/CFontFallback.h"
#include <array>

class CViewFont{
	using Me = CViewFont;

public:
	CViewFont(const LOGFONT *plf, bool bMiniMap = false, const SFontFallbacks* fallbacks = nullptr)
	{
		m_bMiniMap = bMiniMap;
		CreateFonts(plf, fallbacks);
	}
	CViewFont(const Me&) = delete;
	Me& operator = (const Me&) = delete;
	CViewFont(Me&&) noexcept = delete;
	Me& operator = (Me&&) noexcept = delete;
	virtual ~CViewFont()
	{
		DeleteFonts();
	}

	void UpdateFont(const LOGFONT *plf, const SFontFallbacks* fallbacks = nullptr)
	{
		DeleteFonts();
		CreateFonts(plf, fallbacks);
	}

	HFONT ChooseFontHandle( int fontNo, SFontAttr sFontAttr, bool italic = false, bool strike = false ) const;		/* フォントを選ぶ */
	int GetFontTopOffset(int fontNo) const { return m_fallback ? m_fallback->GetTopOffset(fontNo) : 0; }
	int GetFontHeight() const { return m_fallback ? m_fallback->GetHeight() : 0; }

	HFONT GetFontHan() const
	{
		return m_hFont_HAN;
	}

	const LOGFONT& GetLogfont([[maybe_unused]] int FontNo = 0) const
	{
		return m_LogFont;
	}

private:
	void CreateFonts( const LOGFONT *plf, const SFontFallbacks* fallbacks );
	void DeleteFonts( void );

	HFONT	m_hFont_HAN;			/* 現在のフォントハンドル */
	HFONT	m_hFont_HAN_BOLD;		/* 現在のフォントハンドル(太字) */
	HFONT	m_hFont_HAN_UL;			/* 現在のフォントハンドル(下線) */
	HFONT	m_hFont_HAN_BOLD_UL;	/* 現在のフォントハンドル(太字、下線) */
	std::array<HFONT, 4> m_italicFonts{}; // 通常／太字／下線／太字下線の斜体
	std::array<HFONT, 8> m_strikeFonts{}; // 太字／下線／斜体を組み合わせた取り消し線

	LOGFONT	m_LogFont;
	bool	m_bMiniMap;
	std::unique_ptr<CFontFallback> m_fallback;
};

CViewFont* GetViewFont(bool isMiniMap);

#endif /* SAKURA_CVIEWFONT_650DC4AC_DEA2_4652_B47A_6CA998C2F9EF_H_ */
