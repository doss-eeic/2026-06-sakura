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

TEST(MarkdownEmphasis, StrikeUsesParagraphAndFenceContext)
{
	CDocLineMgr document;
	std::vector<CDocLine*> lines;
	for (const auto text : {L"~~first", L"second~~", L"", L"~~~", L"~~literal~~", L"~~~", L"~~last~~"}) {
		auto* line = document.AddNewLine();
		line->_GetDocLineData().SetString(text);
		lines.push_back(line);
	}
	CMarkdownEmphasis paint;
	paint.Prepare(lines[1]);
	EXPECT_EQ(Markdown::Strike, paint.GetStyle(0));
	EXPECT_EQ(Markdown::Normal, paint.GetStyle(6));
	paint.Prepare(lines[4]);
	EXPECT_EQ(Markdown::Normal, paint.GetStyle(2));
	paint.Prepare(lines[6]);
	EXPECT_EQ(Markdown::Strike, paint.GetStyle(2));
	lines[0]->_GetDocLineData().SetString(L"first");
	CMarkdownEmphasis editedPaint;
	editedPaint.Prepare(lines[1]);
	EXPECT_EQ(Markdown::Normal, editedPaint.GetStyle(0));
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
	EXPECT_EQ(Markdown::Bold, paint.GetStyle(0));
	EXPECT_EQ(Markdown::Normal, paint.GetStyle(6));
	paint.Prepare(lines[4]);
	EXPECT_EQ(Markdown::Normal, paint.GetStyle(2));
	paint.Prepare(lines[6]);
	EXPECT_EQ(Markdown::Italic, paint.GetStyle(1));
	EXPECT_EQ(Markdown::Normal, paint.GetStyle(6));

	CMarkdownEmphasis scrolledPaint;
	scrolledPaint.Prepare(lines[4]); // fence opener is above the viewport
	EXPECT_EQ(Markdown::Normal, scrolledPaint.GetStyle(2));
	lines[0]->_GetDocLineData().SetString(L"first");
	CMarkdownEmphasis editedPaint;
	editedPaint.Prepare(lines[1]);
	EXPECT_EQ(Markdown::Normal, editedPaint.GetStyle(0));
}

TEST(MarkdownEmphasis, UsesDisplaySettingInsteadOfNameOrExtension)
{
	auto type = std::make_unique<STypeConfig>();
	wcscpy(type->m_szTypeName, L"Markdown");
	wcscpy(type->m_szTypeExts, L"md,markdown,mdown");
	EXPECT_FALSE(CMarkdownEmphasis::IsEnabled(*type));
	wcscpy(type->m_szTypeName, L"Custom");
	wcscpy(type->m_szTypeExts, L"txt;MD,markdown");
	EXPECT_FALSE(CMarkdownEmphasis::IsEnabled(*type));
	wcscpy(type->m_szTypeExts, L"*.mdown");
	EXPECT_FALSE(CMarkdownEmphasis::IsEnabled(*type));
	wcscpy(type->m_szTypeExts, L"txt,cmd,*");
	type->m_eSyntaxDisplay = SYNTAX_DISPLAY_MARKDOWN;
	EXPECT_TRUE(CMarkdownEmphasis::IsEnabled(*type));
	type->m_eSyntaxDisplay = SYNTAX_DISPLAY_NONE;
	EXPECT_FALSE(CMarkdownEmphasis::IsEnabled(*type));
}

TEST(MarkdownEmphasis, FontVariantsKeepBoldItalicUnderlineAndStrikeTogether)
{
	LOGFONT source{};
	source.lfHeight = -16;
	source.lfWeight = FW_NORMAL;
	wcscpy_s(source.lfFaceName, L"Consolas");
	const SFontFallbacks fallbacks{};
	CFontFallback fonts(source, fallbacks);
	for (int style = 0; style < 16; ++style) {
		LOGFONT actual{};
		const auto font = fonts.GetFont(0, (style & 1) != 0, (style & 2) != 0, (style & 4) != 0, (style & 8) != 0);
		ASSERT_NE(nullptr, font);
		ASSERT_EQ(sizeof(actual), static_cast<size_t>(GetObject(font, sizeof(actual), &actual)));
		EXPECT_EQ(style & 1 ? FW_BOLD : FW_NORMAL, actual.lfWeight);
		EXPECT_EQ((style & 2) != 0, actual.lfUnderline != 0);
		EXPECT_EQ((style & 4) != 0, actual.lfItalic != 0);
		EXPECT_EQ((style & 8) != 0, actual.lfStrikeOut != 0);
	}
}

TEST(MarkdownEmphasis, ThinAndLightFontsUseAtLeastBoldWeight)
{
	for (const LONG weight : {FW_THIN, FW_LIGHT, FW_NORMAL}) {
		LOGFONT source{};
		source.lfHeight = -20;
		source.lfWeight = weight;
		source.lfCharSet = DEFAULT_CHARSET;
		wcscpy_s(source.lfFaceName, L"Segoe UI");
		const SFontFallbacks fallbacks{};
		CFontFallback fonts(source, fallbacks);
		for (bool italic : {false, true}) {
			LOGFONT actual{};
			const auto font = fonts.GetFont(0, true, false, italic);
			ASSERT_EQ(sizeof(actual), static_cast<size_t>(GetObject(font, sizeof(actual), &actual)));
			EXPECT_EQ(FW_BOLD, actual.lfWeight);
			HDC dc = CreateCompatibleDC(nullptr);
			ASSERT_NE(nullptr, dc);
			const auto old = SelectObject(dc, font);
			TEXTMETRIC metrics{};
			const BOOL measured = GetTextMetrics(dc, &metrics);
			SelectObject(dc, old);
			DeleteDC(dc);
			ASSERT_TRUE(measured);
			EXPECT_GE(metrics.tmWeight, FW_BOLD);
		}
	}
}

TEST(MarkdownEmphasis, NamedThinInstanceDoesNotReuseRegularWeight)
{
	if (!CFontFallback::IsAvailable(L"Noto Sans JP Thin")) GTEST_SKIP() << "Noto Sans JP Thin is not installed";
	LOGFONT source{};
	source.lfHeight = -20;
	source.lfWeight = FW_NORMAL; // old font-list settings discarded the selected weight
	source.lfCharSet = DEFAULT_CHARSET;
	wcscpy_s(source.lfFaceName, L"Noto Sans JP Thin");
	EXPECT_EQ(FW_THIN, CFontFallback::ResolveBaseFont(source).lfWeight);
	const SFontFallbacks fallbacks{};
	CFontFallback fonts(source, fallbacks);
	HDC dc = CreateCompatibleDC(nullptr);
	ASSERT_NE(nullptr, dc);
	TEXTMETRIC normal{}, bold{};
	const auto old = SelectObject(dc, fonts.GetFont(0));
	const BOOL normalMeasured = GetTextMetrics(dc, &normal);
	SelectObject(dc, fonts.GetFont(0, true));
	const BOOL boldMeasured = GetTextMetrics(dc, &bold);
	SelectObject(dc, old);
	DeleteDC(dc);
	ASSERT_TRUE(normalMeasured);
	ASSERT_TRUE(boldMeasured);
	EXPECT_EQ(FW_THIN, normal.tmWeight);
	EXPECT_GE(bold.tmWeight, FW_BOLD);
}
