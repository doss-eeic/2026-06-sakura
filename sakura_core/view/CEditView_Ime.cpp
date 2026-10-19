/*!	@file
	@brief IMEの処理

	@author Norio Nakatani
	@date	1998/03/13 作成
	@date   2008/04/13 CEditView.cppから分離
*/
/*
	Copyright (C) 1998-2002, Norio Nakatani
	Copyright (C) 2000, genta, JEPRO, MIK
	Copyright (C) 2001, genta, GAE, MIK, hor, asa-o, Stonee, Misaka, novice, YAZAKI
	Copyright (C) 2002, YAZAKI, hor, aroka, MIK, Moca, minfu, KK, novice, ai, Azumaiya, genta
	Copyright (C) 2003, MIK, ai, ryoji, Moca, wmlhq, genta
	Copyright (C) 2004, genta, Moca, novice, naoh, isearch, fotomo
	Copyright (C) 2005, genta, MIK, novice, aroka, D.S.Koba, かろと, Moca
	Copyright (C) 2006, Moca, aroka, ryoji, fon, genta
	Copyright (C) 2007, ryoji, じゅうじ, maru
	Copyright (C) 2018-2022, Sakura Editor Organization

	This source code is designed for sakura editor.
	Please contact the copyright holders to use this code for other purpose.
*/

#include "StdAfx.h"
#include "CEditView.h"
#include <algorithm>
#include "charset/CShiftJis.h"
#include "doc/CEditDoc.h"
#include "window/CEditWnd.h"
#include <string>
#include <vector>
#include <memory>
#include <utility>

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
//                           IME                               //
// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //

namespace {

template<class T>
bool ReadImeData(
	HIMC context,
	DWORD index,
	std::vector<T>& destination
)
{
	const LONG bytes =
		::ImmGetCompositionStringW(context, index, nullptr, 0);

	if (bytes < 0 || bytes % sizeof(T) != 0) {
		return false;
	}

	std::vector<T> buffer(bytes / sizeof(T));

	if (bytes > 0) {
		const LONG copied = ::ImmGetCompositionStringW(
			context,
			index,
			buffer.data(),
			static_cast<DWORD>(bytes)
		);

		if (copied < 0 ||
			copied > bytes ||
			copied % sizeof(T) != 0) {
			return false;
		}

		buffer.resize(copied / sizeof(T));
	}

	destination = std::move(buffer);
	return true;
}

}

/*!	IME ONか

	@date  2006.12.04 ryoji 新規作成（関数化）
*/
bool CEditView::IsImeON( void )
{
	bool bRet;
	HIMC	hIme;
	DWORD	conv, sent;

	//	From here Nov. 26, 2006 genta
	hIme = ImmGetContext( GetHwnd() );
	if( ImmGetOpenStatus( hIme ) != FALSE ){
		ImmGetConversionStatus( hIme, &conv, &sent );
		if(( conv & IME_CMODE_NOCONVERSION ) == 0 ){
			bRet = true;
		}
		else {
			bRet = false;
		}
	}
	else {
		bRet = false;
	}
	ImmReleaseContext( GetHwnd(), hIme );
	//	To here Nov. 26, 2006 genta

	return bRet;
}

/* IME編集エリアの位置を変更 */
void CEditView::SetIMECompFormPos( void )
{
	//
	// If current composition form mode is near caret operation,
	// application should inform IME UI the caret position has been
	// changed. IME UI will make decision whether it has to adjust
	// composition window position.
	//
	//
	if (m_ime.active && !m_bImeNativeComposition) {
		UpdateImeCandidatePosition();
		return;
	}

	HIMC hIMC = ::ImmGetContext( GetHwnd() );
	if ( hIMC ){
		if ( ::ImmGetOpenStatus( hIMC ) ){
			POINT point;
			::GetCaretPos( &point );
			COMPOSITIONFORM CompForm;
			CompForm.dwStyle = CFS_POINT;
			CompForm.ptCurrentPos.x = (long) point.x;
			if (m_bImeNativeComposition) {// 矩形選択中の変換文字列は、キャレットの下に表示する。
				CompForm.ptCurrentPos.y = point.y + GetTextMetrics().GetHankakuDy();
			} else {
				CompForm.ptCurrentPos.y = point.y + GetCaret().GetCaretSize().cy - GetTextMetrics().GetHankakuHeight();
			}
			::ImmSetCompositionWindow( hIMC, &CompForm );
		}
		::ImmReleaseContext( GetHwnd() , hIMC );
	}
}

