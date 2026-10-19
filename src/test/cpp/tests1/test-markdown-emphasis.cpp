/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "pch.h"
#include "view/CMarkdownEmphasis.h"
#include "view/CFontFallback.h"
#include "doc/logic/CDocLine.h"
#include "doc/logic/CDocLineMgr.h"
#include "types/CType.h"

namespace {
void ExpectStyles(std::wstring_view text, std::string_view expected)
{
	const auto styles = CMarkdownEmphasis::Parse(text);
	ASSERT_EQ(text.size(), styles.size());
	ASSERT_EQ(text.size(), expected.size());
	for (size_t pos = 0; pos < styles.size(); ++pos) {
		EXPECT_EQ(expected[pos] - '0', styles[pos]) << "position " << pos;
	}
}
}

TEST(MarkdownEmphasis, BoldItalicAndNesting)
{
	ExpectStyles(L"**太字**", "001100");
	ExpectStyles(L"*斜体*", "0220");
	ExpectStyles(L"***両方***", "00033000");
	ExpectStyles(L"__bold__", "00111100");
	ExpectStyles(L"_italic_", "02222220");
	ExpectStyles(L"**a *b* c**", "00110301100");
	ExpectStyles(L"*a **b** c*", "02200300220");
	ExpectStyles(L"前**太字**後", "00011000");
}

TEST(MarkdownEmphasis, DelimiterRunCanCloseThenOpen)
{
	ExpectStyles(L"**a***b*", "00100020");
	ExpectStyles(L"*a **b***", "022003000");
	ExpectStyles(L"a**b*c", "000000"); // rule of three
}

TEST(MarkdownEmphasis, UnmatchedEscapedAndIntrawordMarkersAreLiteral)
{
	for (const auto text : {L"*unfinished", L"** unfinished **", L"foo_bar_baz", L"\\*x\\*", L"* item", L"a_b_c"}) {
		const std::wstring_view value(text);
		ExpectStyles(value, std::string(value.size(), '0'));
	}
}

TEST(MarkdownEmphasis, CodeSpansAndDestinationsAreLiteral)
{
	for (const auto text : {L"`**code**`", L"`` `*code*` ``", L"[label](https://host/a_b_c)", L"<https://host/*path*>"}) {
		const std::wstring_view value(text);
		ExpectStyles(value, std::string(value.size(), '0'));
	}
	ExpectStyles(L"[**b**](url)", "000100000000");
	ExpectStyles(L"**a `b` c**", "00110001100");
	ExpectStyles(L"**a\nb**", "0011100");
}

TEST(MarkdownEmphasis, FenceLengthMarkerAndIndentation)
{
	CMarkdownEmphasis::BlockState state;
	EXPECT_TRUE(CMarkdownEmphasis::ScanBlock(L"````cpp", state).code);
	EXPECT_TRUE(CMarkdownEmphasis::ScanBlock(L"**code**", state).code);
	EXPECT_TRUE(CMarkdownEmphasis::ScanBlock(L"```", state).code);
	EXPECT_EQ(L'`', state.fence);
	EXPECT_TRUE(CMarkdownEmphasis::ScanBlock(L"~~~~", state).code);
	EXPECT_EQ(L'`', state.fence);
	EXPECT_TRUE(CMarkdownEmphasis::ScanBlock(L"```` trailing", state).code);
	EXPECT_EQ(L'`', state.fence);
	EXPECT_TRUE(CMarkdownEmphasis::ScanBlock(L"`````", state).code);
	EXPECT_EQ(0, state.fence);
	EXPECT_FALSE(CMarkdownEmphasis::ScanBlock(L"**text**", state).code);
	EXPECT_TRUE(CMarkdownEmphasis::ScanBlock(L"    **code**", state).code);
	EXPECT_TRUE(CMarkdownEmphasis::ScanBlock(L"> ~~~", state).code);
	EXPECT_TRUE(CMarkdownEmphasis::ScanBlock(L"> *code*", state).code);
	EXPECT_TRUE(CMarkdownEmphasis::ScanBlock(L"> ~~~", state).code);
	EXPECT_EQ(0, state.fence);
}

TEST(MarkdownEmphasis, OffscreenParagraphAndFenceContext)
{
	CDocLineMgr document;
	std::vector<CDocLine*> lines;
	for (const auto text : {L"**first", L"second**", L"", L"```", L"**literal**", L"```", L"*last*"}) {
		auto* line = document.AddNewLine();
		line->_GetDocLineData().SetString(text);
		lines.push_back(line);
	}
	CMarkdownEmphasis paint;
	paint.Prepare(lines[1]); // opening delimiter is above the viewport
	EXPECT_EQ(CMarkdownEmphasis::Bold, paint.GetStyle(0));
	EXPECT_EQ(CMarkdownEmphasis::Normal, paint.GetStyle(6));
	paint.Prepare(lines[4]);
	EXPECT_EQ(CMarkdownEmphasis::Normal, paint.GetStyle(2));
	paint.Prepare(lines[6]);
	EXPECT_EQ(CMarkdownEmphasis::Italic, paint.GetStyle(1));
	EXPECT_EQ(CMarkdownEmphasis::Normal, paint.GetStyle(6));

	CMarkdownEmphasis scrolledPaint;
	scrolledPaint.Prepare(lines[4]); // fence opener is above the viewport
	EXPECT_EQ(CMarkdownEmphasis::Normal, scrolledPaint.GetStyle(2));
	lines[0]->_GetDocLineData().SetString(L"first");
	CMarkdownEmphasis editedPaint;
	editedPaint.Prepare(lines[1]);
	EXPECT_EQ(CMarkdownEmphasis::Normal, editedPaint.GetStyle(0));
}

TEST(MarkdownEmphasis, AppliesToMarkdownTypesIncludingExistingCustomTypes)
{
	auto type = std::make_unique<STypeConfig>();
	wcscpy(type->m_szTypeName, L"Markdown");
	EXPECT_TRUE(CMarkdownEmphasis::IsEnabled(*type));
	wcscpy(type->m_szTypeName, L"Custom");
	wcscpy(type->m_szTypeExts, L"txt;MD,markdown");
	EXPECT_TRUE(CMarkdownEmphasis::IsEnabled(*type));
	wcscpy(type->m_szTypeExts, L"*.mdown");
	EXPECT_TRUE(CMarkdownEmphasis::IsEnabled(*type));
	wcscpy(type->m_szTypeExts, L"txt,cmd,*");
	EXPECT_FALSE(CMarkdownEmphasis::IsEnabled(*type));
}

TEST(MarkdownEmphasis, FontVariantsKeepBoldItalicAndUnderlineTogether)
{
	LOGFONT source{};
	source.lfHeight = -16;
	source.lfWeight = FW_NORMAL;
	wcscpy_s(source.lfFaceName, L"Consolas");
	const SFontFallbacks fallbacks{};
	CFontFallback fonts(source, fallbacks);
	for (int style = 0; style < 8; ++style) {
		LOGFONT actual{};
		const auto font = fonts.GetFont(0, (style & 1) != 0, (style & 2) != 0, (style & 4) != 0);
		ASSERT_NE(nullptr, font);
		ASSERT_EQ(sizeof(actual), static_cast<size_t>(GetObject(font, sizeof(actual), &actual)));
		EXPECT_EQ(style & 1 ? FW_BOLD : FW_NORMAL, actual.lfWeight);
		EXPECT_EQ((style & 2) != 0, actual.lfUnderline != 0);
		EXPECT_EQ((style & 4) != 0, actual.lfItalic != 0);
	}
}
