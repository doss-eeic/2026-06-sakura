/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "pch.h"
#include "cmd/markdown/MarkdownInput.h"
#include "window/EditorTestSuite.hpp"
#include "_main/CAppMode.h"
#include "config/system_constants.h"
#include "func/CKeyBind.h"
#include "env/CDataProfile.h"
#include "env/CShareData_IO.h"
#include <limits>

namespace {
using Lines = std::vector<std::wstring_view>;

std::wstring Join(const Lines& lines)
{
	std::wstring text;
	for (size_t i = 0; i < lines.size(); ++i) {
		if (i) text += L'\n';
		text += lines[i];
	}
	return text;
}

std::wstring ApplyEnter(const Lines& lines, const Markdown::Input::EnterEdit& edit)
{
	const auto offset = [&](Markdown::SourcePosition point) {
		size_t result = point.offset;
		for (size_t y = 0; y < point.line; ++y) result += lines[y].size() + 1;
		return result;
	};
	auto text = Join(lines);
	text.replace(offset(edit.range.begin), offset(edit.range.end) - offset(edit.range.begin), edit.text);
	return text;
}

void ExpectEnter(const Lines& lines, Markdown::SourceRange selection, std::wstring_view expected,
	Markdown::SourcePosition caret, size_t tabWidth = 2)
{
	const auto edit = Markdown::Input::Enter(lines, selection, tabWidth, L"\n");
	SCOPED_TRACE(Join(lines));
	ASSERT_TRUE(edit);
	EXPECT_EQ(expected, ApplyEnter(lines, *edit));
	EXPECT_EQ(caret.line, edit->caret.line);
	EXPECT_EQ(caret.offset, edit->caret.offset);
}

void ExpectIndent(const Lines& lines, size_t first, size_t last, bool unindent, std::wstring_view expected, size_t tabWidth = 2)
{
	const auto edits = Markdown::Input::Indent(lines, first, last, tabWidth, unindent);
	SCOPED_TRACE(Join(lines));
	ASSERT_TRUE(edits);
	std::vector<std::wstring> changed;
	for (auto line : lines) changed.emplace_back(line);
	for (const auto& edit : *edits) changed[edit.line].replace(0, edit.oldLength, edit.text);
	Lines result;
	for (const auto& line : changed) result.emplace_back(line);
	EXPECT_EQ(expected, Join(result));
}
}

TEST(MarkdownInput, EnterIncrementsSourceNumberAndKeepsBullet)
{
	ExpectEnter({L"9. text"}, {{0, 7}, {0, 7}}, L"9. text\n10. ", {1, 4});
	ExpectEnter({L"003. text"}, {{0, 9}, {0, 9}}, L"003. text\n4. ", {1, 3});
	for (auto line : {L"- text", L"+ text", L"* text"}) {
		const std::wstring expected = std::wstring(line) + L"\n" + line[0] + L" ";
		ExpectEnter({line}, {{0, 6}, {0, 6}}, expected, {1, 2});
	}
}

TEST(MarkdownInput, EnterResetsTasksAndContinuesNestedQuotes)
{
	ExpectEnter({L"> - [x] text"}, {{0, 12}, {0, 12}}, L"> - [x] text\n> - [ ] ", {1, 8});
	ExpectEnter({L"> > text"}, {{0, 8}, {0, 8}}, L"> > text\n> > ", {1, 4});
	ExpectEnter({L"- text", L"  > quote"}, {{1, 9}, {1, 9}}, L"- text\n  > quote\n  > ", {2, 4});
}

TEST(MarkdownInput, EnterSplitsBodyAndDeletesSelectionBeforeComputing)
{
	ExpectEnter({L"- 日本語です"}, {{0, 4}, {0, 4}}, L"- 日本\n- 語です", {1, 2});
	ExpectEnter({L"12. abcdef"}, {{0, 5}, {0, 8}}, L"12. a\n13. ef", {1, 4});
	ExpectEnter({L"- abc", L"  def"}, {{0, 3}, {1, 3}}, L"- a\n- ef", {1, 2});
	ExpectEnter({L"- body"}, {{0, 2}, {0, 6}}, L"", {0, 0});
}

