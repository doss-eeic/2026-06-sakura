/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "pch.h"
#include "parse/markdown/CMarkdownBlockParser.h"
#include "parse/markdown/CMarkdownInlineParser.h"
#include <limits>

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

namespace {
void ExpectPosition(const Markdown::SourcePosition& position, size_t line, size_t offset)
{
	EXPECT_EQ(line, position.line);
	EXPECT_EQ(offset, position.offset);
}

void ExpectRange(const std::optional<Markdown::SourceRange>& range, size_t line, size_t begin, size_t end)
{
	ASSERT_TRUE(range.has_value());
	ExpectPosition(range->begin, line, begin);
	ExpectPosition(range->end, line, end);
}
}

TEST(MarkdownBlockParser, ContextOrderedNumberAndSourceRanges)
{
	Markdown::ParseState state;
	const auto line = CMarkdownBlockParser::ParseLine(L"12. 日本", 0, 2, state);
	ASSERT_EQ(1u, line.containers.size());
	const auto& item = line.containers.front();
	ASSERT_TRUE(item.list.has_value());
	EXPECT_EQ(Markdown::ListKind::Ordered, item.list->kind);
	EXPECT_EQ(12u, item.list->number);
	EXPECT_EQ(L'.', item.list->marker);
	ExpectRange(item.list->range, 0, 0, 3);
	ExpectRange(item.prefixRange, 0, 0, 4);
	ExpectRange(line.bodyRange, 0, 4, 6);
	EXPECT_EQ(4u, item.contentColumn);
	EXPECT_EQ(Markdown::LineKind::ListItem, line.kind);
	EXPECT_TRUE(state.paragraphOpen);
}

TEST(MarkdownBlockParser, ContextOrderedNumberChecksUnsignedLimitAndDigitCount)
{
	const auto maximum = (std::numeric_limits<unsigned int>::max)();
	for (const auto number : {0u, 1u, maximum}) {
		Markdown::ParseState state;
		const auto text = std::to_wstring(number) + L". text";
		const auto line = CMarkdownBlockParser::ParseLine(text, 0, 2, state);
		ASSERT_EQ(1u, line.containers.size());
		ASSERT_TRUE(line.containers[0].list.has_value());
		EXPECT_EQ(number, line.containers[0].list->number);
	}
	const auto overflow = std::to_wstring(static_cast<unsigned long long>(maximum) + 1) + L". text";
	const auto tooManyDigits = std::wstring(std::numeric_limits<unsigned int>::digits10 + 2, L'0') + L". text";
	for (const auto& text : {overflow, tooManyDigits, std::wstring(L"1a text"), std::wstring(L"1.text")}) {
		Markdown::ParseState state;
		const auto line = CMarkdownBlockParser::ParseLine(text, 0, 2, state);
		EXPECT_EQ(Markdown::LineKind::Paragraph, line.kind);
		EXPECT_TRUE(line.containers.empty());
		ExpectRange(line.bodyRange, 0, 0, text.size());
	}
	Markdown::ParseState state;
	const auto zeroPadded = CMarkdownBlockParser::ParseLine(L"00012. text", 0, 2, state);
	ASSERT_EQ(1u, zeroPadded.containers.size());
	ASSERT_TRUE(zeroPadded.containers[0].list.has_value());
	EXPECT_EQ(12u, zeroPadded.containers[0].list->number);
	ExpectRange(zeroPadded.containers[0].list->range, 0, 0, 6);
}

TEST(MarkdownBlockParser, ContextTaskIncludesCheckboxInPrefix)
{
	Markdown::ParseState state;
	const auto line = CMarkdownBlockParser::ParseLine(L"> - [x] 日本語", 0, 2, state);
	EXPECT_EQ(Markdown::LineKind::ListItem, line.kind);
	ASSERT_EQ(2u, line.containers.size());
	EXPECT_EQ(Markdown::ContainerKind::Quote, line.containers[0].kind);
	const auto& item = line.containers[1];
	ASSERT_TRUE(item.list.has_value());
	ASSERT_TRUE(item.list->task.has_value());
	EXPECT_TRUE(item.list->task->checked);
	ExpectRange(item.list->range, 0, 2, 3);
	ExpectRange(item.list->task->range, 0, 4, 7);
	ExpectRange(item.prefixRange, 0, 2, 8);
	ExpectRange(line.bodyRange, 0, 8, 11);
	EXPECT_EQ(4u, item.contentColumn);
	EXPECT_FALSE(line.bodyEmpty);
}

