/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "pch.h"
#include "util/ImageLink.h"
#include "window/CImagePreviewWnd.h"
#include "cxx/com_pointer.hpp"
#include "window/EditorTestSuite.hpp"
#include "_main/CAppMode.h"
#include "util/os.h"
#include <fstream>
#include <wincodec.h>

std::filesystem::path GetTempFilePathWithExt(std::wstring_view prefix, std::wstring_view extension);

TEST(ImageLink, GeneratedLinksRoundTripAtEveryCharacter)
{
	const std::pair<std::wstring, std::wstring> examples[] = {
		{LR"(C:\Users\学生\Pictures\図.png)", L"![図](C:/Users/学生/Pictures/図.png)"},
		{L"D:/My Pictures/図 (1).JpEg", L"![図 (1)](<D:/My Pictures/図 (1).JpEg>)"},
		{L"C:/a]b.png", LR"(![a\]b](C:/a]b.png))"},
		{L"C:/a(b).jpg", L"![a(b)](<C:/a(b).jpg>)"},
	};
	for (const auto& [path, expected] : examples) {
		const auto generated = ImageLink::MakeLink(path);
		ASSERT_TRUE(generated);
		EXPECT_EQ(expected, *generated);
		const auto line = L"文中: " + *generated + L" end";
		for (size_t i = 4; i < 4 + generated->size(); ++i) {
			const auto found = ImageLink::FindAt(line, i);
			ASSERT_TRUE(found) << i;
			EXPECT_EQ(4u, found->begin);
			EXPECT_EQ(4 + generated->size(), found->end);
		}
		EXPECT_FALSE(ImageLink::FindAt(line, 3));
		EXPECT_FALSE(ImageLink::FindAt(line, 4 + generated->size()));
	}
}

TEST(ImageLink, HandwrittenBackslashesEmptyDescriptionAndEscapes)
{
	const auto link = ImageLink::FindAt(LR"(![a\]b\\c](C:\学生\図.png))", 0);
	ASSERT_TRUE(link);
	EXPECT_EQ(LR"(C:\学生\図.png)", link->path);
	EXPECT_TRUE(ImageLink::FindAt(L"![](C:/a.png)", 0));
	EXPECT_TRUE(ImageLink::FindAt(L"![図](<C:\\My Pictures\\図 (1).jpg>)", 9));
}

TEST(ImageLink, MultipleLinksAndLogicalLineBoundary)
{
	const std::wstring line = L"![a](C:/a.png) ![b](D:/b.JPG)";
	const auto second = ImageLink::FindAt(line, line.size() - 1);
	ASSERT_TRUE(second);
	EXPECT_EQ(L"D:/b.JPG", second->path);
	EXPECT_EQ(line.find(L"![b]"), second->begin);
	EXPECT_FALSE(ImageLink::FindAt(line, line.size()));
	EXPECT_FALSE(ImageLink::FindAt(L"![a\n](C:/a.png)", 0));
	EXPECT_FALSE(ImageLink::FindAt(L"![a](<C:/a\r\nb.png>)", 0));
}

TEST(ImageLink, ExcludesOtherSyntaxAndPaths)
{
	const wchar_t* invalid[] = {
		L"[a](C:/a.png)", L"C:/a.png", L"![a](a.png)", L"![a](C:a.png)",
		L"![a](https://example.com/a.png)", L"![a](file:///C:/a.png)",
		LR"(![a](\\server\share\a.png))", L"![a](//server/share/a.png)",
		L"![a](C:/a.gif)", L"![a](C:/a.svg)", L"![a](C:/a.bmp)",
		L"![a](C:/a b.png)", L"![a](C:/a(b).png)", L"![a](<C:/a.png)",
		L"![a](C:/a.png", L"![a](C:/a.png \"title\")", L"![a](C:/a.png:stream)",
	};
	for (const auto text : invalid) {
		for (size_t i = 0; i < std::wcslen(text); ++i) EXPECT_FALSE(ImageLink::FindAt(text, i));
	}
}

TEST(ImageLink, UnrepresentablePathsAreNotGenerated)
{
	for (const auto path : {L"C:/a<b.png", L"C:/a>b.png", L"C:/a\nb.png", L"C:/a\tb.png",
		L"C:/a?.png", L"C:/a.png:stream", L"C:relative.jpg", LR"(\\server\a.png)", L"C:/a.svg"}) {
		EXPECT_FALSE(ImageLink::MakeLink(path));
	}
	EXPECT_FALSE(ImageLink::MakeLink(std::wstring(L"C:/a\0b.png", 10)));
}