TEST(MarkdownInput, EnterOnContinuationUsesOwningItem)
{
	ExpectEnter({L"3. a", L"   continuation"}, {{1, 15}, {1, 15}}, L"3. a\n   continuation\n4. ", {2, 3});
	ExpectEnter({L"> - a", L"lazy"}, {{1, 4}, {1, 4}}, L"> - a\nlazy\n> - ", {2, 4});
	ExpectEnter({L"- parent", L"\t- child"}, {{1, 8}, {1, 8}}, L"- parent\n\t- child\n  - ", {2, 4});
}

TEST(MarkdownInput, EmptyItemsExitOneLevelWithoutAddingNewline)
{
	ExpectEnter({L"- [x] \t"}, {{0, 7}, {0, 7}}, L"", {0, 0});
	ExpectEnter({L"3. parent", L"   - "}, {{1, 5}, {1, 5}}, L"3. parent\n4. ", {1, 3});
	ExpectEnter({L"> - "}, {{0, 4}, {0, 4}}, L"> ", {0, 2});
	ExpectEnter({L"> > "}, {{0, 4}, {0, 4}}, L"> ", {0, 2});
	ExpectEnter({L"> "}, {{0, 2}, {0, 2}}, L"", {0, 0});
}

TEST(MarkdownInput, EnterFallsBackInLiteralBlocksAndPrefixes)
{
	for (auto line : {L"text", L"---", L"# heading", L"    - code", L">no-space", L"1) text"}) {
		const size_t end = std::wstring_view(line).size();
		EXPECT_FALSE(Markdown::Input::Enter({line}, {{0, end}, {0, end}}, 2, L"\n"));
	}
	EXPECT_FALSE(Markdown::Input::Enter({L"```", L"- code"}, {{1, 6}, {1, 6}}, 2, L"\n"));
	EXPECT_FALSE(Markdown::Input::Enter({L"- [x] body"}, {{0, 4}, {0, 4}}, 2, L"\n"));
	EXPECT_FALSE(Markdown::Input::Enter({L"- a", L"- b"}, {{0, 2}, {1, 3}}, 2, L"\n"));
}

TEST(MarkdownInput, EnterChecksUnsignedOverflowAndUsesRequestedEol)
{
	const auto maximum = std::to_wstring((std::numeric_limits<unsigned int>::max)()) + L". text";
	EXPECT_FALSE(Markdown::Input::Enter({maximum}, {{0, maximum.size()}, {0, maximum.size()}}, 2, L"\n"));
	const auto below = std::to_wstring((std::numeric_limits<unsigned int>::max)() - 1) + L". text";
	const auto edit = Markdown::Input::Enter({below}, {{0, below.size()}, {0, below.size()}}, 2, L"\r\n");
	ASSERT_TRUE(edit);
	EXPECT_EQ(L"\r\n" + std::to_wstring((std::numeric_limits<unsigned int>::max)()) + L". ", edit->text);
}

TEST(MarkdownInput, IndentUsesPreviousItemWidthAndMovesWholeSubtree)
{
	ExpectIndent({L"10. first", L"11. next", L"    continuation", L"    - child", L"12. last"}, 1, 1, false,
		L"10. first\n    11. next\n        continuation\n        - child\n12. last");
	ExpectIndent({L"- first", L"- next", L"  continuation", L"  - child"}, 2, 2, false,
		L"- first\n  - next\n    continuation\n    - child");
}

TEST(MarkdownInput, IndentSelectedSiblingsShareOneParent)
{
	ExpectIndent({L"- a", L"- b", L"  - child", L"- c", L"- d"}, 1, 3, false,
		L"- a\n  - b\n    - child\n  - c\n- d");
	ExpectIndent({L"- a", L"- b", L"- c"}, 0, 2, false, L"- a\n- b\n- c");
}

TEST(MarkdownInput, UnindentKeepsSubtreeRelativeColumns)
{
	ExpectIndent({L"10. parent", L"    - child", L"      continuation", L"      - grandchild", L"11. next"}, 1, 3, true,
		L"10. parent\n- child\n  continuation\n  - grandchild\n11. next");
	ExpectIndent({L"- top"}, 0, 0, true, L"- top");
	ExpectIndent({L"- - inline"}, 0, 0, true, L"- - inline");
}