TEST(MarkdownBlockParser, ContextTaskBodyIndentExcludesCheckbox)
{
	for (size_t tabWidth : {2u, 4u}) {
		Markdown::ParseState state;
		const auto line = CMarkdownBlockParser::ParseLine(L"- [x]\ttext", 0, tabWidth, state);
		EXPECT_EQ(Markdown::LineKind::ListItem, line.kind);
		ExpectRange(line.bodyRange, 0, 6, 10);
		EXPECT_TRUE(state.paragraphOpen);
		const auto continued = CMarkdownBlockParser::ParseLine(L"  continued", 1, tabWidth, state);
		EXPECT_EQ(Markdown::LineKind::Continuation, continued.kind);
	}
	Markdown::ParseState state;
	const auto code = CMarkdownBlockParser::ParseLine(L"- [ ]     code", 0, 2, state);
	EXPECT_EQ(Markdown::LineKind::IndentedCode, code.kind);
	ExpectRange(code.bodyRange, 0, 10, 14);
}

TEST(MarkdownBlockParser, ContextTaskBoundaryAndEmptyItems)
{
	for (const auto text : {L"- [ ] ", L"- [x]", L"- [x]\t"}) {
		Markdown::ParseState state;
		const auto line = CMarkdownBlockParser::ParseLine(text, 0, 2, state);
		ASSERT_EQ(1u, line.containers.size());
		ASSERT_TRUE(line.containers[0].list.has_value());
		ASSERT_TRUE(line.containers[0].list->task.has_value());
		EXPECT_EQ(Markdown::LineKind::ListItem, line.kind);
		EXPECT_TRUE(line.bodyEmpty);
		EXPECT_FALSE(state.paragraphOpen);
	}
	Markdown::ParseState state;
	const auto literal = CMarkdownBlockParser::ParseLine(L"- [x]text", 0, 2, state);
	ASSERT_EQ(1u, literal.containers.size());
	ASSERT_TRUE(literal.containers[0].list.has_value());
	EXPECT_FALSE(literal.containers[0].list->task.has_value());
	ExpectRange(literal.bodyRange, 0, 2, 9);
}

TEST(MarkdownBlockParser, ContextKeepsExplicitPrefixRecognitionRules)
{
	for (const auto text : {L">text", L">> text", L"-\ttext", L"1) text"}) {
		Markdown::ParseState state;
		const auto line = CMarkdownBlockParser::ParseLine(text, 0, 2, state);
		EXPECT_EQ(Markdown::LineKind::Paragraph, line.kind);
		EXPECT_TRUE(line.containers.empty());
	}
	Markdown::ParseState state;
	const auto line = CMarkdownBlockParser::ParseLine(L"- [X] text", 0, 2, state);
	ASSERT_EQ(1u, line.containers.size());
	ASSERT_TRUE(line.containers[0].list.has_value());
	EXPECT_FALSE(line.containers[0].list->task.has_value());
	ExpectRange(line.bodyRange, 0, 2, 10);
}

TEST(MarkdownBlockParser, ContextTabWidthAndUtf16OffsetsAreIndependent)
{
	Markdown::ParseState state;
	const auto line = CMarkdownBlockParser::ParseLine(L"\t- 日本", 0, 2, state);
	ASSERT_EQ(1u, line.containers.size());
	ExpectPosition(line.containers[0].start, 0, 1);
	ExpectRange(line.containers[0].prefixRange, 0, 1, 3);
	ExpectRange(line.bodyRange, 0, 3, 5);
	EXPECT_EQ(4u, line.containers[0].contentColumn);
	state = {};
	const auto wider = CMarkdownBlockParser::ParseLine(L"\t- 日本", 0, 3, state);
	ASSERT_EQ(1u, wider.containers.size());
	EXPECT_EQ(5u, wider.containers[0].contentColumn);
	state = {};
	const auto code = CMarkdownBlockParser::ParseLine(L"\t- 日本", 0, 4, state);
	EXPECT_EQ(Markdown::LineKind::IndentedCode, code.kind);
	EXPECT_TRUE(code.containers.empty());
	ExpectRange(code.bodyRange, 0, 1, 5);
	state = {};
	const auto surrogate = CMarkdownBlockParser::ParseLine(L"- \xD83C\xDF38" L"文", 0, 2, state);
	ExpectRange(surrogate.bodyRange, 0, 2, 5);
}

