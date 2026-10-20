/*!	@file
@brief CViewCommanderクラスのコマンド(挿入系)関数群

	2012/12/15	CViewCommander.cpp,CViewCommander_New.cppから分離
*/
/*
	Copyright (C) 1998-2001, Norio Nakatani
	Copyright (C) 2002, MIK
	Copyright (C) 2018-2022, Sakura Editor Organization

	This source code is designed for sakura editor.
	Please contact the copyright holders to use this code for other purpose.
*/

#include "StdAfx.h"
#include "CViewCommander.h"
#include "CViewCommander_inline.h"

#include "dlg/CDlgCtrlCode.h"	//コントロールコードの入力(ダイアログ)
#include "env/CFormatManager.h"
#include "recent/CMRUFile.h"
#include "recent/CMRUFolder.h"
#include "util/ImageLink.h"
#include "util/Freeboard.h"
#include "dlg/CDlgInput1.h"
#include "util/window.h"
#include "util/os.h"
#include "sakura_rc.h"
#include "CSelectLang.h"
#include <commdlg.h>
#include <filesystem>

namespace {
bool ValidBoardName(std::wstring_view name)
{
	if (name.empty() || name.front() == L' ' || name.back() == L' ' || name.back() == L'.' ||
		name.find_first_of(L"<>:\"/\\|?*") != name.npos) return false;
	for (wchar_t c : name) if (c < 32) return false;
	const auto dot = name.find(L'.');
	std::wstring stem(name.substr(0, dot));
	std::transform(stem.begin(), stem.end(), stem.begin(), towupper);
	if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL") return false;
	if (stem.size() == 4 && ((stem.starts_with(L"COM") || stem.starts_with(L"LPT")) &&
		stem[3] >= L'1' && stem[3] <= L'9')) return false;
	return true;
}

}