TEST(MarkdownInput, IndentPreservesQuoteAndTaskPrefixes)
{
	ExpectIndent({L"> - [x] a", L"> - [ ] b", L">   > quote"}, 1, 1, false,
		L"> - [x] a\n>   - [ ] b\n>     > quote");
	ExpectIndent({L"> - a", L">   - b", L">     text"}, 1, 1, true,
		L"> - a\n> - b\n>   text");
	ExpectIndent({L"> - a"}, 0, 0, true, L"> - a");
}

TEST(MarkdownInput, IndentMaterializesLazyContinuation)
{
	ExpectIndent({L"- a", L"- b", L"lazy"}, 1, 1, false, L"- a\n  - b\n    lazy");
	ExpectIndent({L"> - a", L"> - b", L"lazy"}, 1, 1, false, L"> - a\n>   - b\n>     lazy");
}

TEST(MarkdownInput, IndentHandlesBlankLinesAndShiftedQuoteColumns)
{
	ExpectIndent({L"- a", L"", L"- b", L""}, 2, 2, false, L"- a\n\n  - b\n");
	ExpectIndent({L"> - a", L" > - b"}, 1, 1, false, L"> - a\n >   - b");
}

TEST(MarkdownInput, IndentUsesConfiguredTabsIncludingPartialTabs)
{
	ExpectIndent({L"- a", L"\t- b"}, 1, 1, true, L"- a\n- b", 2);
	ExpectIndent({L"- a", L"\t- b"}, 1, 1, true, L"- a\n- b", 4);
	ExpectIndent({L"- a", L"- b", L"\ttext"}, 1, 1, false, L"- a\n  - b\n      text", 4);
}

TEST(MarkdownInput, IndentCarriesCodeButCodeCaretFallsBack)
{
	const Lines lines{L"- a", L"- b", L"  ```", L"  - literal", L"  ```"};
	ExpectIndent(lines, 1, 4, false, L"- a\n  - b\n    ```\n    - literal\n    ```");
	EXPECT_FALSE(Markdown::Input::Indent(lines, 3, 3, 2, false));
	EXPECT_FALSE(Markdown::Input::Indent({L"plain"}, 0, 0, 2, false));
	EXPECT_FALSE(Markdown::Input::Indent({L"> quote"}, 0, 0, 2, false));
	EXPECT_FALSE(Markdown::Input::Indent({L"- a", L"", L"plain"}, 0, 2, 2, false));
}

TEST(MarkdownInput, InvalidInputDoesNotProduceEdits)
{
	EXPECT_FALSE(Markdown::Input::Enter({}, {{0, 0}, {0, 0}}, 2, L"\n"));
	EXPECT_FALSE(Markdown::Input::Enter({L"- a"}, {{0, 3}, {0, 3}}, 0, L"\n"));
	EXPECT_FALSE(Markdown::Input::Enter({L"- a"}, {{0, 4}, {0, 4}}, 2, L"\n"));
	EXPECT_FALSE(Markdown::Input::Indent({}, 0, 0, 2, false));
	EXPECT_FALSE(Markdown::Input::Indent({L"- a"}, 0, 0, 0, false));
}

class MarkdownInputEditor : public testing::Test, public window::EditorTestSuite {
protected:
	static void SetUpTestSuite() { SetUpEditor(); }
	static void TearDownTestSuite() { TearDownEditor(); }
	void SetUp() override
	{
		User32::setInstance<MockUser32>();
		CAppMode::getInstance()->SetViewMode(false);
		auto& view = pcEditWnd->GetActiveView();
		view.GetSelectionInfo().SetBoxSelect(false);
		Command(F_SELECTALL);
		Command(F_DELETE);
		view.GetTextArea().TextArea_OnSize(CMySize(800, 600), 0, 0);
		pcEditDoc->m_cDocEditor.SetNewLineCode(EEolType::cr_and_lf);
	}
	void TearDown() override
	{
		CAppMode::getInstance()->SetViewMode(false);
		pcEditWnd->GetActiveView().GetSelectionInfo().SetBoxSelect(false);
		pcEditDoc->m_cDocEditor.m_bIsDocModified = false;
		User32::resetInstance();
		MSG message{};
		while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {}
	}
	void Command(EFunctionCode code) { pcEditWnd->GetActiveView().GetCommander().HandleCommand(code, true, 0, 0, 0, 0); }
	void Insert(std::wstring_view text)
	{
		pcEditWnd->GetActiveView().GetCommander().HandleCommand(F_INSTEXT_W, true, reinterpret_cast<LPARAM>(text.data()), text.size(), TRUE, 0);
	}
	std::wstring Text()
	{
		std::wstring text;
		for (CLogicInt y(0); y < pcEditDoc->m_cDocLineMgr.GetLineCount(); ++y) {
			const auto* line = pcEditDoc->m_cDocLineMgr.GetLine(y);
			text.append(line->GetPtr(), static_cast<size_t>(line->GetLengthWithEOL()));
		}
		return text;
	}
	void MoveTo(CLogicPoint point)
	{
		CLayoutPoint layout;
		pcEditDoc->m_cLayoutMgr.LogicToLayout(point, &layout);
		pcEditWnd->GetActiveView().GetCaret().MoveCursor(layout, false);
	}
	void Select(CLogicPoint anchor, CLogicPoint caret)
	{
		MoveTo(anchor);
		auto& view = pcEditWnd->GetActiveView();
		view.GetSelectionInfo().BeginSelectArea();
		MoveTo(caret);
		view.GetSelectionInfo().ChangeSelectAreaByCurrentCursor(view.GetCaret().GetCaretLayoutPos());
	}
};