TEST(MarkdownBlockParser, ContextBlankAndEmptyItemAreDifferent)
{
	Markdown::ParseState state;
	const auto blank = CMarkdownBlockParser::ParseLine(L" \t ", 0, 2, state);
	EXPECT_EQ(Markdown::LineKind::Blank, blank.kind);
	EXPECT_TRUE(blank.bodyEmpty);
	EXPECT_TRUE(state.previousLineBlank);
	ExpectRange(blank.bodyRange, 0, 0, 3);
	const auto item = CMarkdownBlockParser::ParseLine(L"- ", 1, 2, state);
	EXPECT_EQ(Markdown::LineKind::ListItem, item.kind);
	EXPECT_TRUE(item.bodyEmpty);
	EXPECT_FALSE(state.previousLineBlank);
	ASSERT_EQ(1u, item.containers.size());
	ExpectRange(item.bodyRange, 1, 2, 2);
	state = {};
	const auto quote = CMarkdownBlockParser::ParseLine(L"> ", 0, 2, state);
	EXPECT_EQ(Markdown::LineKind::Blank, quote.kind);
	ASSERT_EQ(1u, quote.containers.size());
	EXPECT_FALSE(state.paragraphOpen);
}

TEST(MarkdownBlockParser, ContextNestedItemsKeepIdentityAcrossLines)
{
	Markdown::ParseState state;
	CMarkdownBlockParser::ParseLine(L"- parent", 0, 2, state);
	const auto child = CMarkdownBlockParser::ParseLine(L"  - child", 1, 2, state);
	ASSERT_EQ(2u, child.containers.size());
	ExpectPosition(child.containers[0].start, 0, 0);
	EXPECT_FALSE(child.containers[0].prefixRange.has_value());
	ExpectPosition(child.containers[1].start, 1, 2);
	ASSERT_TRUE(child.containers[1].parentItem.has_value());
	ExpectPosition(*child.containers[1].parentItem, 0, 0);
	EXPECT_EQ(4u, child.containers[1].contentColumn);
	const auto continued = CMarkdownBlockParser::ParseLine(L"\t\tchild text", 2, 2, state);
	EXPECT_EQ(Markdown::LineKind::Continuation, continued.kind);
	ASSERT_EQ(2u, continued.containers.size());
	ExpectPosition(continued.containers[1].start, 1, 2);
	EXPECT_FALSE(continued.containers[1].prefixRange.has_value());
	ExpectRange(continued.bodyRange, 2, 2, 12);
	CMarkdownBlockParser::ParseLine(L"", 3, 2, state);
	const auto parent = CMarkdownBlockParser::ParseLine(L"  parent text", 4, 2, state);
	EXPECT_EQ(Markdown::LineKind::Continuation, parent.kind);
	ASSERT_EQ(1u, parent.containers.size());
	ExpectPosition(parent.containers[0].start, 0, 0);
	const auto sibling = CMarkdownBlockParser::ParseLine(L"- sibling", 5, 2, state);
	EXPECT_EQ(Markdown::LineKind::ListItem, sibling.kind);
	ASSERT_EQ(1u, sibling.containers.size());
	ExpectPosition(sibling.containers[0].start, 5, 0);
	EXPECT_FALSE(sibling.containers[0].parentItem.has_value());
}

TEST(MarkdownBlockParser, ContextOrderedMarkerWidthDeterminesChildIndent)
{
	Markdown::ParseState state;
	CMarkdownBlockParser::ParseLine(L"12. parent", 0, 2, state);
	const auto child = CMarkdownBlockParser::ParseLine(L"\t\t- child", 1, 2, state);
	ASSERT_EQ(2u, child.containers.size());
	EXPECT_EQ(4u, child.containers[0].contentColumn);
	EXPECT_EQ(6u, child.containers[1].contentColumn);
	ASSERT_TRUE(child.containers[1].parentItem.has_value());
	ExpectPosition(*child.containers[1].parentItem, 0, 0);
	state = {};
	CMarkdownBlockParser::ParseLine(L"12. parent", 0, 2, state);
	const auto sibling = CMarkdownBlockParser::ParseLine(L"\t- sibling", 1, 2, state);
	ASSERT_EQ(1u, sibling.containers.size());
	EXPECT_FALSE(sibling.containers[0].parentItem.has_value());
	ExpectPosition(sibling.containers[0].start, 1, 1);
}

