/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "pch.h"
#include "util/ImageLink.h"
#include "view/NoteProjection.h"
#include "window/CImagePreviewWnd.h"
#include "cxx/com_pointer.hpp"
#include "window/EditorTestSuite.hpp"
#include "_main/CAppMode.h"
#include "util/os.h"
#include "sakura_rc.h"
#include "config/system_constants.h"
#include "func/CKeyBind.h"
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

TEST(ImageLink, StandaloneLogicalLines)
{
	for (const auto text : {L"![a](C:/a.png)", L" \t![a](<C:/My Pictures/a.jpg>)\t ", LR"(![a](C:\a.PNG))"}) {
		ASSERT_TRUE(ImageLink::FindStandalone(text));
	}
	for (const auto text : {L"", L" \t", L"text ![a](C:/a.png)", L"![a](C:/a.png) text",
		L"![a](C:/a.png) ![b](C:/b.jpg)", L"[a](C:/a.png)", L"![a](a.png)", L"![a](C:/a.png",
		L"![a](C:/a.png)\nnext"}) {
		EXPECT_FALSE(ImageLink::FindStandalone(text));
	}
	constexpr wchar_t embeddedNull[] = L"![a](C:/a.png)\0suffix";
	EXPECT_FALSE(ImageLink::FindStandalone(std::wstring_view(embeddedNull, std::size(embeddedNull) - 1)));
}

TEST(NoteProjection, TextAndVirtualRowsRoundTrip)
{
	NoteView::Projection projection;
	for (std::int64_t line = 0; line < 30; ++line) EXPECT_EQ(line, projection.FromLayout(line).value);
	projection.Reset({{0, 2, L"C:/a.png"}, {1, 3, L"C:/a.png"}, {9, 12, L"C:/b.jpg"}});
	for (std::int64_t line = 0; line < 30; ++line) {
		const auto hit = projection.ToLayout(projection.FromLayout(line));
		EXPECT_EQ(line, hit.layoutLine);
		EXPECT_EQ(nullptr, hit.block);
	}
	for (size_t index = 0; index < projection.Blocks().size(); ++index) {
		for (int offset = 0; offset < 12; ++offset) {
			const auto hit = projection.ToLayout(NoteView::DisplayRow(projection.BlockStart(index).value + offset));
			ASSERT_NE(nullptr, hit.block);
			EXPECT_EQ(&projection.Blocks()[index], hit.block);
			EXPECT_EQ(offset, hit.offset);
		}
	}
	EXPECT_EQ(30, projection.FromLayout(6).value); // two blocks before layout row 6
	projection.Clear();
	EXPECT_EQ(13, projection.ToLayout(NoteView::DisplayRow(13)).layoutLine);
}

TEST(NoteProjection, IncrementalEditsShiftAndRemoveAnchors)
{
	NoteView::Projection projection;
	projection.Reset({{1, 2, L"a"}, {4, 6, L"b"}});
	projection.Replace(0, 0, 2, 3, {});
	EXPECT_EQ(3, projection.Blocks()[0].logicalLine);
	EXPECT_EQ(5, projection.Blocks()[0].lastLayoutLine);
	projection.Replace(3, 3, 0, 0, {});
	ASSERT_EQ(1u, projection.Blocks().size());
	EXPECT_EQ(L"b", projection.Blocks()[0].path);
	projection.Replace(3, 3, 0, 0, {{3, 5, L"new"}});
	EXPECT_EQ(2u, projection.Blocks().size());
	EXPECT_EQ(L"new", projection.ToLayout(NoteView::DisplayRow(6)).block->path);
}