void CViewCommander::Command_CREATE_FREEBOARD()
{
	auto* doc = GetDocument();
	auto* view = m_pCommanderView;
	if (!doc->IsEditable() || view->GetSelectionInfo().IsBoxSelecting()) return;
	if (!doc->m_cDocFile.GetFilePathClass().IsValidPath() && !Command_FILESAVEAS_DIALOG(nullptr, CODE_NONE, EEolType::none)) return;
	if (!doc->m_cDocFile.GetFilePathClass().IsValidPath()) return;
	const std::wstring documentPath(doc->m_cDocFile.GetFilePath());
	const auto caret = view->GetCaret().GetCaretLogicPos();
	const auto selection = view->GetSelectionInfo().m_sSelect;
	const auto lineCount = doc->m_cDocLineMgr.GetLineCount();
	std::vector<std::wstring> originalLines;
	for (CLogicInt i(0); i < lineCount; ++i) {
		const auto line = doc->m_cDocLineMgr.GetLine(i);
		originalLines.emplace_back(line->GetPtr(), line->GetLengthWithEOL());
	}
	const auto unchanged = [&] {
		if (std::wstring(doc->m_cDocFile.GetFilePath()) != documentPath || !doc->IsEditable() ||
			view->GetCaret().GetCaretLogicPos() != caret || view->GetSelectionInfo().m_sSelect != selection ||
			doc->m_cDocLineMgr.GetLineCount() != lineCount) return false;
		for (CLogicInt i(0); i < lineCount; ++i) {
			const auto line = doc->m_cDocLineMgr.GetLine(i);
			if (originalLines[static_cast<int>(i)] != std::wstring(line->GetPtr(), line->GetLengthWithEOL())) return false;
		}
		return true;
	};
	SYSTEMTIME now{};
	::GetLocalTime(&now);
	wchar_t defaultName[64]{};
	swprintf_s(defaultName, L"%04u%02u%02u_%02u%02u%02u", now.wYear, now.wMonth, now.wDay,
		now.wHour, now.wMinute, now.wSecond);
	wchar_t input[256]{};
	wcscpy_s(input, defaultName);
	std::wstring name;
	if (!CDlgInput1().DoModal(view->GetHwnd(), L"フリーボードを作成", L"画像ファイル名（拡張子を除く）",
		std::span(input),
		[&name](HWND dialog, std::wstring_view, size_t) {
			wchar_t typed[256]{};
			::GetDlgItemTextW(dialog, IDC_EDIT_INPUT1, typed, static_cast<int>(std::size(typed)));
			std::wstring stem(typed);
			if (stem.size() >= 4 && _wcsicmp(stem.c_str() + stem.size() - 4, L".png") == 0) stem.resize(stem.size() - 4);
			if (ValidBoardName(stem)) { name = typed; return 1; }
			::MessageBoxW(dialog, L"使用できないファイル名です。修正してください。", L"フリーボード", MB_OK | MB_ICONWARNING);
			return -1;
		})) return;
	if (name.size() >= 4 && _wcsicmp(name.c_str() + name.size() - 4, L".png") == 0) name.resize(name.size() - 4);
	if (!unchanged()) return;
	const auto directory = std::filesystem::path(documentPath).parent_path();
	Freeboard::Creation creation;
	const auto cancel = [&] {
		if (!creation.Cancel()) {
			const auto message = L"作成したPNGを後始末できませんでした。残存先：\n" + creation.Path();
			::MessageBoxW(view->GetHwnd(), message.c_str(), L"フリーボード", MB_OK | MB_ICONWARNING);
		}
	};
	if (!creation.Create(directory, name)) {
		cancel();
		::MessageBoxW(view->GetHwnd(), L"白いPNGを作成できませんでした。", L"フリーボード", MB_OK | MB_ICONERROR);
		return;
	}
	if (!unchanged()) {
		cancel();
		return;
	}
	const auto& path = creation.Path();
	const auto& link = creation.Link();
	bool inserted;
	try {
		inserted = view->InsertStandaloneNoteLink(link);
	} catch (const std::exception&) {
		// A partially completed text edit is ambiguous: keep the PNG and report its path.
		creation.Keep();
		const auto message = L"画像リンクの挿入を完了できませんでした。PNGは次の場所に残しています：\n" + path;
		::MessageBoxW(view->GetHwnd(), message.c_str(), L"フリーボード", MB_OK | MB_ICONERROR);
		return;
	}
	if (!inserted) {
		cancel();
		::MessageBoxW(view->GetHwnd(), L"画像リンクを挿入できませんでした。", L"フリーボード", MB_OK | MB_ICONERROR);
		return;
	}
	creation.Keep();
	// Any paint during insertion may have observed the exclusive handle. Retry now
	// that the fully written file is available to the asynchronous image decoder.
	const auto imagePath = ImageLink::FindStandalone(link)->path;
	GetEditWindow()->GetNoteImageCache().Invalidate(imagePath);
	for (int i = 0; i < GetEditWindow()->GetAllViewCount(); ++i)
		GetEditWindow()->GetView(i).RefreshNoteImage(imagePath);
	GetEditWindow()->RedrawAllViews(nullptr);
	view->GetCaret().MoveCursor(view->GetCaret().GetCaretLayoutPos(), true);
	if (reinterpret_cast<INT_PTR>(::ShellExecuteW(view->GetHwnd(), L"open", L"mspaint.exe",
		(L"\"" + path + L"\"").c_str(), nullptr, SW_SHOWNORMAL)) <= 32)
		::MessageBoxW(view->GetHwnd(), L"PNGとリンクは作成しましたが、ペイントを起動できませんでした。", L"フリーボード", MB_OK | MB_ICONWARNING);
}