TEST(MarkdownBlockParser, ContextQuoteAndListOrderIsPreserved)
{
	Markdown::ParseState state;
	const auto first = CMarkdownBlockParser::ParseLine(L"- > text", 0, 2, state);
	ASSERT_EQ(2u, first.containers.size());
	EXPECT_EQ(Markdown::ContainerKind::ListItem, first.containers[0].kind);
	EXPECT_EQ(Markdown::ContainerKind::Quote, first.containers[1].kind);
	ASSERT_TRUE(first.containers[1].parentItem.has_value());
	ExpectPosition(*first.containers[1].parentItem, 0, 0);
	const auto next = CMarkdownBlockParser::ParseLine(L"  > again", 1, 2, state);
	ASSERT_EQ(2u, next.containers.size());
	ExpectPosition(next.containers[1].start, 0, 2);
	ExpectRange(next.containers[1].prefixRange, 1, 2, 4);
	EXPECT_EQ(Markdown::LineKind::Continuation, next.kind);
	state = {};
	const auto inverse = CMarkdownBlockParser::ParseLine(L"> - text", 0, 2, state);
	ASSERT_EQ(2u, inverse.containers.size());
	EXPECT_EQ(Markdown::ContainerKind::Quote, inverse.containers[0].kind);
	EXPECT_EQ(Markdown::ContainerKind::ListItem, inverse.containers[1].kind);
	EXPECT_FALSE(inverse.containers[1].parentItem.has_value());
}

TEST(MarkdownBlockParser, ContextQuoteIndentCanMoveWithoutChangingItemIdentity)
{
	Markdown::ParseState state;
	CMarkdownBlockParser::ParseLine(L"> - item", 0, 2, state);
	const auto shifted = CMarkdownBlockParser::ParseLine(L" >   more", 1, 2, state);
	ASSERT_EQ(2u, shifted.containers.size());
	EXPECT_EQ(3u, shifted.containers[0].contentColumn);
	EXPECT_EQ(5u, shifted.containers[1].contentColumn);
	ExpectPosition(shifted.containers[1].start, 0, 2);
	ExpectRange(shifted.bodyRange, 1, 5, 9);
	const auto restored = CMarkdownBlockParser::ParseLine(L">   again", 2, 2, state);
	ASSERT_EQ(2u, restored.containers.size());
	EXPECT_EQ(4u, restored.containers[1].contentColumn);
	ExpectPosition(restored.containers[1].start, 0, 2);
	ExpectRange(restored.bodyRange, 2, 4, 9);
}

TEST(MarkdownBlockParser, ContextLazyParagraphContinuationStopsAtBlankOrBlock)
{
	Markdown::ParseState state;
	CMarkdownBlockParser::ParseLine(L"> - item", 0, 2, state);
	const auto lazy = CMarkdownBlockParser::ParseLine(L"continued", 1, 2, state);
	EXPECT_EQ(Markdown::LineKind::Continuation, lazy.kind);
	ASSERT_EQ(2u, lazy.containers.size());
	EXPECT_FALSE(lazy.containers[0].prefixRange.has_value());
	EXPECT_FALSE(lazy.containers[1].prefixRange.has_value());
	ExpectRange(lazy.bodyRange, 1, 0, 9);
	const auto blank = CMarkdownBlockParser::ParseLine(L"", 2, 2, state);
	EXPECT_EQ(Markdown::LineKind::Blank, blank.kind);
	EXPECT_TRUE(blank.containers.empty());
	const auto outside = CMarkdownBlockParser::ParseLine(L"outside", 3, 2, state);
	EXPECT_EQ(Markdown::LineKind::Paragraph, outside.kind);
	EXPECT_TRUE(outside.containers.empty());
	state = {};
	CMarkdownBlockParser::ParseLine(L"- item", 0, 2, state);
	const auto heading = CMarkdownBlockParser::ParseLine(L"# heading", 1, 2, state);
	EXPECT_EQ(Markdown::LineKind::Heading, heading.kind);
	EXPECT_TRUE(heading.containers.empty());
	ExpectRange(heading.bodyRange, 1, 2, 9);
}

