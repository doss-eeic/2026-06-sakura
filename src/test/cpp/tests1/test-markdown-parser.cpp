/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "pch.h"
#include "parse/markdown/CMarkdownBlockParser.h"
#include "parse/markdown/CMarkdownInlineParser.h"

namespace {
void ExpectStyles(std::wstring_view text, std::string_view expected)
{
	const auto styles = CMarkdownInlineParser::Parse(text);
	ASSERT_EQ(text.size(), styles.size());
	ASSERT_EQ(text.size(), expected.size());
	for (size_t pos = 0; pos < styles.size(); ++pos) {
		EXPECT_EQ(expected[pos] - '0', styles[pos]) << "position " << pos;
	}
}
}

TEST(MarkdownInlineParser, BoldItalicAndNesting)
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

TEST(MarkdownInlineParser, DelimiterRunCanCloseThenOpen)
{
	ExpectStyles(L"**a***b*", "00100020");
	ExpectStyles(L"*a **b***", "022003000");
	ExpectStyles(L"a**b*c", "000000"); // rule of three
}

TEST(MarkdownInlineParser, StrikeAndCombinedEmphasis)
{
	ExpectStyles(L"~~取消~~", "004400");
	ExpectStyles(L"~~a b~~", "0044400");
	ExpectStyles(L"~~a\tb~~", "0044400");
	ExpectStyles(L"~~a\nb~~", "0044400");
	ExpectStyles(L"~~**b**~~", "000050000");
	ExpectStyles(L"*~~i~~*", "0006000");
	ExpectStyles(L"~~***x***~~", "00000700000");
	ExpectStyles(L"~~a~~ ~~b~~", "00400000400");
	ExpectStyles(L"前~~取消~~後", "00044000");
}

TEST(MarkdownInlineParser, StrikeIgnoresLiteralAndUnpairedTildes)
{
	for (const auto text : {L"~x~", L"~~~x~~~", L"~~~~x~~~~", L"~~unfinished", L"~~ x ~~",
		L"\\~~x\\~~", L"`~~x~~`", L"[x](https://host/~~x~~)", L"<https://host/~~x~~>"}) {
		const std::wstring_view value(text);
		ExpectStyles(value, std::string(value.size(), '0'));
	}
	ExpectStyles(L"~~a `b` c~~", "00440004400");
}

TEST(MarkdownInlineParser, UnmatchedEscapedAndIntrawordMarkersAreLiteral)
{
	for (const auto text : {L"*unfinished", L"** unfinished **", L"foo_bar_baz", L"\\*x\\*", L"* item", L"a_b_c"}) {
		const std::wstring_view value(text);
		ExpectStyles(value, std::string(value.size(), '0'));
	}
}

TEST(MarkdownInlineParser, CodeSpansAndDestinationsAreLiteral)
{
	for (const auto text : {L"`**code**`", L"`` `*code*` ``", L"[label](https://host/a_b_c)", L"<https://host/*path*>"}) {
		const std::wstring_view value(text);
		ExpectStyles(value, std::string(value.size(), '0'));
	}
	ExpectStyles(L"[**b**](url)", "000100000000");
	ExpectStyles(L"**a `b` c**", "00110001100");
	ExpectStyles(L"**a\nb**", "0011100");
}

TEST(MarkdownBlockParser, FenceLengthMarkerAndIndentation)
{
	Markdown::BlockState state;
	EXPECT_TRUE(CMarkdownBlockParser::ScanBlock(L"````cpp", state).code);
	EXPECT_TRUE(CMarkdownBlockParser::ScanBlock(L"**code**", state).code);
	EXPECT_TRUE(CMarkdownBlockParser::ScanBlock(L"```", state).code);
	EXPECT_EQ(L'`', state.fence);
	EXPECT_TRUE(CMarkdownBlockParser::ScanBlock(L"~~~~", state).code);
	EXPECT_EQ(L'`', state.fence);
	EXPECT_TRUE(CMarkdownBlockParser::ScanBlock(L"```` trailing", state).code);
	EXPECT_EQ(L'`', state.fence);
	EXPECT_TRUE(CMarkdownBlockParser::ScanBlock(L"`````", state).code);
	EXPECT_EQ(0, state.fence);
	EXPECT_FALSE(CMarkdownBlockParser::ScanBlock(L"**text**", state).code);
	EXPECT_TRUE(CMarkdownBlockParser::ScanBlock(L"    **code**", state).code);
	EXPECT_TRUE(CMarkdownBlockParser::ScanBlock(L"> ~~~", state).code);
	EXPECT_TRUE(CMarkdownBlockParser::ScanBlock(L"> *code*", state).code);
	EXPECT_TRUE(CMarkdownBlockParser::ScanBlock(L"> ~~~", state).code);
	EXPECT_EQ(0, state.fence);
}