TEST_F(MarkdownInputEditor, EnterSplitIsOneUndoAndRestoresCaretOnRedo)
{
	Insert(L"9. abcdef");
	MoveTo({6, 0});
	Command(F_MARKDOWN_ENTER);
	EXPECT_EQ(L"9. abc\r\n10. def", Text());
	EXPECT_EQ(CLogicPoint(4, 1), pcEditWnd->GetActiveView().GetCaret().GetCaretLogicPos());
	Command(F_UNDO);
	EXPECT_EQ(L"9. abcdef", Text());
	Command(F_REDO);
	EXPECT_EQ(L"9. abc\r\n10. def", Text());
	EXPECT_EQ(CLogicPoint(4, 1), pcEditWnd->GetActiveView().GetCaret().GetCaretLogicPos());
}

TEST_F(MarkdownInputEditor, EnterSelectionAndEmptyExitAreAtomic)
{
	Insert(L"> - [x] body");
	Select({12, 0}, {8, 0});
	Command(F_MARKDOWN_ENTER);
	EXPECT_EQ(L"> ", Text());
	Command(F_UNDO);
	EXPECT_EQ(L"> - [x] body", Text());
	Command(F_REDO);
	EXPECT_EQ(L"> ", Text());
	Command(F_MARKDOWN_ENTER);
	EXPECT_EQ(L"", Text());
}

TEST_F(MarkdownInputEditor, IndentAndUnindentKeepCaretAndUseOneUndo)
{
	const std::wstring original = L"- a\r\n- b\r\n  - child\r\n- c";
	Insert(original);
	MoveTo({3, 1});
	Command(F_MARKDOWN_INDENT);
	EXPECT_EQ(L"- a\r\n  - b\r\n    - child\r\n- c", Text());
	EXPECT_EQ(CLogicPoint(5, 1), pcEditWnd->GetActiveView().GetCaret().GetCaretLogicPos());
	Command(F_UNDO);
	EXPECT_EQ(original, Text());
	Command(F_REDO);
	Command(F_MARKDOWN_UNINDENT);
	EXPECT_EQ(original, Text());
	EXPECT_EQ(CLogicPoint(3, 1), pcEditWnd->GetActiveView().GetCaret().GetCaretLogicPos());
}

TEST_F(MarkdownInputEditor, ReverseSelectionExcludesNextLineAndSurvivesIndent)
{
	Insert(L"- a\r\n- b\r\n- c\r\n- d");
	Select({0, 3}, {0, 1});
	Command(F_MARKDOWN_INDENT);
	EXPECT_EQ(L"- a\r\n  - b\r\n  - c\r\n- d", Text());
	auto& view = pcEditWnd->GetActiveView();
	EXPECT_TRUE(view.GetSelectionInfo().IsTextSelected());
	EXPECT_EQ(CLogicPoint(0, 1), view.GetCaret().GetCaretLogicPos());
	CLogicRange selected;
	pcEditDoc->m_cLayoutMgr.LayoutToLogic(view.GetSelectionInfo().m_sSelect, &selected);
	EXPECT_EQ(CLogicPoint(0, 3), selected.GetTo());
	Command(F_MARKDOWN_UNINDENT);
	EXPECT_EQ(L"- a\r\n- b\r\n- c\r\n- d", Text());
}