TEST(MarkdownBlockParser, ContextListSurvivesBlankButUnindentedParagraphLeavesIt)
{
	Markdown::ParseState state;
	CMarkdownBlockParser::ParseLine(L"- [x] item", 0, 2, state);
	const auto blank = CMarkdownBlockParser::ParseLine(L"", 1, 2, state);
	ASSERT_EQ(1u, blank.containers.size());
	EXPECT_TRUE(state.previousLineBlank);
	const auto continued = CMarkdownBlockParser::ParseLine(L"  more", 2, 2, state);
	EXPECT_EQ(Markdown::LineKind::Continuation, continued.kind);
	ASSERT_EQ(1u, continued.containers.size());
	ASSERT_TRUE(continued.containers[0].list.has_value());
	ASSERT_TRUE(continued.containers[0].list->task.has_value());
	ExpectRange(continued.containers[0].list->task->range, 0, 2, 5);
	EXPECT_FALSE(continued.containers[0].prefixRange.has_value());
	CMarkdownBlockParser::ParseLine(L"", 3, 2, state);
	const auto outside = CMarkdownBlockParser::ParseLine(L"outside", 4, 2, state);
	EXPECT_TRUE(outside.containers.empty());
	EXPECT_EQ(Markdown::LineKind::Paragraph, outside.kind);
}

TEST(MarkdownBlockParser, ContextHeadingsAndRulesTakePrecedenceOverLists)
{
	for (const auto text : {L"---", L"* * *", L"_\t_\t_"}) {
		Markdown::ParseState state;
		const auto line = CMarkdownBlockParser::ParseLine(text, 0, 2, state);
		EXPECT_EQ(Markdown::LineKind::ThematicBreak, line.kind);
		EXPECT_TRUE(line.containers.empty());
		EXPECT_FALSE(state.paragraphOpen);
	}
	for (const auto text : {L"#", L"## title", L"###### title"}) {
		Markdown::ParseState state;
		const auto line = CMarkdownBlockParser::ParseLine(text, 0, 2, state);
		EXPECT_EQ(Markdown::LineKind::Heading, line.kind);
	}
	for (const auto text : {L"--", L"####### title", L"#title", L"==="}) {
		Markdown::ParseState state;
		EXPECT_EQ(Markdown::LineKind::Paragraph, CMarkdownBlockParser::ParseLine(text, 0, 2, state).kind);
	}
	Markdown::ParseState state;
	const auto quotedRule = CMarkdownBlockParser::ParseLine(L"> * * *", 0, 2, state);
	EXPECT_EQ(Markdown::LineKind::ThematicBreak, quotedRule.kind);
	ASSERT_EQ(1u, quotedRule.containers.size());
	EXPECT_EQ(Markdown::ContainerKind::Quote, quotedRule.containers[0].kind);
}

TEST(MarkdownBlockParser, ContextSetextNeedsParagraphInSameContainers)
{
	Markdown::ParseState state;
	CMarkdownBlockParser::ParseLine(L"title", 0, 2, state);
	EXPECT_EQ(Markdown::LineKind::Heading, CMarkdownBlockParser::ParseLine(L"---", 1, 2, state).kind);
	CMarkdownBlockParser::ParseLine(L"", 2, 2, state);
	EXPECT_EQ(Markdown::LineKind::ThematicBreak, CMarkdownBlockParser::ParseLine(L"---", 3, 2, state).kind);
	state = {};
	CMarkdownBlockParser::ParseLine(L"- title", 0, 2, state);
	const auto nested = CMarkdownBlockParser::ParseLine(L"  ===", 1, 2, state);
	EXPECT_EQ(Markdown::LineKind::Heading, nested.kind);
	ASSERT_EQ(1u, nested.containers.size());
	state = {};
	CMarkdownBlockParser::ParseLine(L"> title", 0, 2, state);
	const auto outside = CMarkdownBlockParser::ParseLine(L"---", 1, 2, state);
	EXPECT_EQ(Markdown::LineKind::ThematicBreak, outside.kind);
	EXPECT_TRUE(outside.containers.empty());
}