/* IME編集エリアの表示フォントを変更 */
void CEditView::SetIMECompFormFont( void )
{
	//
	// If current composition form mode is near caret operation,
	// application should inform IME UI the caret position has been
	// changed. IME UI will make decision whether it has to adjust
	// composition window position.
	//
	//
	HIMC	hIMC = ::ImmGetContext( GetHwnd() );
	if ( hIMC ){
		::ImmSetCompositionFont( hIMC, const_cast<LOGFONT *>(&(GetEditWnd().GetLogfont())) );
	}
	::ImmReleaseContext( GetHwnd() , hIMC );
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
//                          再変換・変換補助
// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
/*!
	@brief IMEの再変換/前後参照で、カーソル位置から前後200chars位を取り出してRECONVERTSTRINGを埋める
	@param  pReconv  [out]  RECONVERTSTRING構造体へのポインタ。NULLあり
	@param  bDocumentFeed   trueならばIMR_DOCUMENTFEEDとして処理する
	@return   RECONVERTSTRINGのサイズ。0ならIMEは何もしない(はず)
	@date 2002.04.09 minfu
	@date 2010.03.16 Moca IMR_DOCUMENTFEED対応
*/
LRESULT CEditView::SetReconvertStruct(PRECONVERTSTRING pReconv, bool bDocumentFeed)
{
	if( false == bDocumentFeed ){
		m_nLastReconvIndex = -1;
		m_nLastReconvLine  = -1;
	}
	
	//矩形選択中は何もしない
	if( GetSelectionInfo().IsBoxSelecting() )
		return 0;

	// 2010.04.06 ビューモードでは何もしない
	if( CAppMode::getInstance()->IsViewMode() ){
		return 0;
	}
	
	// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
	//                      選択範囲を取得                         //
	// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
	//選択範囲を取得 -> ptSelect, ptSelectTo, nSelectedLen
	CLogicPoint	ptSelect;
	CLogicPoint	ptSelectTo;
	int			nSelectedLen;
	if( GetSelectionInfo().IsTextSelected() ){
		//テキストが選択されているとき
		m_pcEditDoc->m_cLayoutMgr.LayoutToLogic(GetSelectionInfo().m_sSelect.GetFrom(), &ptSelect);
		m_pcEditDoc->m_cLayoutMgr.LayoutToLogic(GetSelectionInfo().m_sSelect.GetTo(), &ptSelectTo);
		
		// 選択範囲が複数行の時、１ロジック行以内に制限
		if (ptSelectTo.y != ptSelect.y){
			if( bDocumentFeed ){
				// 暫定：未選択として振舞う
				// 改善案：選択範囲は置換されるので、選択範囲の前後をIMEに渡す
				// ptSelectTo.y = ptSelectTo.y;
				ptSelectTo.x = ptSelect.x;
			}else{
				// 2010.04.06 対象をptSelect.yの行からカーソル行に変更
				const CDocLine* pDocLine = m_pcEditDoc->m_cDocLineMgr.GetLine(GetCaret().GetCaretLogicPos().y);
				CLogicInt targetY = GetCaret().GetCaretLogicPos().y;
				// カーソル行が実質無選択なら、直前・直後の行を選択
				if( ptSelect.y == GetCaret().GetCaretLogicPos().y
						&& pDocLine && pDocLine->GetLengthWithoutEOL() == GetCaret().GetCaretLogicPos().x ){
					// カーソルが上側行末 => 次の行。行末カーソルでのShift+Upなど
					targetY = t_min(m_pcEditDoc->m_cDocLineMgr.GetLineCount(),
						GetCaret().GetCaretLogicPos().y + 1);
					pDocLine = m_pcEditDoc->m_cDocLineMgr.GetLine(targetY);
				}else
				if( ptSelectTo.y == GetCaret().GetCaretLogicPos().y
						&& 0 == GetCaret().GetCaretLogicPos().x ){
					// カーソルが下側行頭 => 前の行。 行頭でShift+Down/Shift+End→Rightなど
					targetY = GetCaret().GetCaretLogicPos().y - 1;
					pDocLine = m_pcEditDoc->m_cDocLineMgr.GetLine(targetY);
				}
				// 選択範囲をxで指定：こちらはカーソルではなく選択範囲基準
				if(targetY == ptSelect.y){
					// ptSelect.x; 未変更
					ptSelectTo.x = pDocLine ? pDocLine->GetLengthWithoutEOL() : 0;
				}else
				if(targetY == ptSelectTo.y){
					ptSelect.x = 0;
					// ptSelectTo.x; 未変更
				}else{
					ptSelect.x = 0;
					ptSelectTo.x = pDocLine ? pDocLine->GetLengthWithoutEOL() : 0;
				}
				ptSelect.y = targetY;
				// ptSelectTo.y = targetY; 以下未使用
			}
		}
	}
	else{
		//テキストが選択されていないとき
		m_pcEditDoc->m_cLayoutMgr.LayoutToLogic(GetCaret().GetCaretLayoutPos(), &ptSelect);
		ptSelectTo = ptSelect;
	}
	nSelectedLen = ptSelectTo.x - ptSelect.x;
	// 以下 ptSelect.y ptSelect.x, nSelectedLen を使用

	//ドキュメント行取得 -> pcCurDocLine
	const CDocLine* pcCurDocLine = m_pcEditDoc->m_cDocLineMgr.GetLine(ptSelect.GetY2());
	if (nullptr == pcCurDocLine )
		return 0;

	//テキスト取得 -> pLine, nLineLen
	const int nLineLen = pcCurDocLine->GetLengthWithoutEOL();
	if ( 0 == nLineLen )
		return 0;
	const wchar_t* pLine = pcCurDocLine->GetPtr();

	// 2010.04.17 行頭から←選択だと「SelectToが改行の後ろの位置」にあるため範囲を調整する
	// フリーカーソル選択でも行末より後ろにカーソルがある
	if( nLineLen < ptSelect.x ){
		// 改行直前をIMEに渡すカーソル位置ということにする
		ptSelect.x = CLogicInt(nLineLen);
		nSelectedLen = 0;
	}
	if( nLineLen <  ptSelect.x + nSelectedLen ){
		nSelectedLen = nLineLen - ptSelect.x;
	}

	// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
	//              再変換範囲・考慮文字を修正                     //
	// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //

	//再変換考慮文字列開始  //行の中で再変換のAPIにわたすとする文字列の開始位置
	int nReconvIndex = 0;
	int nInsertCompLen = 0; // DOCUMENTFEED用。変換中の文字列をdwStrに混ぜる
	// Iはカーソル　[]が選択範囲=dwTargetStrLenだとして
	// 行：日本語をIします。
	// IME：にゅうｒ
	// APIに渡す文字列：日本語を[にゅうｒ]Iします。

	// 選択開始位置より前後200(or 50)文字ずつを考慮文字列にする
	const int nReconvMaxLen = (bDocumentFeed ? 50 : 200); //$$マジックナンバー注意
	while (ptSelect.x - nReconvIndex > nReconvMaxLen) {
		nReconvIndex = t_max<int>(nReconvIndex+1, int(::CharNextW(pLine+nReconvIndex) - pLine));
	}
	
	//再変換考慮文字列終了  //行の中で再変換のAPIにわたすとする文字列の長さ
	int nReconvLen = nLineLen - nReconvIndex;
	if ( (nReconvLen + nReconvIndex - ptSelect.x) > nReconvMaxLen ){
		const wchar_t*       p = pLine + ptSelect.x;
		const wchar_t* const q = pLine + ptSelect.x + nReconvMaxLen;
		while (p <= q) {
			p = t_max(p+1, const_cast<const wchar_t*>(::CharNext(p)));
		}
		nReconvLen = int(p - pLine - nReconvIndex);
	}
	
	//対象文字列の調整
	if ( ptSelect.x + nSelectedLen > nReconvIndex + nReconvLen ){
		// 考慮分しかAPIに渡さないので、選択範囲を縮小
		nSelectedLen = nReconvLen + nReconvIndex - ptSelect.x;
	}
	
	if( bDocumentFeed ){
		// IMR_DOCUMENTFEEDでは、再変換対象はIMEから取得した入力中文字列
		nInsertCompLen = (int)wcslen(m_szComposition);
		if( 0 == nInsertCompLen ){
			// 2回呼ばれるので、m_szCompositionに覚えておく
			HWND hwnd = GetHwnd();
			HIMC hIMC = ::ImmGetContext( hwnd );
			if( !hIMC ){
				return 0;
			}
			wmemset(m_szComposition, L'\0', int(std::size(m_szComposition)));
			LONG immRet = ::ImmGetCompositionString(hIMC, GCS_COMPSTR, m_szComposition, int(std::size(m_szComposition)));
			if( immRet == IMM_ERROR_NODATA || immRet == IMM_ERROR_GENERAL ){
				m_szComposition[0] = L'\0';
			}
			::ImmReleaseContext( hwnd, hIMC );
			nInsertCompLen = (int)wcslen(m_szComposition);
		}
	}

	// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
	//                      構造体設定要素                         //
	// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //

	//行の中で再変換のAPIにわたすとする文字列の長さ
	int         cbReconvLenWithNull; // byte
	DWORD       dwReconvTextLen;    // CHARs
	DWORD       dwReconvTextInsLen; // CHARs
	DWORD       dwCompStrOffset;    // byte
	DWORD       dwCompStrLen;       // CHARs
	DWORD       dwInsByteCount = 0; // byte
	const void* pszReconv; 
	const void* pszInsBuffer;

	const WCHAR* pszCompInsStr = L"";
	int nCompInsStr   = 0;
	if( nInsertCompLen ){
		pszCompInsStr = m_szComposition;
		nCompInsStr   = (int)wcslen( pszCompInsStr );
	}
	dwInsByteCount      = nCompInsStr * sizeof(wchar_t);
	dwReconvTextLen     = nReconvLen;
	dwReconvTextInsLen  = dwReconvTextLen + nCompInsStr;                 //reconv文字列長。文字単位。
	cbReconvLenWithNull = (dwReconvTextInsLen + 1) * sizeof(wchar_t);    //reconvデータ長。バイト単位。
	dwCompStrOffset     = (Int)(ptSelect.x - nReconvIndex) * sizeof(wchar_t);    //compオフセット。バイト単位。
	dwCompStrLen        = nSelectedLen + nCompInsStr;                            //comp文字列長。文字単位。
	pszReconv           = reinterpret_cast<const void*>(pLine + nReconvIndex);   //reconv文字列へのポインタ。
	pszInsBuffer        = pszCompInsStr;
	
	// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
	//                        構造体設定                           //
	// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
	if ( nullptr != pReconv) {
		//再変換構造体の設定
		DWORD dwOrgSize = pReconv->dwSize;
		// 2010.03.17 Moca dwSizeはpReconvを用意する側(IME等)が設定
		//     のはずなのに Win XP+IME2002+TSF では dwSizeが0で送られてくる
		if( dwOrgSize != 0 && dwOrgSize < sizeof(*pReconv) + cbReconvLenWithNull ){
			// バッファ不足
			m_szComposition[0] = L'\0';
			return 0;
		}
		else if( 0 == dwOrgSize ){
			pReconv->dwSize = sizeof(*pReconv) + cbReconvLenWithNull;
		}
		pReconv->dwVersion         = 0;
		pReconv->dwStrLen          = dwReconvTextInsLen;	//文字単位
		pReconv->dwStrOffset       = sizeof(*pReconv) ;
		pReconv->dwCompStrLen      = dwCompStrLen;		//文字単位
		pReconv->dwCompStrOffset   = dwCompStrOffset;	//バイト単位
		pReconv->dwTargetStrLen    = dwCompStrLen;		//文字単位
		pReconv->dwTargetStrOffset = dwCompStrOffset;	//バイト単位
		
		// 2004.01.28 Moca ヌル終端の修正
		WCHAR* p = (WCHAR*)(pReconv + 1);
		if( dwInsByteCount ){
			// カーソル位置に、入力中IMEデータを挿入
			CHAR* pb = (CHAR*)p;
			CopyMemory(pb, pszReconv, dwCompStrOffset);
			pb += dwCompStrOffset;
			CopyMemory(pb, pszInsBuffer, dwInsByteCount);
			pb += dwInsByteCount;
			CopyMemory(pb, ((char*)pszReconv) + dwCompStrOffset,
				dwReconvTextLen*sizeof(wchar_t) - dwCompStrOffset);
		}else{
			CopyMemory(p, pszReconv, cbReconvLenWithNull - sizeof(wchar_t));
		}
		// \0があると応答なしになることがある
		for( DWORD i = 0; i < dwReconvTextInsLen; i++ ){
			if( p[i] == 0 ){
				p[i] = L' ';
			}
		}
		p[dwReconvTextInsLen] = L'\0';
	}
	
	if( false == bDocumentFeed ){
		// 再変換情報の保存
		m_nLastReconvIndex = nReconvIndex;
		m_nLastReconvLine  = ptSelect.y;
	}
	if( bDocumentFeed && pReconv ){
		m_szComposition[0] = L'\0';
	}
	return sizeof(RECONVERTSTRING) + cbReconvLenWithNull;
}

/*再変換用 エディタ上の選択範囲を変更する 2002.04.09 minfu */
LRESULT CEditView::SetSelectionFromReonvert(const RECONVERTSTRING* pReconv)
{
	// 再変換情報が保存されているか
	if ( (m_nLastReconvIndex < 0) || (m_nLastReconvLine < 0))
		return 0;

	if ( GetSelectionInfo().IsTextSelected()) 
		GetSelectionInfo().DisableSelectArea( true );

	if( 0 != pReconv->dwVersion ){
		return 0;
	}
	
	DWORD dwOffset, dwLen;
	dwOffset = pReconv->dwCompStrOffset/sizeof(WCHAR);	//0またはデータ長。バイト単位。→文字単位
	dwLen    = pReconv->dwCompStrLen;					//0または文字列長。文字単位。
	
	//選択開始の位置を取得
	m_pcEditDoc->m_cLayoutMgr.LogicToLayout(
		CLogicPoint(m_nLastReconvIndex + dwOffset, m_nLastReconvLine),
		GetSelectionInfo().m_sSelect.GetFromPointer()
	);

	//選択終了の位置を取得
	m_pcEditDoc->m_cLayoutMgr.LogicToLayout(
		CLogicPoint(m_nLastReconvIndex + dwOffset + dwLen, m_nLastReconvLine),
		GetSelectionInfo().m_sSelect.GetToPointer()
	);

	// 単語の先頭にカーソルを移動
	GetCaret().MoveCursor( GetSelectionInfo().m_sSelect.GetFrom(), true );

	//選択範囲再描画 
	GetSelectionInfo().DrawSelectArea();

	// 再変換情報の破棄
	m_nLastReconvIndex = -1;
	m_nLastReconvLine  = -1;

	return 1;
}

const CLayout* CEditView::SearchDisplayLine(CLayoutInt y) const
{
	const CLayoutMgr& original = m_pcEditDoc->m_cLayoutMgr;

	if (!m_ime.displayValid || !m_imeLayout) {
		return original.SearchLineByLayoutY(y);
	}

	const CLayoutInt first = m_ime.firstLayoutLine;
	const CLayoutInt displayLines = m_imeLayout->GetLineCount();

	// 未確定文字を含む行より前。
	if (y < first) {
		return original.SearchLineByLayoutY(y);
	}

	// 未確定文字を含む表示用レイアウト。
	if (y < first + displayLines) {
		return m_imeLayout->SearchLineByLayoutY(y - first);
	}

	// 後続行。折り返しで増減した表示行数を補正する。
	return original.SearchLineByLayoutY(
		y - displayLines + m_ime.originalLayoutLines
	);
}

CLayoutInt CEditView::GetDisplayLineCount() const
{
	const CLayoutInt originalLines =
		m_pcEditDoc->m_cLayoutMgr.GetLineCount();

	if (!m_ime.displayValid || !m_imeLayout) {
		return originalLines;
	}

	return originalLines
		- m_ime.originalLayoutLines
		+ m_imeLayout->GetLineCount();
}

void CEditView::RebuildImeDisplayLayout()
{
	m_ime.displayValid = false;

	// レイアウトは行データを参照しているので、この順に破棄する。
	m_imeLayout.reset();
	m_imeLine.reset();

	if (!m_ime.active || m_bImeNativeComposition || m_ime.text.empty()) {
		return;
	}

	auto& original = m_pcEditDoc->m_cLayoutMgr;
	auto& lines = m_pcEditDoc->m_cDocLineMgr;

	const CDocLine* head = lines.GetLine(m_ime.begin.y);
	const CDocLine* tail = lines.GetLine(m_ime.end.y);

	std::wstring display;

	// 挿入位置より前の本文。
	const int headLength = head
		? static_cast<int>(head->GetLengthWithoutEOL())
		: 0;

	const int beginX = static_cast<int>(m_ime.begin.x);
	const int prefixLength = std::min(beginX, headLength);

	if (head && prefixLength > 0) {
		display.assign(head->GetPtr(), prefixLength);
	}

	// 行末より右のフリーカーソル位置に対応する空白。
	if (beginX > headLength) {
		display.append(beginX - headLength, L' ');
	}

	// 未確定文字を表示用文字列にだけ挿入する。
	display += m_ime.text;

	// 挿入位置、または選択終了位置より後の本文。
	if (tail) {
		const int endX = std::min(
			static_cast<int>(m_ime.end.x),
			static_cast<int>(tail->GetLengthWithoutEOL())
		);

		display.append(
			tail->GetPtr() + endX,
			static_cast<int>(tail->GetLengthWithEOL()) - endX
		);
	}

	// 置換対象の最初の表示行を求める。
	CLayoutPoint first;
	original.LogicToLayout(
		CLogicPoint(CLogicInt(0), m_ime.begin.y),
		&first
	);

	m_ime.firstLayoutLine = first.y;

	// 元の表示行を何行置き換えるか数える。
	CLayoutInt after = first.y;

	while (const CLayout* layout =
		original.SearchLineByLayoutY(after)) {
		if (layout->GetLogicLineNo() > m_ime.end.y) {
			break;
		}
		after += CLayoutInt(1);
	}

	m_ime.originalLayoutLines = after - first.y;

	const CLayout* firstOriginal =
		original.SearchLineByLayoutY(first.y);

	const EColorIndexType initialColor = firstOriginal
		? firstOriginal->GetColorTypePrev()
		: COLORIDX_DEFAULT;

	const CLayoutColorInfo* initialColorInfo = firstOriginal
		? firstOriginal->GetColorInfo()
		: nullptr;

	m_imeLine = std::make_unique<CDocLine>();

	m_imeLine->SetDocLineString(
		display.c_str(),
		static_cast<int>(display.size()),
		GetDllShareData().m_Common.m_sEdit.m_bEnableExtEol
	);

	m_imeLayout = std::make_unique<CLayoutMgr>();

	m_imeLayout->BuildDisplayLine(
		original,
		m_imeLine.get(),
		m_ime.begin.y,
		initialColor,
		initialColorInfo
	);

	m_ime.displayValid = true;
}
void CEditView::BeginImeComposition()
{
	m_imeLayout.reset();
	m_imeLine.reset();
	m_ime = SImeComposition{};

	m_ime.active = true;

	auto& layout = m_pcEditDoc->m_cLayoutMgr;

	if (!m_bImeNativeComposition && GetSelectionInfo().IsTextSelected()) {
		layout.LayoutToLogic(
			GetSelectionInfo().m_sSelect.GetFrom(),
			&m_ime.begin
		);
		layout.LayoutToLogic(
			GetSelectionInfo().m_sSelect.GetTo(),
			&m_ime.end
		);
	} else {
		m_ime.begin = GetCaret().GetCaretLogicPos();
		m_ime.end = m_ime.begin;
	}

	m_szComposition[0] = L'\0';
}

void CEditView::EndImeComposition()
{
	m_bImeNativeComposition = false;

	m_ime.displayValid = false;
	m_ime.active = false;

	m_imeLayout.reset();
	m_imeLine.reset();
	m_ime = SImeComposition{};

	m_szComposition[0] = L'\0';

	// 状態解除後なので、通常の本文キャレットが基準になる。
	EnsureDisplayCaretVisible();

	Redraw();
	GetCaret().ShowEditCaret();
}

void CEditView::CommitImeResultViaDefault(
	HWND hwnd,
	WPARAM wParam,
	LPARAM resultFlags
)
{
	std::wstring result;

	{
		// 呼び出し終了時に、以前のバッファ参照へ必ず戻す。
		// ローカル変数へのポインタをメンバーに残さない。
		struct SRestoreResultBuffer {
			std::wstring*& slot;
			std::wstring* previous;

			~SRestoreResultBuffer()
			{
				slot = previous;
			}
		};

		const SRestoreResultBuffer restore{
			m_pImeResultBuffer,
			m_pImeResultBuffer
		};

		m_pImeResultBuffer = &result;

		// この呼び出し中に届くWM_IME_CHARでは、
		// 本文を変更せずresultへ文字を追加する。
		::DefWindowProc(
			hwnd,
			WM_IME_COMPOSITION,
			wParam,
			resultFlags
		);
	}

	// ここでは収集状態を解除済み。
	// 受信した確定文字列を1回だけ編集コマンドへ渡す。
	if (!result.empty()) {
		CommitImeResult(result);
		RefreshImeDisplay();
	}
}

void CEditView::CommitImeResult(const std::wstring& text)
{
	// 編集コマンドによる再描画では通常の本文を参照する。
	m_ime.displayValid = false;
	m_ime.active = false;
	m_imeLayout.reset();
	m_imeLine.reset();
	m_szComposition[0] = L'\0';

	if (!text.empty()) {
		const BOOL oldHokan = m_bHokan;

		if (m_bHideMouse && 0 <= m_nMousePause) {
			m_nMousePause = -1;
			::SetCursor(nullptr);
		}

		if (!IsInsMode()) {
			for (wchar_t ch : text) {
				GetCommander().HandleCommand(
					F_IME_CHAR, true, ch, 0, 0, 0
				);
			}
		} else if (
			text.size() == 1 &&
			GetCommander().GetAutoCloseBracket(text[0]) != L'\0'
		) {
			GetCommander().HandleCommand(
				F_WCHAR, true, text[0], 0, 0, 0
			);
		} else {
			GetCommander().HandleCommand(
				F_INSTEXT_W,
				true,
				reinterpret_cast<LPARAM>(text.c_str()),
				static_cast<LPARAM>(text.size()),
				TRUE,
				0
			);
		}

		m_bHokan = oldHokan;
		PostprocessCommand_hokan();
	}

	// 部分確定後の新しい挿入位置を保存する。
	BeginImeComposition();
}

bool CEditView::OnImeComposition(LPARAM flags)
{
	const bool hasResult = (flags & GCS_RESULTSTR) != 0;
	const bool hasComposition = (flags & GCS_COMPSTR) != 0;

	std::vector<wchar_t> result;
	std::vector<wchar_t> composition;
	std::vector<BYTE> attributes;
	std::vector<DWORD> clauses;

	// 確定文字と未確定文字の取得成否を別々に管理する。
	bool resultRead = !hasResult;
	bool compositionRead = !hasComposition;
	bool attributesRead = false;
	bool clausesRead = false;
	LONG cursor = -1;

	HIMC context = ::ImmGetContext(GetHwnd());
	if (!context) {
		// 確定文字がある通知だけ、呼び出し側へ未処理を返す。
		return !hasResult;
	}

	if (hasResult) {
		resultRead = ReadImeData(
			context, GCS_RESULTSTR, result
		);
	}

	if (hasComposition) {
		compositionRead = ReadImeData(
			context, GCS_COMPSTR, composition
		);
	}

	if (flags & GCS_COMPATTR) {
		attributesRead = ReadImeData(
			context, GCS_COMPATTR, attributes
		);
	}

	if (flags & GCS_COMPCLAUSE) {
		clausesRead = ReadImeData(
			context, GCS_COMPCLAUSE, clauses
		);
	}

	if (flags & GCS_CURSORPOS) {
		cursor = ::ImmGetCompositionStringW(
			context, GCS_CURSORPOS, nullptr, 0
		);
	}

	::ImmReleaseContext(GetHwnd(), context);

	if (!resultRead) {
		// 確定文字の取得失敗。
		// 状態や本文を変更せず、標準処理への引き継ぎを要求する。
		return false;
	}

	if (!m_ime.active) {
		BeginImeComposition();
	}

	if (hasResult) {
		// 未確定文字の取得に失敗していても、
		// 取得済みの確定文字はここで処理する。
		CommitImeResult(
			std::wstring(result.begin(), result.end())
		);
	}

	if (compositionRead) {
		// 結果文字列と未確定文字列は、同じ通知に含まれ得る。
		if (hasComposition) {
			m_ime.text.assign(
				composition.begin(), composition.end()
			);
			m_ime.attributes.assign(
				m_ime.text.size(), ATTR_INPUT
			);
			m_ime.clauses.clear();
		}

		if (attributesRead) {
			m_ime.attributes = std::move(attributes);
		}

		if (clausesRead) {
			m_ime.clauses = std::move(clauses);
		}

		if (cursor >= 0) {
			m_ime.cursor = cursor;
		}
	}
	// 新しい未確定文字列の取得に失敗した場合は、
	// その文字列に対応する属性・文節・カーソルも適用しない。
	// 確定済み部分の本文への挿入は取り消さない。

	if (flags == 0) {
		m_ime.text.clear();
		m_ime.attributes.clear();
		m_ime.clauses.clear();
		m_ime.cursor = 0;
	}

	m_ime.cursor = std::clamp(
		m_ime.cursor,
		LONG(0),
		static_cast<LONG>(m_ime.text.size())
	);

	// IMR_DOCUMENTFEED用バッファも更新する。
	const size_t length = std::min(
		m_ime.text.size(),
		std::size(m_szComposition) - 1
	);

	std::copy_n(m_ime.text.data(), length, m_szComposition);
	m_szComposition[length] = L'\0';

	RefreshImeDisplay();
	return true;
}

CLayoutPoint CEditView::GetDisplayCaretLayoutPos() const
{
	if (!m_ime.displayValid || !m_imeLayout) {
		return GetCaret().GetCaretLayoutPos();
	}

	const CLogicInt target =
		m_ime.begin.x + CLogicInt(m_ime.cursor);

	const CLayout* line = m_imeLayout->GetTopLayout();
	CLayoutInt y = m_ime.firstLayoutLine;

	if (!line) {
		return GetCaret().GetCaretLayoutPos();
	}

	while (line->GetNextLayout() &&
		line->GetNextLayout()->GetLogicOffset() <= target) {
		line = line->GetNextLayout();
		y += CLayoutInt(1);
	}

	auto iterator = m_imeLayout->CreateCMemoryIterator(line);

	while (!iterator.end() &&
		line->GetLogicOffset() + iterator.getIndex() < target) {
		iterator.scanNext();
		iterator.addDelta();
	}

	return CLayoutPoint(iterator.getColumn(), y);
}

void CEditView::UpdateImeCandidatePosition()
{
	if (!m_ime.active || m_bImeNativeComposition) {
		return;
	}

	HIMC context = ::ImmGetContext(GetHwnd());
	if (!context) {
		return;
	}

	const POINT point =
		GetCaret().CalcCaretDrawPos(GetDisplayCaretLayoutPos());

	CANDIDATEFORM form{};
	form.dwIndex = 0;
	form.dwStyle = CFS_EXCLUDE;
	form.ptCurrentPos = point;
	form.rcArea.left = point.x;
	form.rcArea.top = point.y;
	form.rcArea.right = point.x + 1;
	form.rcArea.bottom =
		point.y + GetTextMetrics().GetHankakuDy();

	::ImmSetCandidateWindow(context, &form);
	::ImmReleaseContext(GetHwnd(), context);
}


void CEditView::RefreshImeDisplay()
{
	if (!m_ime.active) {
		return;
	}

	if (m_bImeNativeComposition) {
		SetIMECompFormPos();
		return;
	}

	RebuildImeDisplayLayout();
	EnsureDisplayCaretVisible();

	Redraw();
	GetCaret().ShowEditCaret();
	UpdateImeCandidatePosition();
}

bool CEditView::IsImeInlineDisplay() const
{
	return !m_bImeNativeComposition
		&& m_ime.active
		&& m_ime.displayValid;
}

void CEditView::InvalidateImeDisplayLayout()
{
	m_ime.displayValid = false;
}

bool CEditView::CompleteImeBeforeMouseAction()
{
	if (m_bImeCompleting) {
		return false;
	}

	if (!m_ime.active) {
		return true;
	}

	HIMC context = ::ImmGetContext(GetHwnd());
	if (!context) {
		return false;
	}

	m_bImeCompleting = true;

	// この呼び出し中にWM_IME_COMPOSITIONなどが届き、
	// 既存の確定処理が本文へ結果文字列を挿入する。
	const BOOL notified = ::ImmNotifyIME(
		context,
		NI_COMPOSITIONSTR,
		CPS_COMPLETE,
		0
	);

	const LONG remaining = ::ImmGetCompositionStringW(
		context,
		GCS_COMPSTR,
		nullptr,
		0
	);

	::ImmReleaseContext(GetHwnd(), context);

	m_bImeCompleting = false;

	// 確定できなかった場合は、クリックによる移動を中止する。
	// EndImeComposition()で未確定文字を強制破棄しない。
	if (!notified || remaining != 0) {
		return false;
	}

	return !m_ime.active || m_ime.text.empty();
}

bool CEditView::ShouldUseNativeIme() const
{
	return GetSelectionInfo().IsBoxSelecting() || !IsInsMode();
}

void CEditView::EnsureDisplayCaretVisible()
{
	// 表示行数・最大幅が減った場合のスクロール位置を補正する。
	ScrollAtV(GetTextArea().GetViewTopLine());
	ScrollAtH(GetTextArea().GetViewLeftCol());

	const CLayoutPoint caret = GetDisplayCaretLayoutPos();

	if (caret.y < GetTextArea().GetViewTopLine()) {
		ScrollAtV(caret.y);
	} else if (caret.y >= GetTextArea().GetBottomLine()) {
		ScrollAtV(
			caret.y - GetTextArea().m_nViewRowNum + CLayoutInt(1)
		);
	}

	if (caret.x < GetTextArea().GetViewLeftCol()) {
		ScrollAtH(caret.x);
	} else if (caret.x >= GetTextArea().GetRightCol()) {
		ScrollAtH(
			caret.x - GetTextArea().m_nViewColNum
				+ GetTextMetrics().GetLayoutXDefault()
		);
	}

	AdjustScrollBars();
}

void CEditView::SaveDisplayTopPosition(
	CLogicPointEx* position
) const
{
	*position = CLogicPointEx{};

	const CLayoutInt top = GetTextArea().GetViewTopLine();
	const CLayout* line = SearchDisplayLine(top);

	if (!line) {
		// EOF以下の余白も、先頭行へ戻さず保存する。
		position->ext = CLayoutInt(-2);
		position->x = CLogicInt(
			static_cast<int>(top - GetDisplayLineCount())
		);
		return;
	}

	// 従来と同じく、上端行の中央付近の文字を目印にする。
	position->x =
		line->GetLogicOffset() + line->GetLengthWithoutEOL() / 2;
	position->y = line->GetLogicLineNo();

	if (IsImeInlineDisplay() &&
		line->GetDocLineRef() == m_imeLine.get()) {
		// xは本文ではなく、仮の文字列内のオフセット。
		position->ext = CLayoutInt(-1);
	}
}

CLayoutInt CEditView::RestoreDisplayTopPosition(
	const CLogicPointEx& position
)
{
	// 本文レイアウト・フォント更新後の設定で作り直す。
	RebuildImeDisplayLayout();

	if (position.ext == CLayoutInt(-2)) {
		return GetDisplayLineCount()
			+ CLayoutInt(static_cast<int>(position.x));
	}

	if (position.ext == CLayoutInt(-1) &&
		IsImeInlineDisplay() &&
		m_imeLayout &&
		position.y == m_ime.begin.y) {
		// 仮の文字列内の同じ文字が含まれる表示行を探す。
		const CLayout* line = m_imeLayout->GetTopLayout();
		CLayoutInt y = m_ime.firstLayoutLine;

		if (line) {
			while (line->GetNextLayout() &&
				line->GetNextLayout()->GetLogicOffset()
					<= position.x) {
				line = line->GetNextLayout();
				y += CLayoutInt(1);
			}

			return y;
		}
	}

	CLogicPoint logical(position.x, position.y);

	if (position.ext == CLayoutInt(-1)) {
		// 保存後に変換が終了していた場合。
		// 仮の文字列のオフセットを本文へ直接渡さない。
		logical.x = CLogicInt(0);
	}

	CLayoutPoint original;
	m_pcEditDoc->m_cLayoutMgr.LogicToLayout(logical, &original);

	if (!IsImeInlineDisplay() || !m_imeLayout) {
		return original.y;
	}

	const CLayoutInt first = m_ime.firstLayoutLine;
	const CLayoutInt originalAfter =
		first + m_ime.originalLayoutLines;

	if (original.y >= originalAfter) {
		// IME表示より後ろの本文行。
		return original.y
			- m_ime.originalLayoutLines
			+ m_imeLayout->GetLineCount();
	}

	if (original.y >= first) {
		return first;
	}

	return original.y;
}

void CEditView::RefreshImeDisplayAfterRelayout()
{
	if (!m_ime.active) {
		return;
	}

	if (m_bImeNativeComposition) {
		SetIMECompFormPos();
		return;
	}

	RebuildImeDisplayLayout();

	// 有効範囲から外れたスクロール位置だけを補正する。
	ScrollAtV(GetTextArea().GetViewTopLine());
	ScrollAtH(GetTextArea().GetViewLeftCol());
	AdjustScrollBars();

	Redraw();
	GetCaret().ShowEditCaret();
	UpdateImeCandidatePosition();
}