TEST(ImagePreview, DimensionLimits)
{
	EXPECT_TRUE(CImagePreviewWnd::IsAllowedSize(8192, 1));
	EXPECT_TRUE(CImagePreviewWnd::IsAllowedSize(4000, 5000));
	EXPECT_FALSE(CImagePreviewWnd::IsAllowedSize(0, 1));
	EXPECT_FALSE(CImagePreviewWnd::IsAllowedSize(1, 0));
	EXPECT_FALSE(CImagePreviewWnd::IsAllowedSize(8193, 1));
	EXPECT_FALSE(CImagePreviewWnd::IsAllowedSize(1, 8193));
	EXPECT_FALSE(CImagePreviewWnd::IsAllowedSize(4001, 5000));
	EXPECT_FALSE(CImagePreviewWnd::IsAllowedSize(UINT_MAX, UINT_MAX));
}

class ImagePreviewFile : public testing::Test {
protected:
	std::filesystem::path path;
	HRESULT com = E_FAIL;
	void SetUp() override
	{
		com = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		ASSERT_TRUE(SUCCEEDED(com) || com == RPC_E_CHANGED_MODE);
		path = GetTempFilePathWithExt(L"img", L"png");
	}
	void TearDown() override
	{
		std::error_code error;
		std::filesystem::remove(path, error);
		if (SUCCEEDED(com)) ::CoUninitialize();
	}
	void WriteImage(REFGUID container, UINT width, UINT height)
	{
		cxx::com_pointer<IWICImagingFactory> factory;
		ASSERT_HRESULT_SUCCEEDED(factory.CreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER));
		cxx::com_pointer<IWICStream> stream;
		ASSERT_HRESULT_SUCCEEDED(factory->CreateStream(&stream));
		ASSERT_HRESULT_SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
		cxx::com_pointer<IWICBitmapEncoder> encoder;
		ASSERT_HRESULT_SUCCEEDED(factory->CreateEncoder(container, nullptr, &encoder));
		ASSERT_HRESULT_SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache));
		cxx::com_pointer<IWICBitmapFrameEncode> frame;
		ASSERT_HRESULT_SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr));
		ASSERT_HRESULT_SUCCEEDED(frame->Initialize(nullptr));
		ASSERT_HRESULT_SUCCEEDED(frame->SetSize(width, height));
		GUID format = GUID_WICPixelFormat24bppBGR;
		ASSERT_HRESULT_SUCCEEDED(frame->SetPixelFormat(&format));
		ASSERT_TRUE(IsEqualGUID(format, GUID_WICPixelFormat24bppBGR));
		std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 3, 128);
		ASSERT_HRESULT_SUCCEEDED(frame->WritePixels(height, width * 3, static_cast<UINT>(pixels.size()), pixels.data()));
		ASSERT_HRESULT_SUCCEEDED(frame->Commit());
		ASSERT_HRESULT_SUCCEEDED(encoder->Commit());
	}
};

TEST_F(ImagePreviewFile, PngShrinksWithAspectRatioAndDoesNotUpscale)
{
	WriteImage(GUID_ContainerFormatPng, 800, 200);
	const auto image = CImagePreviewWnd::Decode(path.native(), 480, 360);
	ASSERT_TRUE(image);
	EXPECT_EQ(480u, image->width);
	EXPECT_EQ(120u, image->height);
	EXPECT_EQ(480u * 120 * 4, image->pixels.size());
	const auto original = CImagePreviewWnd::Decode(path.native(), 1000, 1000);
	ASSERT_TRUE(original);
	EXPECT_EQ(800u, original->width);
	EXPECT_EQ(200u, original->height);
}

TEST_F(ImagePreviewFile, JpegAndVerticalLimit)
{
	WriteImage(GUID_ContainerFormatJpeg, 200, 800);
	const auto image = CImagePreviewWnd::Decode(path.native(), 480, 360);
	ASSERT_TRUE(image);
	EXPECT_EQ(90u, image->width);
	EXPECT_EQ(360u, image->height);
}

TEST_F(ImagePreviewFile, RejectsWrongContainerEvenWithPngExtension)
{
	WriteImage(GUID_ContainerFormatBmp, 8, 8);
	EXPECT_FALSE(CImagePreviewWnd::Decode(path.native(), 480, 360));
}

TEST_F(ImagePreviewFile, OversizedHeaderIsRejected)
{
	WriteImage(GUID_ContainerFormatPng, 8193, 1);
	EXPECT_FALSE(CImagePreviewWnd::Decode(path.native(), 480, 360));
}

