/*! @file */
/*
	Copyright (C) 2008, kobake
	Copyright (C) 2018-2022, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/

#include "StdAfx.h"
#include "CViewFont.h"
#include "env/DLLSHAREDATA.h"
#include "util/window.h"

/*! フォント作成
*/
void CViewFont::CreateFonts( const LOGFONT *plf )
{
	LOGFONT	lf;
	int miniSize = ::DpiScaleX(GetDllShareData().m_Common.m_sWindow.m_nMiniMapFontSize);
	int quality = GetDllShareData().m_Common.m_sWindow.m_nMiniMapQuality;
	int outPrec = OUT_TT_ONLY_PRECIS;	// FixedSys等でMiniMapのフォントが小さくならない修正

	/* フォント作成 */
	lf = *plf;
	if( m_bMiniMap ){
		lf.lfHeight = miniSize;
		lf.lfQuality = BYTE(quality);
		lf.lfOutPrecision = BYTE(outPrec);
	}
	m_hFont_HAN = CreateFontIndirect( &lf );
	m_LogFont = lf;
	const auto& fallbacks = GetDllShareData().m_Common.m_sView.m_fallbackFonts;
	if (std::any_of(fallbacks.begin(), fallbacks.end(), [](const auto& name) { return name[0] != L'\0'; })) {
		m_fallback = std::make_unique<CFontFallback>(lf, fallbacks);
	}

	/* 太字フォント作成 */
	lf = *plf;
	if( m_bMiniMap ){
		lf.lfHeight = miniSize;
		lf.lfQuality = BYTE(quality);
		lf.lfOutPrecision = BYTE(outPrec);
	}
	lf.lfWeight += 300;
	if( 1000 < lf.lfWeight ){
		lf.lfWeight = 1000;
	}
	m_hFont_HAN_BOLD = CreateFontIndirect( &lf );

	/* 下線フォント作成 */
	lf = *plf;
	if( m_bMiniMap ){
		lf.lfHeight = miniSize;
		lf.lfQuality = BYTE(quality);
		lf.lfOutPrecision = BYTE(outPrec);
	}
	
	lf.lfUnderline = TRUE;
	m_hFont_HAN_UL = CreateFontIndirect( &lf );

	/* 太字下線フォント作成 */
	lf = *plf;
	if( m_bMiniMap ){
		lf.lfHeight = miniSize;
		lf.lfQuality = BYTE(quality);
		lf.lfOutPrecision = BYTE(outPrec);
	}
	lf.lfUnderline = TRUE;
	lf.lfWeight += 300;
	if( 1000 < lf.lfWeight ){
		lf.lfWeight = 1000;
	}
	m_hFont_HAN_BOLD_UL = CreateFontIndirect( &lf );
}

/*! フォント削除
*/
void CViewFont::DeleteFonts()
{
	m_fallback.reset();
	DeleteObject( m_hFont_HAN );
	DeleteObject( m_hFont_HAN_BOLD );
	DeleteObject( m_hFont_HAN_UL );
	DeleteObject( m_hFont_HAN_BOLD_UL );
}

/*! フォントを選ぶ
	@param m_bBoldFont trueで太字
	@param m_bUnderLine trueで下線
*/
HFONT CViewFont::ChooseFontHandle( int fontNo, SFontAttr sFontAttr ) const
{
	if (m_fallback) return m_fallback->GetFont(fontNo, sFontAttr.m_bBoldFont, sFontAttr.m_bUnderLine);
	assert( fontNo == 0 );
	if( sFontAttr.m_bBoldFont ){	/* 太字か */
		if( sFontAttr.m_bUnderLine ){	/* 下線か */
			return m_hFont_HAN_BOLD_UL;
		}else{
			return m_hFont_HAN_BOLD;
		}
	}else{
		if( sFontAttr.m_bUnderLine ){	/* 下線か */
			return m_hFont_HAN_UL;
		}else{
			return m_hFont_HAN;
		}
	}
}