TEST(MarkdownBlockParser, EditIndentCountsSourceCharacters)
{
	EXPECT_EQ(0u, CMarkdownBlockParser::IndentLength(L""));
	EXPECT_EQ(3u, CMarkdownBlockParser::IndentLength(L" \t "));
	EXPECT_EQ(2u, CMarkdownBlockParser::IndentLength(L"\t - item"));
	EXPECT_EQ(0u, CMarkdownBlockParser::IndentLength(L"　item"));
}

TEST(MarkdownBlockParser, EditListMarkersKeepExistingBoundariesAndFlags)
{
	struct Example {
		const wchar_t* text;
		size_t length;
		bool ordered, task;
	};
	for (const auto& example : {
		Example{L"  - text", 2, false, false},
		Example{L"+\ttext", 2, false, false},
		Example{L"* [X] text", 6, false, true},
		Example{L"12) [ ] text", 8, true, true},
		Example{L"1. [x]", 6, true, true},
		Example{L"- [x]text", 2, false, false},
		Example{L"-  [ ]  text", 7, false, true},
		Example{L"123456789. text", 11, true, false},
		Example{L"1234567890. text", 0, false, false},
		Example{L"1.text", 0, true, false},
		Example{L"> - text", 0, false, false},
		Example{L"-", 0, false, false}}) {
		bool ordered = true, task = true;
		const auto indent = CMarkdownBlockParser::IndentLength(example.text);
		EXPECT_EQ(example.length, CMarkdownBlockParser::ListMarkerLength(example.text, indent, ordered, task));
		EXPECT_EQ(example.ordered, ordered);
		EXPECT_EQ(example.task, task);
	}
}

TEST(MarkdownBlockParser, EditHeadingRequiresSpaceAndAtMostSixHashes)
{
	EXPECT_EQ(3u, CMarkdownBlockParser::HeadingMarkerLength(L"  ## title", 2));
	EXPECT_EQ(7u, CMarkdownBlockParser::HeadingMarkerLength(L"###### title", 0));
	EXPECT_EQ(0u, CMarkdownBlockParser::HeadingMarkerLength(L"####### title", 0));
	EXPECT_EQ(0u, CMarkdownBlockParser::HeadingMarkerLength(L"#\ttitle", 0));
	EXPECT_EQ(0u, CMarkdownBlockParser::HeadingMarkerLength(L"#", 0));
}

TEST(MarkdownBlockParser, EditFenceRecognitionRemainsDistinctFromDisplayScan)
{
	EXPECT_TRUE(CMarkdownBlockParser::IsBacktickFence(L"```cpp", L"````"));
	EXPECT_TRUE(CMarkdownBlockParser::IsBacktickFence(L"```", L"```"));
	EXPECT_FALSE(CMarkdownBlockParser::IsBacktickFence(L"````", L"```"));
	EXPECT_FALSE(CMarkdownBlockParser::IsBacktickFence(L"```", L"``` "));
	EXPECT_FALSE(CMarkdownBlockParser::IsBacktickFence(L" ```", L"```"));
	EXPECT_FALSE(CMarkdownBlockParser::IsBacktickFence(L"~~~", L"~~~"));
	Markdown::BlockState state;
	EXPECT_TRUE(CMarkdownBlockParser::ScanBlock(L"~~~", state).code);
	EXPECT_EQ(L'~', state.fence);
}