TEST(MarkdownBlockParser, ContextHeadingMetadataKeepsSourceRangesAndLiteralHashes)
{
	Markdown::ParseState state;
	const auto line = CMarkdownBlockParser::ParseLine(L"> ## title ###  ", 0, 2, state);
	ASSERT_TRUE(line.heading);
	EXPECT_EQ(2u, line.heading->level);
	ExpectRange(line.heading->markerRange, 0, 2, 4);
	ExpectRange(line.heading->titleRange, 0, 5, 10);
	state = {};
	const auto literal = CMarkdownBlockParser::ParseLine(L"# C#", 0, 2, state);
	ASSERT_TRUE(literal.heading);
	ExpectRange(literal.heading->titleRange, 0, 2, 4);
	state = {};
	const auto empty = CMarkdownBlockParser::ParseLine(L"# ###", 0, 2, state);
	ASSERT_TRUE(empty.heading);
	ExpectRange(empty.heading->titleRange, 0, 2, 2);
	state = {};
	CMarkdownBlockParser::ParseLine(L"title", 0, 2, state);
	const auto setext = CMarkdownBlockParser::ParseLine(L"===", 1, 2, state);
	ASSERT_TRUE(setext.heading);
	EXPECT_EQ(1u, setext.heading->level);
	EXPECT_FALSE(setext.heading->titleRange);
	ExpectRange(setext.heading->markerRange, 1, 0, 3);
}

TEST(MarkdownBlockParser, ContextEmptyBulletTakesPrecedenceOverSetext)
{
	Markdown::ParseState state;
	CMarkdownBlockParser::ParseLine(L"3. parent", 0, 2, state);
	const auto child = CMarkdownBlockParser::ParseLine(L"   - ", 1, 2, state);
	EXPECT_EQ(Markdown::LineKind::ListItem, child.kind);
	ASSERT_EQ(2u, child.containers.size());
	ExpectRange(child.containers[1].prefixRange, 1, 3, 5);
	EXPECT_TRUE(child.bodyEmpty);
	state = {};
	CMarkdownBlockParser::ParseLine(L"title", 0, 2, state);
	EXPECT_EQ(Markdown::LineKind::Heading, CMarkdownBlockParser::ParseLine(L"-", 1, 2, state).kind);
}

TEST(MarkdownBlockParser, ContextIndentedCodeAndItsBlankLines)
{
	Markdown::ParseState state;
	const auto code = CMarkdownBlockParser::ParseLine(L"\t\t- code", 0, 2, state);
	EXPECT_EQ(Markdown::LineKind::IndentedCode, code.kind);
	EXPECT_TRUE(code.containers.empty());
	ExpectRange(code.bodyRange, 0, 2, 8);
	EXPECT_TRUE(state.indentedCodeOpen);
	EXPECT_EQ(Markdown::LineKind::IndentedCode, CMarkdownBlockParser::ParseLine(L"", 1, 2, state).kind);
	const auto outside = CMarkdownBlockParser::ParseLine(L"text", 2, 2, state);
	EXPECT_EQ(Markdown::LineKind::Paragraph, outside.kind);
	EXPECT_FALSE(state.indentedCodeOpen);
	const auto paragraph = CMarkdownBlockParser::ParseLine(L"    continued", 3, 2, state);
	EXPECT_EQ(Markdown::LineKind::Paragraph, paragraph.kind);
	ExpectRange(paragraph.bodyRange, 3, 0, 13);
}