TEST_F(MarkdownInputEditor, IndentPreservesMixedLineEndings)
{
	const std::wstring original = L"- a\r\n- b\n  continuation\r- c";
	Insert(original);
	MoveTo({3, 1});
	Command(F_MARKDOWN_INDENT);
	EXPECT_EQ(L"- a\r\n  - b\n    continuation\r- c", Text());
	Command(F_UNDO);
	EXPECT_EQ(original, Text());
}

TEST_F(MarkdownInputEditor, EnterOnWrappedRowSplitsLogicalItem)
{
	const auto width = pcEditDoc->m_cLayoutMgr.GetMaxLineKetas();
	pcEditDoc->m_cLayoutMgr.ChangeLayoutParam(pcEditDoc->m_cLayoutMgr.GetTabSpaceKetas(), 0, 10);
	Insert(L"- abcdefghijklmnopqrstuvwxyz");
	MoveTo({18, 0});
	Command(F_MARKDOWN_ENTER);
	EXPECT_EQ(L"- abcdefghijklmnop\r\n- qrstuvwxyz", Text());
	EXPECT_EQ(CLogicPoint(2, 1), pcEditWnd->GetActiveView().GetCaret().GetCaretLogicPos());
	Command(F_UNDO);
	EXPECT_EQ(L"- abcdefghijklmnopqrstuvwxyz", Text());
	pcEditDoc->m_cLayoutMgr.ChangeLayoutParam(pcEditDoc->m_cLayoutMgr.GetTabSpaceKetas(), 0, width);
}

TEST_F(MarkdownInputEditor, EnterUsesLfAndHandlesVirtualEof)
{
	pcEditDoc->m_cDocEditor.SetNewLineCode(EEolType::line_feed);
	Insert(L"- a");
	Command(F_MARKDOWN_ENTER);
	EXPECT_EQ(L"- a\n- ", Text());
	Command(F_MARKDOWN_ENTER);
	EXPECT_EQ(L"- a\n", Text());
	Command(F_MARKDOWN_ENTER);
	EXPECT_EQ(L"- a\n\n", Text());
}

TEST_F(MarkdownInputEditor, ReadOnlyAndNoParentLeaveTextUntouched)
{
	Insert(L"- a");
	Command(F_MARKDOWN_INDENT);
	EXPECT_EQ(L"- a", Text());
	Command(F_MARKDOWN_UNINDENT);
	EXPECT_EQ(L"- a", Text());
	CAppMode::getInstance()->SetViewMode(true);
	Command(F_MARKDOWN_ENTER);
	Command(F_MARKDOWN_INDENT);
	Command(F_MARKDOWN_UNINDENT);
	EXPECT_EQ(L"- a", Text());
}

TEST_F(MarkdownInputEditor, CodeEnterUsesOrdinaryNewline)
{
	Insert(L"```\r\n- code\r\n```");
	MoveTo({6, 1});
	Command(F_MARKDOWN_ENTER);
	EXPECT_EQ(L"```\r\n- code\r\n\r\n```", Text());
	Command(F_UNDO);
	EXPECT_EQ(L"```\r\n- code\r\n```", Text());
}

TEST_F(MarkdownInputEditor, SelectionAcrossItemsUsesOrdinaryNewline)
{
	Insert(L"- a\r\n- b");
	Select({2, 0}, {3, 1});
	Command(F_MARKDOWN_ENTER);
	EXPECT_EQ(L"- \r\n", Text());
	Command(F_UNDO);
	EXPECT_EQ(L"- a\r\n- b", Text());
}

TEST_F(MarkdownInputEditor, BoxIndentUsesExistingIndentCommand)
{
	const std::wstring original = L"- a\r\n- b";
	Insert(original);
	Select({0, 0}, {1, 1});
	auto& view = pcEditWnd->GetActiveView();
	view.GetSelectionInfo().SetBoxSelect(true);
	Command(F_MARKDOWN_INDENT);
	const auto markdownResult = Text();
	Command(F_UNDO);
	EXPECT_EQ(original, Text());
	view.GetSelectionInfo().SetBoxSelect(false);
	view.GetSelectionInfo().DisableSelectArea(false);
	Select({0, 0}, {1, 1});
	view.GetSelectionInfo().SetBoxSelect(true);
	Command(F_INDENT_TAB);
	EXPECT_EQ(markdownResult, Text());
}