TEST_F(ImagePreviewFile, MissingCorruptAndNextValidImage)
{
	std::filesystem::remove(path);
	EXPECT_FALSE(CImagePreviewWnd::Decode(path.native(), 480, 360));
	{ std::ofstream file(path, std::ios::binary); file << "broken image"; }
	EXPECT_TRUE(ImageLink::IsReadableFile(path.native())); // insertion does not decode
	EXPECT_FALSE(CImagePreviewWnd::Decode(path.native(), 480, 360));
	WriteImage(GUID_ContainerFormatPng, 10, 10);
	EXPECT_TRUE(CImagePreviewWnd::Decode(path.native(), 480, 360));
}

TEST_F(ImagePreviewFile, PopupDoesNotActivateAndCancelledResultsStayHidden)
{
	WriteImage(GUID_ContainerFormatPng, 800, 200);
	const HWND owner = ::CreateWindowExW(0, L"STATIC", L"Image preview test", WS_OVERLAPPED,
		0, 0, 100, 100, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
	ASSERT_NE(nullptr, owner);
	{
		CImagePreviewWnd popup;
		MONITORINFO monitor{sizeof(monitor)};
		ASSERT_TRUE(::GetMonitorInfoW(::MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor));
		const POINT point{monitor.rcWork.right - 2, monitor.rcWork.bottom - 2};
		const HWND focus = ::GetFocus();
		popup.Begin(owner, path.native(), point);
		HWND preview = nullptr;
		const auto started = ::GetTickCount();
		while (::GetTickCount() - started < 5000) {
			popup.Poll();
			preview = ::FindWindowW(L"SakuraImagePreview", nullptr);
			if (preview && ::IsWindowVisible(preview)) break;
			::Sleep(10);
		}
		EXPECT_NE(nullptr, preview);
		EXPECT_TRUE(::IsWindowVisible(preview));
		EXPECT_EQ(focus, ::GetFocus());
		EXPECT_NE(0, ::GetWindowLongPtrW(preview, GWL_EXSTYLE) & WS_EX_NOACTIVATE);
		EXPECT_EQ(MA_NOACTIVATE, ::SendMessageW(preview, WM_MOUSEACTIVATE, 0, 0));
		RECT rect{};
		ASSERT_TRUE(::GetWindowRect(preview, &rect));
		EXPECT_GE(rect.left, monitor.rcWork.left);
		EXPECT_GE(rect.top, monitor.rcWork.top);
		EXPECT_LE(rect.right, monitor.rcWork.right);
		EXPECT_LE(rect.bottom, monitor.rcWork.bottom);
		EXPECT_FALSE(::PtInRect(&rect, point));
		popup.Begin(owner, path.native(), point);
		popup.Hide();
		::Sleep(100);
		popup.Poll();
		EXPECT_FALSE(::IsWindowVisible(preview));
	}
	::DestroyWindow(owner);
}

class ImageLinkEditor : public testing::Test, public window::EditorTestSuite {
protected:
	std::filesystem::path path;
	static void SetUpTestSuite() { SetUpEditor(); }
	static void TearDownTestSuite() { TearDownEditor(); }
	void SetUp() override
	{
		User32::setInstance<MockUser32>();
		Comdlg32::setInstance<MockComdlg32>();
		CAppMode::getInstance()->SetViewMode(false);
		auto& view = pcEditWnd->GetActiveView();
		view.GetSelectionInfo().SetBoxSelect(false);
		view.GetCommander().HandleCommand(F_SELECTALL, true, 0, 0, 0, 0);
		view.GetCommander().HandleCommand(F_DELETE, true, 0, 0, 0, 0);
		view.GetTextArea().TextArea_OnSize(CMySize(800, 600), 0, 0);
		path = GetTempFilePathWithExt(L"img", L"png");
		{ std::ofstream file(path); file << "not decoded during insertion"; }
	}
	void TearDown() override
	{
		CAppMode::getInstance()->SetViewMode(false);
		pcEditWnd->GetActiveView().GetSelectionInfo().SetBoxSelect(false);
		pcEditDoc->m_cDocEditor.m_bIsDocModified = false;
		Comdlg32::resetInstance();
		User32::resetInstance();
		std::error_code error;
		std::filesystem::remove(path, error);
		MSG message{};
		while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {}
	}
	std::wstring Text()
	{
		std::wstring text;
		for (CLogicInt i(0); i < pcEditDoc->m_cDocLineMgr.GetLineCount(); ++i) {
			const auto line = pcEditDoc->m_cDocLineMgr.GetLine(i);
			text.append(line->GetPtr(), static_cast<size_t>(line->GetLengthWithEOL()));
		}
		return text;
	}
	void Command(EFunctionCode code) { pcEditWnd->GetActiveView().GetCommander().HandleCommand(code, true, 0, 0, 0, 0); }
	void Insert(std::wstring_view text)
	{
		pcEditWnd->GetActiveView().GetCommander().HandleCommand(F_INSTEXT_W, true, reinterpret_cast<LPARAM>(text.data()), text.size(), TRUE, 0);
	}
	POINT DropPoint(int column = 0)
	{
		auto& view = pcEditWnd->GetActiveView();
		// The in-process editor fixture has no native view window.
		view.GetTextArea().TextArea_OnSize(CMySize(800, 600), 0, 0);
		view.GetTextArea().SetViewTopLine(CLayoutInt(0));
		view.GetTextArea().SetViewLeftCol(CLayoutInt(0));
		const auto layout = pcEditDoc->m_cLayoutMgr.SearchLineByLayoutY(CLayoutInt(0));
		const auto x = layout ? view.LineIndexToColumn(layout, CLogicInt(column)) : CLayoutInt(0);
		return {view.GetTextArea().GetAreaLeft() + view.GetTextMetrics().GetCharPxWidth(x),
			view.GetTextArea().GetAreaTop() + 1};
	}
};

TEST_F(ImageLinkEditor, MenuInsertionReplacesSelectionAndUndoRedoOnlyChangesText)
{
	Insert(L"selected text");
	Command(F_SELECTALL);
	MockComdlg32::gm_Files.push_back(path.native());
	Command(F_INS_IMAGE_LINK);
	ASSERT_EQ(*ImageLink::MakeLink(path.native()), Text());
	Command(F_UNDO);
	EXPECT_EQ(L"selected text", Text());
	Command(F_REDO);
	EXPECT_EQ(*ImageLink::MakeLink(path.native()), Text());
	std::ifstream file(path);
	const std::string unchanged((std::istreambuf_iterator<char>(file)), {});
	EXPECT_EQ("not decoded during insertion", unchanged);
}

TEST_F(ImageLinkEditor, CancelViewModeAndBoxSelectionDoNotInsert)
{
	Insert(L"keep");
	Command(F_INS_IMAGE_LINK); // empty mock file list means cancel
	EXPECT_EQ(L"keep", Text());
	MockComdlg32::gm_Files.push_back(path.native());
	CAppMode::getInstance()->SetViewMode(true);
	Command(F_INS_IMAGE_LINK);
	EXPECT_EQ(L"keep", Text());
	CAppMode::getInstance()->SetViewMode(false);
	pcEditWnd->GetActiveView().GetSelectionInfo().SetBoxSelect(true);
	Command(F_INS_IMAGE_LINK);
	EXPECT_EQ(L"keep", Text());
}

TEST_F(ImageLinkEditor, MultiImageDropUsesDropPositionAndOneUndo)
{
	Insert(L"abcd");
	Command(F_SELECTALL);
	const std::array files{path, path};
	auto drop = cxx::MakeDropFiles(files);
	auto& view = pcEditWnd->GetActiveView();
	ASSERT_TRUE(view.TryInsertImageFiles(static_cast<HDROP>(drop.Get()), DropPoint(2)));
	const auto link = *ImageLink::MakeLink(path.native());
	EXPECT_EQ(L"ab" + link + pcEditDoc->m_cDocEditor.GetNewLineCode().GetValue2() + link + L"cd", Text());
	Command(F_UNDO);
	EXPECT_EQ(L"abcd", Text());
}

TEST_F(ImageLinkEditor, MixedDirectoryMissingAndReadOnlyDrops)
{
	Insert(L"keep");
	auto& view = pcEditWnd->GetActiveView();
	const std::array mixed{path, std::filesystem::path(L"C:/notes.txt")};
	auto drop = cxx::MakeDropFiles(mixed);
	EXPECT_FALSE(view.TryInsertImageFiles(static_cast<HDROP>(drop.Get()), DropPoint()));
	const std::array directory{path, path.parent_path()};
	drop = cxx::MakeDropFiles(directory);
	EXPECT_FALSE(view.TryInsertImageFiles(static_cast<HDROP>(drop.Get()), DropPoint()));
	const std::array missing{path, path.parent_path() / L"sakura-nonexistent-image.png"};
	drop = cxx::MakeDropFiles(missing);
	EXPECT_TRUE(view.TryInsertImageFiles(static_cast<HDROP>(drop.Get()), DropPoint()));
	EXPECT_EQ(L"keep", Text());
	const std::array images{path};
	drop = cxx::MakeDropFiles(images);
	CAppMode::getInstance()->SetViewMode(true);
	EXPECT_TRUE(view.TryInsertImageFiles(static_cast<HDROP>(drop.Get()), DropPoint()));
	EXPECT_EQ(L"keep", Text());
}