TEST(MarkdownBlockParser, ContextCodeIndentIsRelativeToItemContent)
{
	Markdown::ParseState state;
	CMarkdownBlockParser::ParseLine(L"- item", 0, 2, state);
	const auto paragraph = CMarkdownBlockParser::ParseLine(L"      continued", 1, 2, state);
	EXPECT_EQ(Markdown::LineKind::Continuation, paragraph.kind);
	ExpectRange(paragraph.bodyRange, 1, 2, 15);
	CMarkdownBlockParser::ParseLine(L"", 2, 2, state);
	const auto code = CMarkdownBlockParser::ParseLine(L"      - code", 3, 2, state);
	EXPECT_EQ(Markdown::LineKind::IndentedCode, code.kind);
	ASSERT_EQ(1u, code.containers.size());
	ExpectRange(code.bodyRange, 3, 6, 12);
	const auto after = CMarkdownBlockParser::ParseLine(L"  after", 4, 2, state);
	EXPECT_EQ(Markdown::LineKind::Continuation, after.kind);
	EXPECT_FALSE(state.indentedCodeOpen);
	state = {};
	const auto firstLine = CMarkdownBlockParser::ParseLine(L"-     code", 0, 2, state);
	EXPECT_EQ(Markdown::LineKind::IndentedCode, firstLine.kind);
	ASSERT_EQ(1u, firstLine.containers.size());
}

TEST(MarkdownBlockParser, ContextFenceLengthMarkerAndTrailingText)
{
	Markdown::ParseState state;
	EXPECT_EQ(Markdown::LineKind::Fence, CMarkdownBlockParser::ParseLine(L"````cpp", 0, 2, state).kind);
	ASSERT_TRUE(state.fence.has_value());
	EXPECT_EQ(4u, state.fence->length);
	EXPECT_EQ(0u, state.fence->containerDepth);
	size_t index = 1;
	for (const auto text : {L"- literal", L"```", L"~~~~", L"```` trailing", L""}) {
		const auto line = CMarkdownBlockParser::ParseLine(text, index++, 2, state);
		EXPECT_EQ(Markdown::LineKind::FencedCode, line.kind);
		EXPECT_TRUE(line.containers.empty());
		EXPECT_TRUE(state.fence.has_value());
	}
	EXPECT_EQ(Markdown::LineKind::Fence, CMarkdownBlockParser::ParseLine(L"`````", index++, 2, state).kind);
	EXPECT_FALSE(state.fence.has_value());
	EXPECT_EQ(Markdown::LineKind::Paragraph, CMarkdownBlockParser::ParseLine(L"text", index, 2, state).kind);
	state = {};
	EXPECT_EQ(Markdown::LineKind::Paragraph, CMarkdownBlockParser::ParseLine(L"```a`b", 0, 2, state).kind);
	EXPECT_FALSE(state.fence.has_value());
}

TEST(MarkdownBlockParser, ContextFenceInsideQuoteAndListStaysLiteral)
{
	Markdown::ParseState state;
	CMarkdownBlockParser::ParseLine(L"> - ~~~", 0, 2, state);
	ASSERT_TRUE(state.fence.has_value());
	EXPECT_EQ(2u, state.fence->containerDepth);
	ExpectPosition(state.fence->start, 0, 4);
	const auto code = CMarkdownBlockParser::ParseLine(L">   - literal", 1, 2, state);
	EXPECT_EQ(Markdown::LineKind::FencedCode, code.kind);
	ASSERT_EQ(2u, code.containers.size());
	ExpectRange(code.bodyRange, 1, 4, 13);
	const auto blank = CMarkdownBlockParser::ParseLine(L"> ", 2, 2, state);
	EXPECT_EQ(Markdown::LineKind::FencedCode, blank.kind);
	EXPECT_TRUE(blank.bodyEmpty);
	EXPECT_TRUE(state.fence.has_value());
	const auto closing = CMarkdownBlockParser::ParseLine(L">   ~~~", 3, 2, state);
	EXPECT_EQ(Markdown::LineKind::Fence, closing.kind);
	EXPECT_FALSE(state.fence.has_value());
	const auto after = CMarkdownBlockParser::ParseLine(L">   after", 4, 2, state);
	EXPECT_EQ(Markdown::LineKind::Continuation, after.kind);
	ASSERT_EQ(2u, after.containers.size());
}

TEST(MarkdownBlockParser, ContextFenceEndsWhenItsOwnerEnds)
{
	Markdown::ParseState state;
	CMarkdownBlockParser::ParseLine(L"> ```", 0, 2, state);
	const auto outside = CMarkdownBlockParser::ParseLine(L"outside", 1, 2, state);
	EXPECT_EQ(Markdown::LineKind::Paragraph, outside.kind);
	EXPECT_TRUE(outside.containers.empty());
	EXPECT_FALSE(state.fence.has_value());
	state = {};
	CMarkdownBlockParser::ParseLine(L"- ```", 0, 2, state);
	const auto sibling = CMarkdownBlockParser::ParseLine(L"- sibling", 1, 2, state);
	EXPECT_EQ(Markdown::LineKind::ListItem, sibling.kind);
	ASSERT_EQ(1u, sibling.containers.size());
	ExpectPosition(sibling.containers[0].start, 1, 0);
	EXPECT_FALSE(state.fence.has_value());
}