TEST(NoteProjection, SaturatingArithmetic)
{
	constexpr auto maximum = (std::numeric_limits<std::int64_t>::max)();
	constexpr auto minimum = (std::numeric_limits<std::int64_t>::min)();
	EXPECT_EQ(maximum, NoteView::Projection::Add(maximum - 1, 12));
	EXPECT_EQ(minimum, NoteView::Projection::Add(minimum + 1, -12));
	NoteView::Projection projection;
	projection.Reset({{0, 0, L"a"}});
	EXPECT_EQ(maximum, projection.FromLayout(maximum).value);
	EXPECT_EQ(0, projection.FromLayout(0).value);
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

static NoteView::ImageCache::Result WaitForImage(NoteView::ImageCache& cache, const NoteView::ImageKey& key)
{
	const auto started = ::GetTickCount64();
	auto result = cache.Get(key);
	while (result.state == NoteView::ImageCache::State::Loading && ::GetTickCount64() - started < 5000) {
		::Sleep(5);
		result = cache.Get(key);
	}
	return result;
}

TEST_F(ImagePreviewFile, NoteCacheLoadsAsynchronouslyAndResetRejectsOldWork)
{
	WriteImage(GUID_ContainerFormatPng, 800, 200);
	NoteView::ImageCache cache;
	const NoteView::ImageKey key{path.native(), 640, 640};
	EXPECT_EQ(NoteView::ImageCache::State::Loading, cache.Get(key).state);
	cache.Reset();
	::Sleep(100);
	EXPECT_EQ(0u, cache.EntryCount());
	EXPECT_EQ(0u, cache.ByteCount());
	const auto result = WaitForImage(cache, key);
	ASSERT_EQ(NoteView::ImageCache::State::Ready, result.state);
	EXPECT_EQ(640u, result.image->width);
	EXPECT_EQ(160u, result.image->height);
	EXPECT_EQ(640u * 160 * 4, cache.ByteCount());
}

TEST_F(ImagePreviewFile, NoteCacheFailuresStayFailedUntilReset)
{
	std::filesystem::remove(path);
	NoteView::ImageCache cache;
	const NoteView::ImageKey key{path.native(), 640, 640};
	ASSERT_EQ(NoteView::ImageCache::State::Failed, WaitForImage(cache, key).state);
	WriteImage(GUID_ContainerFormatPng, 10, 20);
	EXPECT_EQ(NoteView::ImageCache::State::Failed, cache.Get(key).state);
	cache.Reset();
	const auto result = WaitForImage(cache, key);
	ASSERT_EQ(NoteView::ImageCache::State::Ready, result.state);
	EXPECT_EQ(10u, result.image->width);
	EXPECT_EQ(20u, result.image->height);
}

TEST_F(ImagePreviewFile, NoteCacheBoundsEntriesAndDecodedBytes)
{
	WriteImage(GUID_ContainerFormatPng, 1000, 1000);
	NoteView::ImageCache cache;
	for (UINT width = 900; width < 924; ++width) {
		const auto result = WaitForImage(cache, {path.native(), width, 1000});
		ASSERT_EQ(NoteView::ImageCache::State::Ready, result.state);
		EXPECT_LE(cache.EntryCount(), NoteView::ImageCache::MaxEntries);
		EXPECT_LE(cache.ByteCount(), NoteView::ImageCache::MaxBytes);
	}
	EXPECT_EQ(NoteView::ImageCache::State::Loading, cache.Get({path.native(), 900, 1000}).state);
}

TEST_F(ImagePreviewFile, NoteCacheLargeValidImageFitsMemoryBudget)
{
	WriteImage(GUID_ContainerFormatPng, 4096, 4096);
	NoteView::ImageCache cache;
	const auto result = WaitForImage(cache, {path.native(), 4096, 4096});
	ASSERT_EQ(NoteView::ImageCache::State::Ready, result.state);
	EXPECT_EQ(result.image->width, result.image->height);
	EXPECT_LE(result.image->pixels.size(), NoteView::ImageCache::MaxBytes);
	EXPECT_GT(result.image->width, 0u);
}

class ImageLinkEditor : public testing::Test, public window::EditorTestSuite {
protected:
	std::filesystem::path path;
	std::optional<CKetaXInt> savedWrapWidth;
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
		auto& view = pcEditWnd->GetActiveView();
		if (view.m_ime.active) view.EndImeComposition();
		if (savedWrapWidth) pcEditDoc->m_cLayoutMgr.ChangeLayoutParam(
			pcEditDoc->m_cLayoutMgr.GetTabSpaceKetas(), 0, *savedWrapWidth);
		pcEditWnd->SetNoteView(false);
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
	void SetWrapWidth(int width)
	{
		if (!savedWrapWidth) savedWrapWidth = pcEditDoc->m_cLayoutMgr.GetMaxLineKetas();
		pcEditDoc->m_cLayoutMgr.ChangeLayoutParam(pcEditDoc->m_cLayoutMgr.GetTabSpaceKetas(), 0, width);
	}
	void MoveTo(CLogicPoint point)
	{
		CLayoutPoint layout;
		pcEditDoc->m_cLayoutMgr.LogicToLayout(point, &layout);
		pcEditWnd->GetActiveView().GetCaret().MoveCursor(layout, false);
	}
	void Compose(std::wstring text)
	{
		auto& view = pcEditWnd->GetActiveView();
		if (!view.m_ime.active) view.BeginImeComposition();
		view.m_ime.text = std::move(text);
		view.m_ime.cursor = static_cast<LONG>(view.m_ime.text.size());
		view.RebuildImeDisplayLayout();
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

TEST_F(ImageLinkEditor, NoteTogglePreservesTextCaretAndUndo)
{
	const std::wstring text = L"before\r\n![a](C:/a.png)\r\nafter";
	Insert(text);
	auto& view = pcEditWnd->GetActiveView();
	const auto caret = view.GetCaret().GetCaretLogicPos();
	pcEditWnd->SetNoteView(true);
	ASSERT_EQ(1u, view.GetTextArea().GetNoteProjection().Blocks().size());
	EXPECT_EQ(caret, view.GetCaret().GetCaretLogicPos());
	EXPECT_EQ(text, Text());
	EXPECT_EQ(static_cast<Int>(view.GetTextArea().GetNoteProjection().Blocks()[0].lastLayoutLine) + 13,
		view.GetTextArea().GetNoteProjection().FromLayout(view.GetTextArea().GetNoteProjection().Blocks()[0].lastLayoutLine + 1).value);
	pcEditWnd->SetNoteView(false);
	EXPECT_FALSE(view.HasNoteBlocks());
	EXPECT_EQ(caret, view.GetCaret().GetCaretLogicPos());
	Command(F_UNDO);
	EXPECT_EQ(L"", Text());
	Command(F_REDO);
	EXPECT_EQ(text, Text());
}

TEST_F(ImageLinkEditor, NoteEditingAndUndoRebuildWithoutPainting)
{
	Insert(L"![a](C:/a.png)\r\nafter");
	pcEditWnd->SetNoteView(true);
	auto& view = pcEditWnd->GetActiveView();
	Command(F_GOFILETOP);
	Insert(L"prefix");
	EXPECT_FALSE(view.HasNoteBlocks());
	Command(F_UNDO);
	ASSERT_TRUE(view.HasNoteBlocks());
	Command(F_GOFILETOP);
	Insert(L"before\r\n");
	ASSERT_EQ(1u, view.GetTextArea().GetNoteProjection().Blocks().size());
	EXPECT_EQ(1, view.GetTextArea().GetNoteProjection().Blocks()[0].logicalLine);
	Command(F_UNDO);
	EXPECT_EQ(0, view.GetTextArea().GetNoteProjection().Blocks()[0].logicalLine);
}

TEST_F(ImageLinkEditor, NoteBlockScrollHitTestingAndCaretSkip)
{
	Insert(L"![a](C:/a.png)\r\nafter\r\nend");
	auto& view = pcEditWnd->GetActiveView();
	auto& area = view.GetTextArea();
	pcEditWnd->SetNoteView(true);
	area.TextArea_OnSize(CMySize(800, area.GetAreaTop() + view.GetTextMetrics().GetHankakuDy() * 3 + 1), 0, 0);
	const auto start = area.GetNoteProjection().BlockStart(0).value;
	for (int offset = 0; offset < 12; ++offset) {
		view.ScrollDisplayAtV(NoteView::DisplayRow(start + offset));
		EXPECT_EQ(start + offset, area.GetDisplayTop().value);
		const POINT point{area.GetAreaLeft() + 1, area.GetAreaTop() + 1};
		ASSERT_TRUE(view.IsNoteBlockPoint(point));
		EXPECT_EQ(offset, area.HitClientRow(point.y).offset);
		EXPECT_EQ(CLayoutPoint(0, 0), view.NoteBlockTextPosition(point, false));
		CLayoutPoint after;
		pcEditDoc->m_cLayoutMgr.LogicToLayout(CLogicPoint(0, 1), &after);
		EXPECT_EQ(12 - offset, area.RelativeRow(after.y));
		if (12 - offset <= static_cast<int>(area.m_nViewRowNum))
			EXPECT_EQ(area.GetAreaTop() + (12 - offset) * view.GetTextMetrics().GetHankakuDy(), area.LayoutToClientY(after.y));
	}
	Command(F_GOFILETOP);
	Command(F_DOWN);
	EXPECT_EQ(CLogicPoint(0, 1), view.GetCaret().GetCaretLogicPos());
	// The headless fixture disables painting, so exercise the scrolling branch explicitly.
	view.GetCaret().MoveCursor(view.GetCaret().GetCaretLayoutPos(), true);
	EXPECT_GE(area.RelativeRow(view.GetCaret().GetCaretLayoutPos().y), 0);
	EXPECT_LT(area.RelativeRow(view.GetCaret().GetCaretLayoutPos().y), static_cast<int>(area.m_nViewRowNum));
}

TEST_F(ImageLinkEditor, NoteToggleKeepsSelectionAndNoLinkDocumentGeometry)
{
	Insert(L"first\r\ninline ![a](C:/a.png)\r\nlast");
	Command(F_SELECTALL);
	auto& view = pcEditWnd->GetActiveView();
	const auto range = view.GetSelectionInfo().m_sSelect;
	const auto caret = view.GetCaret().GetCaretLogicPos();
	const auto top = view.GetTextArea().GetDisplayTop();
	Command(F_NOTE_VIEW);
	EXPECT_TRUE(pcEditWnd->IsNoteView());
	EXPECT_FALSE(view.HasNoteBlocks());
	EXPECT_EQ(top, view.GetTextArea().GetDisplayTop());
	EXPECT_EQ(range.GetFrom(), view.GetSelectionInfo().m_sSelect.GetFrom());
	EXPECT_EQ(range.GetTo(), view.GetSelectionInfo().m_sSelect.GetTo());
	EXPECT_EQ(caret, view.GetCaret().GetCaretLogicPos());
	CAppMode::getInstance()->SetViewMode(true);
	Command(F_NOTE_VIEW);
	EXPECT_FALSE(pcEditWnd->IsNoteView());
	EXPECT_TRUE(CAppMode::getInstance()->IsViewMode());
}

TEST_F(ImageLinkEditor, NoteBlocksRejectFileDropsWithoutChangingSelection)
{
	Insert(L"![a](C:/a.png)\r\nnext");
	pcEditWnd->SetNoteView(true);
	Command(F_SELECTALL);
	auto& view = pcEditWnd->GetActiveView();
	auto& area = view.GetTextArea();
	area.SetDisplayTop(NoteView::DisplayRow(0));
	const POINT point{area.GetAreaLeft() + 1, area.GetAreaTop() + view.GetTextMetrics().GetHankakuDy() * 2};
	ASSERT_TRUE(view.IsNoteBlockPoint(point));
	const auto before = Text();
	const auto range = view.GetSelectionInfo().m_sSelect;
	const std::array files{path};
	auto drop = cxx::MakeDropFiles(files);
	EXPECT_TRUE(view.TryInsertImageFiles(static_cast<HDROP>(drop.Get()), point));
	EXPECT_EQ(before, Text());
	EXPECT_EQ(range.GetFrom(), view.GetSelectionInfo().m_sSelect.GetFrom());
	EXPECT_EQ(range.GetTo(), view.GetSelectionInfo().m_sSelect.GetTo());
}

TEST_F(ImageLinkEditor, NoteMenuInsertionAddsOnlyNecessaryLineBreaks)
{
	pcEditWnd->SetNoteView(true);
	const auto link = *ImageLink::MakeLink(path.native());
	const auto eol = std::wstring(pcEditDoc->m_cDocEditor.GetNewLineCode().GetValue2());
	struct Example { std::wstring before; CLogicPoint caret; std::wstring after; };
	const Example examples[] = {
		{L"abcd", {2, 0}, L"ab" + eol + link + eol + L"cd"},
		{L"abcd", {0, 0}, link + eol + L"abcd"},
		{L"abcd", {4, 0}, L"abcd" + eol + link},
		{L"", {0, 0}, link},
		{L"a\r\n", {0, 1}, L"a\r\n" + link},
		{L" \t ", {1, 0}, L" " + link + L"\t "},
	};
	for (const auto& example : examples) {
		Command(F_SELECTALL);
		Command(F_DELETE);
		Insert(example.before);
		CLayoutPoint caret;
		pcEditDoc->m_cLayoutMgr.LogicToLayout(example.caret, &caret);
		pcEditWnd->GetActiveView().GetCaret().MoveCursor(caret, false);
		MockComdlg32::gm_Files.push_back(path.native());
		Command(F_INS_IMAGE_LINK);
		EXPECT_EQ(example.after, Text());
		EXPECT_EQ(1u, pcEditWnd->GetActiveView().GetTextArea().GetNoteProjection().Blocks().size());
		Command(F_UNDO);
		EXPECT_EQ(example.before, Text());
	}
}

TEST_F(ImageLinkEditor, NoteMultiImageDropIsOneUndo)
{
	Insert(L"abcd");
	pcEditWnd->SetNoteView(true);
	const std::array files{path, path};
	auto drop = cxx::MakeDropFiles(files);
	auto& view = pcEditWnd->GetActiveView();
	ASSERT_TRUE(view.TryInsertImageFiles(static_cast<HDROP>(drop.Get()), DropPoint(2)));
	const auto link = *ImageLink::MakeLink(path.native());
	const auto eol = std::wstring(pcEditDoc->m_cDocEditor.GetNewLineCode().GetValue2());
	EXPECT_EQ(L"ab" + eol + link + eol + link + eol + L"cd", Text());
	EXPECT_EQ(2u, view.GetTextArea().GetNoteProjection().Blocks().size());
	Command(F_UNDO);
	EXPECT_EQ(L"abcd", Text());
	EXPECT_FALSE(view.HasNoteBlocks());
}

TEST_F(ImageLinkEditor, NoteWrappedFinalLineAndReflowKeepVirtualOffset)
{
	Insert(L"before\r\n![image](C:/directory/long-file-name-for-wrapping.png)");
	auto& manager = pcEditDoc->m_cLayoutMgr;
	const auto previousWidth = manager.GetMaxLineKetas();
	pcEditWnd->SetNoteView(true);
	manager.ChangeLayoutParam(8, 0, 20);
	auto& view = pcEditWnd->GetActiveView();
	auto& area = view.GetTextArea();
	ASSERT_EQ(1u, area.GetNoteProjection().Blocks().size());
	EXPECT_GT(static_cast<int>(manager.GetLineCount()), 2);
	EXPECT_EQ(static_cast<int>(manager.GetLineCount()) - 1, area.GetNoteProjection().Blocks()[0].lastLayoutLine);
	EXPECT_EQ(static_cast<int>(manager.GetLineCount()) + 12,
		area.GetNoteProjection().FromLayout(static_cast<int>(manager.GetLineCount())).value);
	area.TextArea_OnSize(CMySize(800, area.GetAreaTop() + view.GetTextMetrics().GetHankakuDy() * 3 + 1), 0, 0);
	view.ScrollDisplayAtV(NoteView::DisplayRow(area.GetNoteProjection().BlockStart(0).value + 4));
	const auto saved = pcEditWnd->SavePhysPosOfAllView();
	manager.ChangeLayoutParam(8, 0, 25);
	pcEditWnd->RestorePhysPosOfAllView(saved);
	const auto hit = area.HitClientRow(area.GetAreaTop());
	ASSERT_NE(nullptr, hit.block);
	EXPECT_EQ(4, hit.offset);
	manager.ChangeLayoutParam(8, 0, previousWidth);
}

TEST_F(ImageLinkEditor, NoteOffscreenCaretKeepsSurvivingBlockOrNextTextAnchor)
{
	Insert(L"before\r\n![a](C:/a.png)\r\nafter\r\ntail\r\ntail\r\ntail\r\nend");
	pcEditWnd->SetNoteView(true);
	auto& view = pcEditWnd->GetActiveView();
	auto& area = view.GetTextArea();
	area.TextArea_OnSize(CMySize(800, area.GetAreaTop() + view.GetTextMetrics().GetHankakuDy() * 3 + 1), 0, 0);
	view.ScrollDisplayAtV(NoteView::DisplayRow(area.GetNoteProjection().BlockStart(0).value + 4));
	auto insertWithoutMovingCaret = [&](CLogicInt line, const wchar_t* text) {
		COpeLineData data;
		auto& inserted = data.emplace_back();
		inserted.cmemLine.SetString(text);
		inserted.nSeq = 0;
		CLayoutPoint point;
		pcEditDoc->m_cLayoutMgr.LogicToLayout(CLogicPoint(0, line), &point);
		LayoutReplaceArg arg{};
		arg.sDelRange.Set(point);
		arg.pInsData = &data;
		pcEditDoc->m_cLayoutMgr.ReplaceData_CLayoutMgr(&arg);
	};
	insertWithoutMovingCaret(CLogicInt(1), L"\r\n");
	ASSERT_EQ(1u, area.GetNoteProjection().Blocks().size());
	EXPECT_EQ(2, area.GetNoteProjection().Blocks()[0].logicalLine);
	EXPECT_EQ(4, area.HitClientRow(area.GetAreaTop()).offset);
	insertWithoutMovingCaret(CLogicInt(2), L"x");
	EXPECT_FALSE(view.HasNoteBlocks());
	EXPECT_EQ(CLayoutInt(3), area.GetViewTopLine()); // The text below the vanished block.
}

TEST(ImageLink, NoteResourceIdsDoNotOverlapTypeShortcuts)
{
	const int ids[] = {STR_NOTE_LOADING, STR_NOTE_ERROR, STR_TYPEKEYBIND_INHERIT,
		STR_TYPEKEYBIND_OVERRIDE, STR_PROPTYPE_KEYBIND};
	for (size_t i = 0; i < std::size(ids); ++i)
		for (size_t j = i + 1; j < std::size(ids); ++j) EXPECT_NE(ids[i], ids[j]);
}

TEST(ImageLink, LocalizedNoteAndShortcutStringsRemainDistinct)
{
	wchar_t executable[32768]{};
	ASSERT_GT(::GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable))), 0u);
	const auto directory = std::filesystem::path(executable).parent_path();
	struct Language { const wchar_t* file; const wchar_t* loading; const wchar_t* error; const wchar_t* inherit; };
	const Language languages[] = {
		{L"sakura_lang_en_US.dll", L"Loading", L"Cannot display image", L"Using common binding"},
		{L"sakura_lang_zh_CN.dll", L"正在加载", L"无法显示图像", L"使用通用设置"},
	};
	for (const auto& language : languages) {
		const auto module = ::LoadLibraryExW((directory / language.file).c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
		ASSERT_NE(nullptr, module);
		for (const auto& [id, expected] : {std::pair{STR_NOTE_LOADING, language.loading},
			std::pair{STR_NOTE_ERROR, language.error}, std::pair{STR_TYPEKEYBIND_INHERIT, language.inherit}}) {
			wchar_t text[256]{};
			EXPECT_GT(::LoadStringW(module, id, text, static_cast<int>(std::size(text))), 0);
			EXPECT_STREQ(expected, text);
		}
		::FreeLibrary(module);
	}
}

TEST_F(ImageLinkEditor, ImeWithoutNoteViewKeepsCommittedTextAndCancellation)
{
	Insert(L"before after\r\ntail");
	const auto original = Text();
	MoveTo({7, 0});
	Compose(L"日本語");
	auto& view = pcEditWnd->GetActiveView();
	ASSERT_TRUE(view.IsImeInlineDisplay());
	EXPECT_FALSE(view.HasNoteBlocks());
	EXPECT_EQ(original, Text());
	EXPECT_EQ(L"before 日本語after\r\n", std::wstring(view.m_imeLine->GetPtr(), view.m_imeLine->GetLengthWithEOL()));
	EXPECT_EQ(CLogicPoint(7, 0), view.GetCaret().GetCaretLogicPos());
	view.EndImeComposition();
	EXPECT_FALSE(view.IsImeInlineDisplay());
	EXPECT_EQ(original, Text());
}

TEST_F(ImageLinkEditor, ImeBeforeImageShiftsBlockByWrappedPreeditRows)
{
	Insert(L"before\r\n![a](C:/a.png)\r\nafter\r\ntail");
	SetWrapWidth(20);
	pcEditWnd->SetNoteView(true);
	auto& view = pcEditWnd->GetActiveView();
	auto& area = view.GetTextArea();
	const auto original = Text();
	const auto lines = pcEditDoc->m_cLayoutMgr.GetLineCount();
	const auto originalBlock = area.GetNoteProjection().Blocks()[0];
	MoveTo({3, 0});
	Compose(std::wstring(70, L'語'));
	const int delta = static_cast<int>(view.m_imeLayout->GetLineCount()) - static_cast<int>(view.m_ime.originalLayoutLines);
	ASSERT_GT(delta, 0);
	ASSERT_EQ(1u, area.GetNoteProjection().Blocks().size());
	const auto& block = area.GetNoteProjection().Blocks()[0];
	EXPECT_EQ(originalBlock.lastLayoutLine + delta, block.lastLayoutLine);
	EXPECT_EQ(originalBlock.logicalLine, block.logicalLine);
	EXPECT_EQ(lines, pcEditDoc->m_cLayoutMgr.GetLineCount());
	EXPECT_EQ(lines + delta, view.GetDisplayLineCount());
	EXPECT_EQ(original, Text());
	const auto displayAfter = view.DocumentToDisplayLayoutLine(CLayoutInt(originalBlock.lastLayoutLine + 1));
	ASSERT_NE(nullptr, view.SearchDisplayLine(displayAfter));
	EXPECT_EQ(CLogicInt(2), view.SearchDisplayLine(displayAfter)->GetLogicLineNo());
	EXPECT_EQ(CLayoutInt(originalBlock.lastLayoutLine + 1), view.DisplayToDocumentLayoutLine(displayAfter));
	area.TextArea_OnSize(CMySize(800, area.GetAreaTop() + view.GetTextMetrics().GetHankakuDy() * 3 + 1), 0, 0);
	view.EnsureDisplayCaretVisible();
	const auto relative = area.RelativeRow(view.GetDisplayCaretLayoutPos().y);
	EXPECT_GE(relative, 0);
	EXPECT_LT(relative, static_cast<int>(area.m_nViewRowNum));
	view.EndImeComposition();
	EXPECT_EQ(originalBlock, area.GetNoteProjection().Blocks()[0]);
	EXPECT_EQ(original, Text());
}

TEST_F(ImageLinkEditor, ImeAfterImageUsesNoteCoordinatesForCaret)
{
	Insert(L"![a](C:/a.png)\r\nafter\r\ntail");
	SetWrapWidth(20);
	pcEditWnd->SetNoteView(true);
	auto& view = pcEditWnd->GetActiveView();
	const auto block = view.GetTextArea().GetNoteProjection().Blocks()[0];
	MoveTo({2, 1});
	Compose(std::wstring(40, L'字'));
	EXPECT_EQ(block, view.GetTextArea().GetNoteProjection().Blocks()[0]);
	view.GetTextArea().SetDisplayTop(NoteView::DisplayRow(0));
	const auto caret = view.GetDisplayCaretLayoutPos();
	EXPECT_EQ(static_cast<int>(caret.y) + 12, view.GetTextArea().RelativeRow(caret.y));
	const POINT point = view.GetCaret().CalcCaretDrawPos(caret);
	EXPECT_EQ(view.GetTextArea().LayoutToClientY(caret.y) + view.GetTextMetrics().GetHankakuHeight()
		- view.GetCaret().GetCaretSize().cy, point.y);
}

TEST_F(ImageLinkEditor, ImeEditingLinkHidesOldBlockAndCancellationRestoresIt)
{
	const std::wstring original = L"![a](C:/a.png)\r\nafter";
	Insert(original);
	pcEditWnd->SetNoteView(true);
	auto& view = pcEditWnd->GetActiveView();
	MoveTo({0, 0});
	Compose(L"文中");
	EXPECT_FALSE(view.HasNoteBlocks());
	EXPECT_EQ(original, Text());
	Compose(L"");
	ASSERT_TRUE(view.HasNoteBlocks());
	EXPECT_EQ(L"C:/a.png", view.GetTextArea().GetNoteProjection().Blocks()[0].path);
	Compose(L"prefix");
	view.EndImeComposition();
	EXPECT_TRUE(view.HasNoteBlocks());
	EXPECT_EQ(original, Text());
}

TEST_F(ImageLinkEditor, ImeSelectionAcrossImagesOnlyReplacesDisplayUntilCommit)
{
	const std::wstring original = L"head\r\n![a](C:/a.png)\r\n![b](C:/b.jpg)\r\ntail\r\n![c](C:/c.png)";
	Insert(original);
	pcEditWnd->SetNoteView(true);
	auto& view = pcEditWnd->GetActiveView();
	MoveTo({0, 1});
	view.GetSelectionInfo().BeginSelectArea();
	CLayoutPoint end;
	pcEditDoc->m_cLayoutMgr.LogicToLayout(CLogicPoint(0, 3), &end);
	view.GetSelectionInfo().ChangeSelectAreaByCurrentCursor(end);
	view.GetCaret().MoveCursor(end, false);
	Compose(L"置換");
	ASSERT_EQ(1u, view.GetTextArea().GetNoteProjection().Blocks().size());
	EXPECT_EQ(4, view.GetTextArea().GetNoteProjection().Blocks()[0].logicalLine);
	EXPECT_EQ(original, Text());
	view.EndImeComposition();
	EXPECT_EQ(3u, view.GetTextArea().GetNoteProjection().Blocks().size());
	EXPECT_EQ(original, Text());
}

TEST_F(ImageLinkEditor, ImePartialCommitRetainsTextAndSupportsUndoRedo)
{
	const std::wstring original = L"before\r\n![a](C:/a.png)\r\nafter";
	Insert(original);
	pcEditWnd->SetNoteView(true);
	auto& view = pcEditWnd->GetActiveView();
	MoveTo({0, 0});
	Compose(L"日本");
	view.CommitImeResult(L"日");
	EXPECT_EQ(L"日" + original, Text());
	Compose(L"本語");
	EXPECT_EQ(L"日" + original, Text());
	view.CommitImeResult(L"本語");
	view.EndImeComposition();
	EXPECT_EQ(L"日本語" + original, Text());
	EXPECT_EQ(1u, view.GetTextArea().GetNoteProjection().Blocks().size());
	Command(F_UNDO);
	EXPECT_EQ(L"日" + original, Text());
	Command(F_UNDO);
	EXPECT_EQ(original, Text());
	Command(F_REDO);
	EXPECT_EQ(L"日" + original, Text());
}

TEST_F(ImageLinkEditor, ImeCanDisplayImageLinkInEmptyDocumentWithoutSavingIt)
{
	pcEditWnd->SetNoteView(true);
	Compose(L"![a](C:/a.png)");
	auto& view = pcEditWnd->GetActiveView();
	EXPECT_EQ(L"", Text());
	EXPECT_EQ(CLayoutInt(0), pcEditDoc->m_cLayoutMgr.GetLineCount());
	ASSERT_EQ(1u, view.GetTextArea().GetNoteProjection().Blocks().size());
	EXPECT_EQ(0, view.GetTextArea().GetNoteProjection().Blocks()[0].logicalLine);
	view.EndImeComposition();
	EXPECT_FALSE(view.HasNoteBlocks());
	EXPECT_EQ(L"", Text());
}

TEST_F(ImageLinkEditor, NoteToggleDuringImeDoesNotDiscardComposition)
{
	Insert(L"![a](C:/a.png)\r\nafter\r\ntail");
	MoveTo({2, 1});
	Compose(L"日本語");
	auto& view = pcEditWnd->GetActiveView();
	const auto original = Text();
	const auto caret = view.GetCaret().GetCaretLogicPos();
	Command(F_NOTE_VIEW);
	EXPECT_TRUE(view.IsImeInlineDisplay());
	EXPECT_TRUE(view.HasNoteBlocks());
	EXPECT_EQ(L"日本語", view.m_ime.text);
	Command(F_NOTE_VIEW);
	EXPECT_TRUE(view.IsImeInlineDisplay());
	EXPECT_FALSE(view.HasNoteBlocks());
	EXPECT_EQ(L"日本語", view.m_ime.text);
	EXPECT_EQ(caret, view.GetCaret().GetCaretLogicPos());
	EXPECT_EQ(original, Text());
}

TEST_F(ImageLinkEditor, ImeAndNoteReflowPreservePartialImageViewport)
{
	Insert(L"before\r\n![a](C:/a.png)\r\nafter\r\ntail\r\ntail");
	SetWrapWidth(20);
	pcEditWnd->SetNoteView(true);
	MoveTo({1, 0});
	Compose(std::wstring(50, L'語'));
	auto& view = pcEditWnd->GetActiveView();
	auto& area = view.GetTextArea();
	area.TextArea_OnSize(CMySize(800, area.GetAreaTop() + view.GetTextMetrics().GetHankakuDy() * 3 + 1), 0, 0);
	view.ScrollDisplayAtV(NoteView::DisplayRow(area.GetNoteProjection().BlockStart(0).value + 4));
	const auto saved = pcEditWnd->SavePhysPosOfAllView();
	view.InvalidateImeDisplayLayout();
	SetWrapWidth(30);
	pcEditWnd->RestorePhysPosOfAllView(saved);
	view.RefreshImeDisplayAfterRelayout();
	ASSERT_TRUE(view.IsImeInlineDisplay());
	const auto hit = area.HitClientRow(area.GetAreaTop());
	ASSERT_NE(nullptr, hit.block);
	EXPECT_EQ(4, hit.offset);
	EXPECT_EQ(1, hit.block->logicalLine);
}

TEST_F(ImageLinkEditor, ImeUncompletedMouseActionsPreserveComposition)
{
	Insert(L"![a](C:/a.png)\r\nafter");
	pcEditWnd->SetNoteView(true);
	MoveTo({0, 1});
	Compose(L"未確定");
	auto& view = pcEditWnd->GetActiveView();
	const auto point = view.GetCaret().GetCaretLogicPos();
	const auto& area = view.GetTextArea();
	const int x = area.GetAreaLeft() + 1;
	const int y = area.GetAreaTop() + view.GetTextMetrics().GetHankakuDy() * 2;
	// A headless view has no HIMC, so completion must reject the mouse action.
	view.OnLBUTTONDOWN(0, x, y);
	view.OnRBUTTONDOWN(0, x, y);
	EXPECT_TRUE(view.m_bImeRejectRightUp);
	view.OnRBUTTONUP(0, x, y);
	EXPECT_FALSE(view.m_bImeRejectRightUp);
	view.OnLBUTTONDBLCLK(0, x, y);
	EXPECT_EQ(point, view.GetCaret().GetCaretLogicPos());
	EXPECT_EQ(L"未確定", view.m_ime.text);
	EXPECT_TRUE(view.IsImeInlineDisplay());
}

TEST_F(ImageLinkEditor, NativeImeFallbackStillAppliesToBoxSelection)
{
	Insert(L"![a](C:/a.png)\r\nafter");
	pcEditWnd->SetNoteView(true);
	auto& view = pcEditWnd->GetActiveView();
	view.GetSelectionInfo().SetBoxSelect(true);
	ASSERT_TRUE(view.ShouldUseNativeIme());
	view.m_bImeNativeComposition = view.ShouldUseNativeIme();
	const auto before = view.GetTextArea().GetNoteProjection().Blocks();
	Compose(std::wstring(100, L'語'));
	EXPECT_FALSE(view.IsImeInlineDisplay());
	EXPECT_EQ(before, view.GetTextArea().GetNoteProjection().Blocks());
	EXPECT_EQ(pcEditDoc->m_cLayoutMgr.GetLineCount(), view.GetDisplayLineCount());
}

TEST_F(ImageLinkEditor, NativeImeOverwriteStillCommitsAndUndoesWithNoteView)
{
	const std::wstring original = L"abc\r\n![a](C:/a.png)";
	Insert(original);
	pcEditWnd->SetNoteView(true);
	MoveTo({0, 0});
	auto& view = pcEditWnd->GetActiveView();
	view.SetInsMode(false);
	EXPECT_TRUE(view.ShouldUseNativeIme());
	view.m_bImeNativeComposition = view.ShouldUseNativeIme();
	Compose(L"日");
	EXPECT_FALSE(view.IsImeInlineDisplay());
	EXPECT_EQ(original, Text());
	view.CommitImeResult(L"日");
	view.EndImeComposition();
	view.SetInsMode(true);
	EXPECT_EQ(L"日bc\r\n![a](C:/a.png)", Text());
	EXPECT_TRUE(view.HasNoteBlocks());
	Command(F_UNDO);
	EXPECT_EQ(original, Text());
}

TEST_F(ImageLinkEditor, MarkdownCommandsAndTypeShortcutsCoexistWithNoteView)
{
	const std::wstring original = L"text\r\n![a](C:/a.png)\r\ntail";
	Insert(original);
	pcEditWnd->SetNoteView(true);
	MoveTo({0, 0});
	auto& view = pcEditWnd->GetActiveView();
	view.GetSelectionInfo().BeginSelectArea();
	CLayoutPoint end;
	pcEditDoc->m_cLayoutMgr.LogicToLayout(CLogicPoint(4, 0), &end);
	view.GetSelectionInfo().ChangeSelectAreaByCurrentCursor(end);
	view.GetCaret().MoveCursor(end, false);
	Command(F_MARKDOWN_BOLD);
	EXPECT_EQ(L"**text**\r\n![a](C:/a.png)\r\ntail", Text());
	EXPECT_TRUE(view.HasNoteBlocks());
	Command(F_UNDO);
	EXPECT_EQ(original, Text());
	Command(F_REDO);
	EXPECT_EQ(L"**text**\r\n![a](C:/a.png)\r\ntail", Text());
	EXPECT_TRUE(view.HasNoteBlocks());

	auto type = std::make_unique<STypeConfig>();
	CType_Markdown markdown;
	markdown.InitTypeConfig(17, *type);
	KEYDATA common{};
	common.m_nKeyCode = 'B';
	common.m_nFuncCodeArr[_CTRL] = F_COPY;
	auto& binding = type->m_typeKeyBindings['B'];
	EXPECT_EQ(F_MARKDOWN_BOLD, CKeyBind::GetFuncCodeAt(common, _CTRL, TRUE, &binding));
	binding.m_bOverride[_CTRL] = false;
	EXPECT_EQ(F_COPY, CKeyBind::GetFuncCodeAt(common, _CTRL, TRUE, &binding));
	EXPECT_EQ(F_COPY, common.m_nFuncCodeArr[_CTRL]);
}