TEST_F(MarkdownInputEditor, NewTypeDefaultsAllowCustomBindingsAndExplicitUnassignment)
{
	auto type = std::make_unique<STypeConfig>();
	CType_Markdown markdown;
	markdown.InitTypeConfig(17, *type);
	EXPECT_EQ(F_MARKDOWN_ENTER, type->m_typeKeyBindings[VK_RETURN].m_nFuncCodeArr[0]);
	EXPECT_EQ(F_MARKDOWN_INDENT, type->m_typeKeyBindings[VK_TAB].m_nFuncCodeArr[0]);
	EXPECT_EQ(F_MARKDOWN_UNINDENT, type->m_typeKeyBindings[VK_TAB].m_nFuncCodeArr[_SHIFT]);
	KEYDATA common{};
	common.m_nKeyCode = VK_TAB;
	common.m_nFuncCodeArr[0] = F_INDENT_TAB;
	auto& binding = type->m_typeKeyBindings[VK_TAB];
	EXPECT_EQ(F_MARKDOWN_INDENT, CKeyBind::GetFuncCodeAt(common, 0, TRUE, &binding));
	binding.m_nFuncCodeArr[0] = F_COPY;
	EXPECT_EQ(F_COPY, CKeyBind::GetFuncCodeAt(common, 0, TRUE, &binding));
	binding.m_nFuncCodeArr[0] = F_0;
	EXPECT_EQ(F_0, CKeyBind::GetFuncCodeAt(common, 0, TRUE, &binding));
	binding.m_bOverride[0] = false;
	EXPECT_EQ(F_INDENT_TAB, CKeyBind::GetFuncCodeAt(common, 0, TRUE, &binding));
}

namespace {
struct MarkdownSettingsIO : CShareData_IO {
	using CShareData_IO::ShareData_IO_Types;
};
}

class MarkdownInputMigration : public testing::Test, public env::ShareDataTestSuite {
protected:
	CDataProfile profile;
	void SetUp() override { SetUpShareData(); }
	void TearDown() override { TearDownShareData(); }
	STypeConfig& Type(int index) { return *pcShareData->GetTypeSettings()[index]; }
	void Configure(int index, const wchar_t* name, const wchar_t* extensions)
	{
		auto& type = Type(index);
		CType_Basis().InitTypeConfig(index, type);
		wcscpy(type.m_szTypeName, name);
		wcscpy(type.m_szTypeExts, extensions);
	}
	void Save()
	{
		profile.SetWritingMode();
		MarkdownSettingsIO::ShareData_IO_Types(profile);
	}
	void SaveAsOldSettings()
	{
		Save();
		profile.SetProfileData(L"Other", L"MarkdownInputBindingsInitialized", L"0");
	}
	void Load()
	{
		profile.SetReadingMode();
		MarkdownSettingsIO::ShareData_IO_Types(profile);
	}
};

TEST_F(MarkdownInputMigration, ExistingMarkdownNamesAndExtensionsReceiveInputBindings)
{
	Configure(1, L"mArKdOwN", L"custom");
	Configure(2, L"Notes", L"md");
	Configure(3, L"Notes2", L"markdown");
	Configure(4, L"Notes3", L"mdown");
	Configure(5, L"Plain", L"txt");
	Type(2).m_nTabSpace = 2;
	SaveAsOldSettings();
	Load();
	for (int index = 1; index <= 4; ++index) {
		const auto& keys = Type(index).m_typeKeyBindings;
		EXPECT_TRUE(keys[VK_RETURN].m_bOverride[0]);
		EXPECT_EQ(F_MARKDOWN_ENTER, keys[VK_RETURN].m_nFuncCodeArr[0]);
		EXPECT_TRUE(keys[VK_TAB].m_bOverride[0]);
		EXPECT_EQ(F_MARKDOWN_INDENT, keys[VK_TAB].m_nFuncCodeArr[0]);
		EXPECT_TRUE(keys[VK_TAB].m_bOverride[_SHIFT]);
		EXPECT_EQ(F_MARKDOWN_UNINDENT, keys[VK_TAB].m_nFuncCodeArr[_SHIFT]);
		EXPECT_FALSE(keys[VK_RETURN].m_bOverride[_CTRL]);
	}
	EXPECT_FALSE(Type(5).m_typeKeyBindings[VK_RETURN].m_bOverride[0]);
	EXPECT_FALSE(Type(5).m_typeKeyBindings[VK_TAB].m_bOverride[0]);
	EXPECT_STREQ(L"Notes", Type(2).m_szTypeName);
	EXPECT_STREQ(L"md", Type(2).m_szTypeExts);
	EXPECT_EQ(2, Type(2).m_nTabSpace);
}