TEST(MarkdownBlockParser, ContextFenceRemovesOnlyOpeningIndentFromCode)
{
	Markdown::ParseState state;
	CMarkdownBlockParser::ParseLine(L"  ```", 0, 2, state);
	ASSERT_TRUE(state.fence.has_value());
	EXPECT_EQ(2u, state.fence->indentColumns);
	const auto code = CMarkdownBlockParser::ParseLine(L" \tcode", 1, 2, state);
	ExpectRange(code.bodyRange, 1, 2, 6);
	const auto notClosing = CMarkdownBlockParser::ParseLine(L"    ```", 2, 2, state);
	EXPECT_EQ(Markdown::LineKind::FencedCode, notClosing.kind);
	ExpectRange(notClosing.bodyRange, 2, 2, 7);
	EXPECT_TRUE(state.fence.has_value());
	EXPECT_EQ(Markdown::LineKind::Fence, CMarkdownBlockParser::ParseLine(L" ```", 3, 2, state).kind);
	EXPECT_FALSE(state.fence.has_value());
}

TEST(MarkdownBlockParser, ContextOrdinaryLeadingWhitespaceStaysInBody)
{
	Markdown::ParseState state;
	const auto ordinary = CMarkdownBlockParser::ParseLine(L"  text", 0, 2, state);
	ExpectRange(ordinary.bodyRange, 0, 0, 6);
	state = {};
	const auto quote = CMarkdownBlockParser::ParseLine(L">  text", 0, 2, state);
	ExpectRange(quote.bodyRange, 0, 2, 7);
	state = {};
	const auto item = CMarkdownBlockParser::ParseLine(L"-  text", 0, 2, state);
	ExpectRange(item.bodyRange, 0, 2, 7);
	ASSERT_EQ(1u, item.containers.size());
	ExpectRange(item.containers[0].prefixRange, 0, 0, 2);
}

TEST(MarkdownBlockParser, ContextInvalidTabWidthDoesNotChangeState)
{
	Markdown::ParseState state;
	CMarkdownBlockParser::ParseLine(L"- item", 0, 2, state);
	const auto invalid = CMarkdownBlockParser::ParseLine(L"text", 1, 0, state);
	EXPECT_EQ(Markdown::LineKind::Unknown, invalid.kind);
	EXPECT_FALSE(invalid.bodyRange.has_value());
	ASSERT_EQ(1u, state.containers.size());
	ExpectPosition(state.containers[0].start, 0, 0);
	EXPECT_TRUE(state.paragraphOpen);
}

TEST(MarkdownBlockParser, ContextTabCanCrossContainerBoundary)
{
	Markdown::ParseState state;
	CMarkdownBlockParser::ParseLine(L"- parent", 0, 3, state);
	const auto child = CMarkdownBlockParser::ParseLine(L"\t- child", 1, 3, state);
	ASSERT_EQ(2u, child.containers.size());
	EXPECT_EQ(2u, child.containers[0].contentColumn);
	EXPECT_EQ(5u, child.containers[1].contentColumn);
	ExpectPosition(child.containers[1].start, 1, 1);
	const auto continued = CMarkdownBlockParser::ParseLine(L"\t  more", 2, 3, state);
	EXPECT_EQ(Markdown::LineKind::Continuation, continued.kind);
	ASSERT_EQ(2u, continued.containers.size());
	ExpectRange(continued.bodyRange, 2, 3, 7);
	state = {};
	CMarkdownBlockParser::ParseLine(L"  ```", 0, 4, state);
	const auto code = CMarkdownBlockParser::ParseLine(L"\tcode", 1, 4, state);
	EXPECT_EQ(Markdown::LineKind::FencedCode, code.kind);
	// 字下げに使わなかったタブの残りを、原文範囲から落とさない。
	ExpectRange(code.bodyRange, 1, 0, 5);
}
