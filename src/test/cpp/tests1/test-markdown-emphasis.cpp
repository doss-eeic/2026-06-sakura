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
#include "cmd/markdown/MarkdownCommands.h"

TEST(MarkdownEmphasisEditing, SplitsFormattingAtSelectionBoundaries)
{
	struct Case {
		const wchar_t* source;
		const wchar_t* selection;
		Markdown::Style command;
		const wchar_t* expected;
		const wchar_t* selectedAfter;
	};
	const Case cases[] = {
		{L"**abc**", L"*abc**", Markdown::Strike, L"**~~abc~~**", L"abc"},
		{L"**abc** def", L"c** d", Markdown::Strike, L"**ab**~~**c** d~~ef", L"c** d"},
		{L"**abc**", L"ab", Markdown::Bold, L"ab**c**", L"ab"},
		{L"**abc**", L"b", Markdown::Bold, L"**a**b**c**", L"b"},
		{L"**abc**", L"bc", Markdown::Bold, L"**a**bc", L"bc"},
		{L"*abc*", L"b", Markdown::Italic, L"*a*b*c*", L"b"},
		{L"~~abc~~", L"b", Markdown::Strike, L"~~a~~b~~c~~", L"b"},
		{L"***abc***", L"ab", Markdown::Bold, L"*ab**c***", L"ab"},
		{L"**abc**", L"b", Markdown::Italic, L"**a*b*c**", L"b"},
		{L"**ab** cd", L"**ab** cd", Markdown::Bold, L"**ab cd**", L"ab cd"},
		{L"__abc__", L"b", Markdown::Bold, L"**a**b**c**", L"b"},
		{L"**abc def**", L"abc", Markdown::Bold, L"abc **def**", L"abc"},
		{L"**abc** and *tail*", L"b", Markdown::Bold, L"**a**b**c** and *tail*", L"b"},
		{L"**ab\r\ncd** ef", L"b\r\ncd** e", Markdown::Strike,
			L"**a**~~**b\r\ncd** e~~f", L"b\r\ncd** e"},
		{L"**ab\r\ncd**", L"b\r\nc", Markdown::Bold, L"**a**b\r\nc**d**", L"b\r\nc"},
		{L"ab\ncd", L"ab\ncd", Markdown::Bold, L"**ab\ncd**", L"ab\ncd"},
		{L"ab\rcd", L"ab\rcd", Markdown::Bold, L"**ab\rcd**", L"ab\rcd"},
		{L"**ab**\r\n", L"*ab**\r\n", Markdown::Bold, L"ab\r\n", L"ab\r\n"},
	};
	for (const auto& test : cases) {
		SCOPED_TRACE(test.source);
		SCOPED_TRACE(test.selection);
		const std::wstring source(test.source);
		const size_t from = source.find(test.selection);
		ASSERT_NE(std::wstring::npos, from);
		std::vector<CMarkdownInlineParser::EmphasisSpan> spans;
		CMarkdownInlineParser::Parse(source, &spans);
		const auto edit = Markdown::Commands::ToggleEmphasis(source, spans,
			from, from + std::wstring_view(test.selection).size(), test.command);
		ASSERT_TRUE(edit.has_value());
		const auto actual = source.substr(0, edit->from) + edit->text + source.substr(edit->to);
		EXPECT_EQ(test.expected, actual);
		ASSERT_LE(edit->selectionFrom, edit->selectionTo);
		ASSERT_LE(edit->selectionTo, edit->text.size());
		EXPECT_EQ(test.selectedAfter, edit->text.substr(edit->selectionFrom,
			edit->selectionTo - edit->selectionFrom));
	}
}

TEST(MarkdownEmphasisEditing, EscapedAndUnpairedMarkersRemainText)
{
	const std::wstring source = L"\\*abc\\* + *";
	std::vector<CMarkdownInlineParser::EmphasisSpan> spans;
	CMarkdownInlineParser::Parse(source, &spans);
	ASSERT_TRUE(spans.empty());
	const auto edit = Markdown::Commands::ToggleEmphasis(source, spans, 0, 7, Markdown::Bold);
	ASSERT_TRUE(edit.has_value());
	EXPECT_EQ(L"**\\*abc\\*** + *", source.substr(0, edit->from) + edit->text + source.substr(edit->to));
	EXPECT_EQ(L"\\*abc\\*", edit->text.substr(edit->selectionFrom, edit->selectionTo - edit->selectionFrom));
}

TEST(MarkdownEmphasisEditing, MarkerOnlySelectionDoesNotCreateAnEmptyPair)
{
	const std::wstring source = L"**abc**";
	std::vector<CMarkdownInlineParser::EmphasisSpan> spans;
	CMarkdownInlineParser::Parse(source, &spans);
	EXPECT_FALSE(Markdown::Commands::ToggleEmphasis(source, spans, 0, 2, Markdown::Strike));
}

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