TEST_F(MarkdownInputMigration, ExistingTypeOverridesAndExplicitUnassignmentArePreserved)
{
	Configure(1, L"Markdown", L"md");
	auto& keys = Type(1).m_typeKeyBindings;
	keys[VK_RETURN].m_bOverride[0] = true;
	keys[VK_RETURN].m_nFuncCodeArr[0] = F_COPY;
	keys[VK_TAB].m_bOverride[0] = true;
	keys[VK_TAB].m_nFuncCodeArr[0] = F_0;
	SaveAsOldSettings();
	Load();
	EXPECT_TRUE(Type(1).m_typeKeyBindings[VK_RETURN].m_bOverride[0]);
	EXPECT_EQ(F_COPY, Type(1).m_typeKeyBindings[VK_RETURN].m_nFuncCodeArr[0]);
	EXPECT_TRUE(Type(1).m_typeKeyBindings[VK_TAB].m_bOverride[0]);
	EXPECT_EQ(F_0, Type(1).m_typeKeyBindings[VK_TAB].m_nFuncCodeArr[0]);
	EXPECT_EQ(F_MARKDOWN_UNINDENT, Type(1).m_typeKeyBindings[VK_TAB].m_nFuncCodeArr[_SHIFT]);
}

TEST_F(MarkdownInputMigration, CustomizedCommonKeysRemainInherited)
{
	Configure(1, L"Notes", L"md");
	auto& common = GetDllShareData().m_Common.m_sKeyBind;
	for (int i = 0; i < common.m_nKeyNameArrNum; ++i) {
		auto& key = common.m_pKeyNameArr[i];
		if (key.m_nKeyCode == VK_RETURN) key.m_nFuncCodeArr[0] = F_COPY;
		if (key.m_nKeyCode == VK_TAB) key.m_nFuncCodeArr[0] = F_0;
	}
	SaveAsOldSettings();
	Load();
	EXPECT_FALSE(Type(1).m_typeKeyBindings[VK_RETURN].m_bOverride[0]);
	EXPECT_FALSE(Type(1).m_typeKeyBindings[VK_TAB].m_bOverride[0]);
	EXPECT_EQ(F_MARKDOWN_UNINDENT, Type(1).m_typeKeyBindings[VK_TAB].m_nFuncCodeArr[_SHIFT]);
}

TEST_F(MarkdownInputMigration, SavedMigrationDoesNotReapplyAfterReturningToCommonSettings)
{
	Configure(1, L"Markdown", L"md");
	SaveAsOldSettings();
	Load();
	ASSERT_TRUE(Type(1).m_typeKeyBindings[VK_RETURN].m_bOverride[0]);
	Type(1).m_typeKeyBindings[VK_RETURN].m_bOverride[0] = false;
	Type(1).m_typeKeyBindings[VK_TAB].m_nFuncCodeArr[0] = F_0;
	Type(1).m_typeKeyBindings[VK_TAB].m_nFuncCodeArr[_SHIFT] = F_COPY;
	Save();
	Load();
	bool initialized = false;
	ASSERT_TRUE(profile.IOProfileData(L"Other", L"MarkdownInputBindingsInitialized", initialized));
	EXPECT_TRUE(initialized);
	EXPECT_FALSE(Type(1).m_typeKeyBindings[VK_RETURN].m_bOverride[0]);
	EXPECT_TRUE(Type(1).m_typeKeyBindings[VK_TAB].m_bOverride[0]);
	EXPECT_EQ(F_0, Type(1).m_typeKeyBindings[VK_TAB].m_nFuncCodeArr[0]);
	EXPECT_EQ(F_COPY, Type(1).m_typeKeyBindings[VK_TAB].m_nFuncCodeArr[_SHIFT]);
}