void CViewCommander::Command_INS_IMAGE_LINK()
{
	if (!GetDocument()->IsEditable() || m_pCommanderView->GetSelectionInfo().IsBoxSelecting()) return;
	wchar_t path[32768]{};
	// OPENFILENAME uses a double-NUL-terminated filter list.
	std::wstring filter = LS(STR_IMAGE_LINK_FILTER);
	filter.push_back(L'\0');
	filter += L"*.png;*.jpg;*.jpeg";
	filter.append(2, L'\0');
	OPENFILENAMEW dialog{sizeof(dialog)};
	dialog.hwndOwner = m_pCommanderView->GetHwnd();
	dialog.lpstrTitle = LS(F_INS_IMAGE_LINK);
	dialog.lpstrFilter = filter.c_str();
	dialog.lpstrFile = path;
	dialog.nMaxFile = static_cast<DWORD>(std::size(path));
	dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
	if (!Comdlg32::getInstance()->GetOpenFileNameW(&dialog)) return;
	const auto link = ImageLink::MakeLink(path);
	if (!link || !ImageLink::IsReadableFile(path)) {
		InfoMessage(m_pCommanderView->GetHwnd(), LS(STR_IMAGE_LINK_ERROR));
		return;
	}
	const auto text = m_pCommanderView->PrepareNoteInsertion(*link);
	Command_INSTEXT(true, text.c_str(), text.size(), true);
}

//日付挿入
void CViewCommander::Command_INS_DATE( void )
{
	// 日付をフォーマット
	WCHAR szText[1024];
	SYSTEMTIME systime;
	::GetLocalTime( &systime );
	CFormatManager().MyGetDateFormat( systime, szText, int(std::size(szText)) - 1 );

	// テキストを貼り付け ver1
	Command_INSTEXT( true, szText, CLogicInt(-1), TRUE );
}

//時刻挿入
void CViewCommander::Command_INS_TIME( void )
{
	// 時刻をフォーマット
	WCHAR szText[1024];
	SYSTEMTIME systime;
	::GetLocalTime( &systime );
	CFormatManager().MyGetTimeFormat( systime, szText, int(std::size(szText)) - 1 );

	// テキストを貼り付け ver1
	Command_INSTEXT( true, szText, CLogicInt(-1), TRUE );
}

//	from CViewCommander_New.cpp
/*!	コントロールコードの入力(ダイアログ)
	@author	MIK
	@date	2002/06/02
*/
void CViewCommander::Command_CtrlCode_Dialog( void )
{
	CDlgCtrlCode	cDlgCtrlCode;

	//コントロールコード入力ダイアログを表示する
	if( cDlgCtrlCode.DoModal( G_AppInstance(), m_pCommanderView->GetHwnd(), (LPARAM)GetDocument() ) )
	{
		//コントロールコードを入力する
		// 2013.06.11 Command_WCHAR -> HandleCommand マクロ記録対応
		// 2013.12.12 F_WCHAR -> F_CTRL_CODE
		HandleCommand( F_CTRL_CODE, true, cDlgCtrlCode.GetCharCode(), 0, 0, 0 );
	}
}

// 最近使ったファイル挿入
void CViewCommander::Command_INS_FILE_USED_RECENTLY( void )
{
	std::wstring eol = GetDocument()->m_cDocEditor.GetNewLineCode().GetValue2();
	std::wstring s;
	CMRUFile cMRUFile;
	auto list = cMRUFile.GetPathList();
	for (auto item : list) {
		s += item;
		s += eol;
	}
	Command_INSTEXT( true, s.c_str(), (CLogicInt)s.size(), TRUE );
}

// 最近使ったフォルダー挿入
void CViewCommander::Command_INS_FOLDER_USED_RECENTLY( void )
{
	std::wstring eol = GetDocument()->m_cDocEditor.GetNewLineCode().GetValue2();
	std::wstring s;
	CMRUFolder cMRUFolder;
	auto list = cMRUFolder.GetPathList();
	for (auto item : list) {
		s += item;
		s += eol;
	}
	Command_INSTEXT( true, s.c_str(), (CLogicInt)s.size(), TRUE );
}
