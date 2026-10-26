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
#include "outline/CFuncInfo.h"
#include "outline/CFuncInfoArr.h"
#include "typeprop/CPropTypes.h"
#include "sakura_rc.h"
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
		auto& type = pcEditDoc->m_cDocType.GetDocumentAttributeWrite();
		type.m_eSmartIndent = SMARTINDENT_MARKDOWN;
		type.m_bInsSpace = false;
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
	void Enter() { pcEditWnd->GetActiveView().GetCommander().HandleCommand(F_WCHAR, true, L'\r', 0, 0, 0); }
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
	Enter();
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
	Enter();
	EXPECT_EQ(L"> ", Text());
	Command(F_UNDO);
	EXPECT_EQ(L"> - [x] body", Text());
	Command(F_REDO);
	EXPECT_EQ(L"> ", Text());
	Enter();
	EXPECT_EQ(L"", Text());
}

TEST_F(MarkdownInputEditor, IndentAndUnindentKeepCaretAndUseOneUndo)
{
	const std::wstring original = L"- a\r\n- b\r\n  - child\r\n- c";
	Insert(original);
	MoveTo({3, 1});
	Command(F_INDENT_TAB);
	EXPECT_EQ(L"- a\r\n  - b\r\n    - child\r\n- c", Text());
	EXPECT_EQ(CLogicPoint(5, 1), pcEditWnd->GetActiveView().GetCaret().GetCaretLogicPos());
	Command(F_UNDO);
	EXPECT_EQ(original, Text());
	Command(F_REDO);
	Command(F_UNINDENT_TAB);
	EXPECT_EQ(original, Text());
	EXPECT_EQ(CLogicPoint(3, 1), pcEditWnd->GetActiveView().GetCaret().GetCaretLogicPos());
}

TEST_F(MarkdownInputEditor, ReverseSelectionExcludesNextLineAndSurvivesIndent)
{
	Insert(L"- a\r\n- b\r\n- c\r\n- d");
	Select({0, 3}, {0, 1});
	Command(F_INDENT_TAB);
	EXPECT_EQ(L"- a\r\n  - b\r\n  - c\r\n- d", Text());
	auto& view = pcEditWnd->GetActiveView();
	EXPECT_TRUE(view.GetSelectionInfo().IsTextSelected());
	EXPECT_EQ(CLogicPoint(0, 1), view.GetCaret().GetCaretLogicPos());
	CLogicRange selected;
	pcEditDoc->m_cLayoutMgr.LayoutToLogic(view.GetSelectionInfo().m_sSelect, &selected);
	EXPECT_EQ(CLogicPoint(0, 3), selected.GetTo());
	Command(F_UNINDENT_TAB);
	EXPECT_EQ(L"- a\r\n- b\r\n- c\r\n- d", Text());
}

TEST_F(MarkdownInputEditor, IndentPreservesMixedLineEndings)
{
	const std::wstring original = L"- a\r\n- b\n  continuation\r- c";
	Insert(original);
	MoveTo({3, 1});
	Command(F_INDENT_TAB);
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
	Enter();
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
	Enter();
	EXPECT_EQ(L"- a\n- ", Text());
	Enter();
	EXPECT_EQ(L"- a\n", Text());
	Enter();
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
	Enter();
	Command(F_INDENT_TAB);
	Command(F_UNINDENT_TAB);
	EXPECT_EQ(L"- a", Text());
}

TEST_F(MarkdownInputEditor, CodeEnterUsesOrdinaryNewline)
{
	Insert(L"```\r\n- code\r\n```");
	MoveTo({6, 1});
	Enter();
	EXPECT_EQ(L"```\r\n- code\r\n\r\n```", Text());
	Command(F_UNDO);
	EXPECT_EQ(L"```\r\n- code\r\n```", Text());
}

TEST_F(MarkdownInputEditor, SelectionAcrossItemsUsesOrdinaryNewline)
{
	Insert(L"- a\r\n- b");
	Select({2, 0}, {3, 1});
	Enter();
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
	Command(F_INDENT_TAB);
	const auto markdownResult = Text();
	Command(F_UNDO);
	EXPECT_EQ(original, Text());
	view.GetSelectionInfo().SetBoxSelect(false);
	view.GetSelectionInfo().DisableSelectArea(false);
	Select({0, 0}, {1, 1});
	view.GetSelectionInfo().SetBoxSelect(true);
	pcEditDoc->m_cDocType.GetDocumentAttributeWrite().m_eSmartIndent = SMARTINDENT_NONE;
	Command(F_INDENT_TAB);
	EXPECT_EQ(markdownResult, Text());
}

TEST_F(MarkdownInputEditor, NewTypeDefaultsAllowCustomBindingsAndExplicitUnassignment)
{
	auto type = std::make_unique<STypeConfig>();
	CType_Markdown markdown;
	markdown.InitTypeConfig(17, *type);
	EXPECT_EQ(SMARTINDENT_MARKDOWN, type->m_eSmartIndent);
	EXPECT_EQ(OUTLINE_MARKDOWN, type->m_eDefaultOutline);
	EXPECT_FALSE(type->m_typeKeyBindings[VK_RETURN].m_bOverride[0]);
	EXPECT_FALSE(type->m_typeKeyBindings[VK_TAB].m_bOverride[0]);
	EXPECT_FALSE(type->m_typeKeyBindings[VK_TAB].m_bOverride[_SHIFT]);
	KEYDATA common{};
	common.m_nKeyCode = VK_TAB;
	common.m_nFuncCodeArr[0] = F_INDENT_TAB;
	auto& binding = type->m_typeKeyBindings[VK_TAB];
	EXPECT_EQ(F_INDENT_TAB, CKeyBind::GetFuncCodeAt(common, 0, TRUE, &binding));
	binding.m_bOverride[0] = true;
	binding.m_nFuncCodeArr[0] = F_COPY;
	EXPECT_EQ(F_COPY, CKeyBind::GetFuncCodeAt(common, 0, TRUE, &binding));
	binding.m_nFuncCodeArr[0] = F_0;
	EXPECT_EQ(F_0, CKeyBind::GetFuncCodeAt(common, 0, TRUE, &binding));
	binding.m_bOverride[0] = false;
	EXPECT_EQ(F_INDENT_TAB, CKeyBind::GetFuncCodeAt(common, 0, TRUE, &binding));
	CType_Basis().InitTypeConfig(0, *type);
	EXPECT_EQ(SMARTINDENT_NONE, type->m_eSmartIndent);
	CType_Cpp().InitTypeConfig(1, *type);
	EXPECT_EQ(SMARTINDENT_CPP, type->m_eSmartIndent);
}

TEST_F(MarkdownInputEditor, OrdinaryEnterUsesSettingInsteadOfNameOrExtension)
{
	auto& type = pcEditDoc->m_cDocType.GetDocumentAttributeWrite();
	wcscpy(type.m_szTypeName, L"Markdown");
	wcscpy(type.m_szTypeExts, L"md,markdown,mdown");
	type.m_eSmartIndent = SMARTINDENT_NONE;
	Insert(L"- a");
	Enter();
	EXPECT_EQ(L"- a\r\n", Text());
	Command(F_UNDO);
	wcscpy(type.m_szTypeName, L"Notes");
	wcscpy(type.m_szTypeExts, L"txt");
	type.m_eSmartIndent = SMARTINDENT_MARKDOWN;
	Enter();
	EXPECT_EQ(L"- a\r\n- ", Text());
	Command(F_UNDO);
	EXPECT_EQ(L"- a", Text());
	Command(F_REDO);
	EXPECT_EQ(L"- a\r\n- ", Text());
}

TEST_F(MarkdownInputEditor, OrdinaryTabCommandsUseSettingAndPreserveUndo)
{
	auto& type = pcEditDoc->m_cDocType.GetDocumentAttributeWrite();
	const std::wstring original = L"- a\r\n- b";
	Insert(original);
	type.m_eSmartIndent = SMARTINDENT_NONE;
	Select({0, 1}, {3, 1});
	Command(F_INDENT_TAB);
	// 通常のTAB入力は、1行内の選択範囲を置換する。
	EXPECT_EQ(L"- a\r\n\t", Text());
	Command(F_UNDO);
	EXPECT_EQ(original, Text());
	pcEditWnd->GetActiveView().GetSelectionInfo().DisableSelectArea(false);
	MoveTo({3, 1});
	type.m_eSmartIndent = SMARTINDENT_MARKDOWN;
	Command(F_INDENT_TAB);
	EXPECT_EQ(L"- a\r\n  - b", Text());
	Command(F_UNINDENT_TAB);
	EXPECT_EQ(original, Text());
	Command(F_UNDO);
	EXPECT_EQ(L"- a\r\n  - b", Text());
	Command(F_REDO);
	EXPECT_EQ(original, Text());
}

TEST_F(MarkdownInputEditor, LegacyCommandsAlsoRespectDisabledSetting)
{
	pcEditDoc->m_cDocType.GetDocumentAttributeWrite().m_eSmartIndent = SMARTINDENT_NONE;
	Insert(L"- a\r\n- b");
	Command(F_MARKDOWN_ENTER);
	EXPECT_EQ(L"- a\r\n- b\r\n", Text());
	Command(F_UNDO);
	Select({0, 0}, {3, 1});
	Command(F_MARKDOWN_INDENT);
	EXPECT_EQ(L"\t- a\r\n\t- b", Text());
	Command(F_MARKDOWN_UNINDENT);
	EXPECT_EQ(L"- a\r\n- b", Text());
}

TEST_F(MarkdownInputEditor, LiteralNewlineInsertionDoesNotRunSmartIndent)
{
	Insert(L"- a");
	pcEditWnd->GetActiveView().GetCommander().HandleCommand(F_CTRL_CODE, true, L'\r', 0, 0, 0);
	EXPECT_EQ(L"- a\r", Text());
}

using MarkdownOutline = MarkdownInputEditor;

TEST_F(MarkdownOutline, HeadingsBuildHierarchyAndSkipCodeBlocks)
{
	pcEditDoc->m_cDocType.GetDocumentAttributeWrite().m_eSmartIndent = SMARTINDENT_NONE;
	Insert(L"# Root\r\n### Child ###\r\n## Sibling\r\n```\r\n# hidden\r\n```\r\n\r\n    # code\r\n# Next\r\n####### plain");
	CFuncInfoArr result;
	pcEditDoc->m_cDocOutline.MakeTopicList_markdown(&result);
	ASSERT_EQ(4, result.GetNum());
	const wchar_t* titles[] = {L"Root", L"Child", L"Sibling", L"Next"};
	const int depths[] = {0, 1, 1, 0};
	const int lines[] = {1, 2, 3, 9};
	for (int i = 0; i < result.GetNum(); ++i) {
		const auto* item = result.GetAt(i);
		EXPECT_STREQ(titles[i], item->m_cmemFuncName.GetStringPtr());
		EXPECT_EQ(depths[i], item->m_nDepth);
		EXPECT_EQ(CLogicInt(lines[i]), item->m_nFuncLineCRLF);
	}
}

TEST_F(MarkdownOutline, SetextUsesWholeParagraphAndJumpsToItsFirstLine)
{
	Insert(L"Top\r\nline\r\n===\r\n\r\n> Child\r\n> -----\r\n\r\n- body\r\n  - \r\n\r\n# Last");
	CFuncInfoArr result;
	pcEditDoc->m_cDocOutline.MakeTopicList_markdown(&result);
	ASSERT_EQ(3, result.GetNum());
	EXPECT_STREQ(L"Top line", result.GetAt(0)->m_cmemFuncName.GetStringPtr());
	EXPECT_EQ(CLogicInt(1), result.GetAt(0)->m_nFuncLineCRLF);
	EXPECT_STREQ(L"Child", result.GetAt(1)->m_cmemFuncName.GetStringPtr());
	EXPECT_EQ(CLogicInt(5), result.GetAt(1)->m_nFuncLineCRLF);
	EXPECT_EQ(CLogicInt(3), result.GetAt(1)->m_nFuncColCRLF);
	EXPECT_EQ(1, result.GetAt(1)->m_nDepth);
	EXPECT_STREQ(L"Last", result.GetAt(2)->m_cmemFuncName.GetStringPtr());
}

TEST_F(MarkdownOutline, ContainerHeadingsRespectTabWidthAndLayoutPositions)
{
	const auto width = pcEditDoc->m_cLayoutMgr.GetMaxLineKetas();
	const auto tabWidth = pcEditDoc->m_cLayoutMgr.GetTabSpaceKetas();
	pcEditDoc->m_cLayoutMgr.ChangeLayoutParam(2, 0, 10);
	Insert(L"# abcdefghijklmnopqrstuvwxyz\r\n> ## Quote\r\n- item\r\n\t### Nested\r\n\t```\r\n\t# hidden\r\n\t```");
	CFuncInfoArr result;
	pcEditDoc->m_cDocOutline.MakeTopicList_markdown(&result);
	ASSERT_EQ(3, result.GetNum());
	EXPECT_STREQ(L"Quote", result.GetAt(1)->m_cmemFuncName.GetStringPtr());
	EXPECT_STREQ(L"Nested", result.GetAt(2)->m_cmemFuncName.GetStringPtr());
	EXPECT_EQ(CLogicInt(4), result.GetAt(2)->m_nFuncLineCRLF);
	EXPECT_EQ(CLogicInt(2), result.GetAt(2)->m_nFuncColCRLF);
	CLayoutPoint target;
	pcEditDoc->m_cLayoutMgr.LogicToLayout(CLogicPoint(1, 3), &target);
	EXPECT_EQ(target.GetY2() + 1, result.GetAt(2)->m_nFuncLineLAYOUT);
	EXPECT_EQ(target.GetX2() + 1, result.GetAt(2)->m_nFuncColLAYOUT);
	pcEditDoc->m_cLayoutMgr.ChangeLayoutParam(tabWidth, 0, width);
}

class MarkdownFigures : public MarkdownInputEditor {
protected:
	EOutlineType oldOutline;
	int oldWidth, oldTab;
	CommonSetting_OutLine oldCommon;
	void SetUp() override
	{
		MarkdownInputEditor::SetUp();
		oldOutline = pcEditDoc->m_cDocType.GetDocumentAttribute().m_eDefaultOutline;
		pcEditDoc->m_cDocType.GetDocumentAttributeWrite().m_eDefaultOutline = OUTLINE_MARKDOWN;
		oldWidth = pcEditDoc->m_cLayoutMgr.GetMaxLineKetas();
		oldTab = pcEditDoc->m_cLayoutMgr.GetTabSpaceKetas();
		oldCommon = GetDllShareData().m_Common.m_sOutline;
		GetDllShareData().m_Common.m_sOutline.m_nOutlineDockSet = 0;
		GetDllShareData().m_Common.m_sOutline.m_eOutlineDockSide = DOCKSIDE_FLOAT;
		GetDllShareData().m_Common.m_sOutline.m_bAutoCloseDlgFuncList = false;
	}
	void TearDown() override
	{
		if (pcEditWnd->m_cDlgFuncList.GetHwnd()) ::DestroyWindow(pcEditWnd->m_cDlgFuncList.GetHwnd());
		pcEditWnd->SetNoteView(false);
		pcEditDoc->m_cDocType.GetDocumentAttributeWrite().m_eDefaultOutline = oldOutline;
		pcEditDoc->m_cLayoutMgr.ChangeLayoutParam(oldTab, 0, oldWidth);
		GetDllShareData().m_Common.m_sOutline = oldCommon;
		MarkdownInputEditor::TearDown();
	}
	void Open()
	{
		ASSERT_TRUE(pcEditWnd->GetActiveView().GetCommander().Command_FUNCLIST(SHOW_RELOAD, OUTLINE_DEFAULT));
		ASSERT_NE(nullptr, pcEditWnd->m_cDlgFuncList.GetHwnd());
	}
	HWND Tree() { return ::GetDlgItem(pcEditWnd->m_cDlgFuncList.GetHwnd(), IDC_TREE_FL); }
	std::wstring Label(HTREEITEM item)
	{
		wchar_t text[1024]{};
		TVITEM info{};
		info.mask = TVIF_TEXT;
		info.hItem = item;
		info.pszText = text;
		info.cchTextMax = static_cast<int>(std::size(text));
		TreeView_GetItem(Tree(), &info);
		return text;
	}
	void WaitForUpdate()
	{
		const auto end = ::GetTickCount64() + 450;
		while (::GetTickCount64() < end) {
			MSG message{};
			while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
				::TranslateMessage(&message);
				::DispatchMessageW(&message);
			}
			::Sleep(5);
		}
	}
};

TEST_F(MarkdownFigures, FiguresUseDeepestHeadingAndKeepDuplicatesInDocumentOrder)
{
	Insert(L"![](C:/before.png)\r\n# aaa\r\n![one](C:/same.png)\r\n![two](C:/same.png)\r\n"
		L"## bbb\r\n![three](C:/same.png)\r\n# ccc\r\n![four](C:/same.png)");
	const auto text = Text();
	const auto modified = pcEditDoc->m_cDocEditor.IsModified();
	CFuncInfoArr result;
	pcEditDoc->m_cDocOutline.MakeTopicList_markdown(&result);
	ASSERT_EQ(8, result.GetNum());
	const int depths[] = {0, 0, 1, 1, 1, 2, 0, 1};
	const wchar_t* names[] = {L"図1: before", L"aaa", L"図2: one", L"図3: two", L"bbb", L"図4: three", L"ccc", L"図5: four"};
	for (int i = 0; i < 8; ++i) {
		EXPECT_EQ(depths[i], result.GetAt(i)->m_nDepth);
		EXPECT_STREQ(names[i], result.GetAt(i)->m_cmemFuncName.GetStringPtr());
		EXPECT_EQ(CLogicInt(i + 1), result.GetAt(i)->m_nFuncLineCRLF);
	}
	EXPECT_EQ(text, Text());
	EXPECT_EQ(modified, pcEditDoc->m_cDocEditor.IsModified());
	Command(F_UNDO);
	EXPECT_EQ(L"", Text()); // parsing did not add an undo record
}

TEST_F(MarkdownFigures, SetextParagraphContainingFigureKeepsHeadingBeforeItsChild)
{
	Insert(L"title\r\n![caption](C:/missing.png)\r\n===\r\n# next");
	CFuncInfoArr result;
	pcEditDoc->m_cDocOutline.MakeTopicList_markdown(&result);
	ASSERT_EQ(3, result.GetNum());
	EXPECT_STREQ(L"title ![caption](C:/missing.png)", result.GetAt(0)->m_cmemFuncName.GetStringPtr());
	EXPECT_EQ(CLogicInt(1), result.GetAt(0)->m_nFuncLineCRLF);
	EXPECT_EQ(1, result.GetAt(1)->m_nDepth);
	EXPECT_EQ(0, result.GetAt(2)->m_nDepth);
}

TEST_F(MarkdownFigures, OutlineAndNoteShareCodeContextAndLogicalPositions)
{
	pcEditDoc->m_cLayoutMgr.ChangeLayoutParam(4, 0, 12);
	Insert(L"# long heading abcdefghijklmnop\r\n![yes](C:/missing.png) <!-- sakura:rows=20 -->\r\n"
		L"```\r\n![no](C:/a.png)\r\n```\r\n\r\n    ![no](C:/a.png)\r\n\r\n"
		L"- ```\r\n  ![no](C:/a.png)\r\n  ```\r\n\r\n> ```\r\n> ![no](C:/a.png)\r\n> ```\r\n"
		L"![yes2](C:/a.png)\r\ninline ![no](C:/a.png)\r\n![no](https://a/a.png)");
	CFuncInfoArr off, on;
	pcEditDoc->m_cDocOutline.MakeTopicList_markdown(&off);
	pcEditWnd->SetNoteView(true);
	pcEditDoc->m_cDocOutline.MakeTopicList_markdown(&on);
	ASSERT_EQ(3, off.GetNum());
	ASSERT_EQ(off.GetNum(), on.GetNum());
	const auto& blocks = pcEditWnd->GetActiveView().GetTextArea().GetNoteProjection().Blocks();
	ASSERT_EQ(2u, blocks.size());
	EXPECT_EQ(20, blocks[0].maxRows);
	for (int i = 0; i < on.GetNum(); ++i) {
		EXPECT_STREQ(off.GetAt(i)->m_cmemFuncName.GetStringPtr(), on.GetAt(i)->m_cmemFuncName.GetStringPtr());
		CLayoutPoint expected;
		pcEditDoc->m_cLayoutMgr.LogicToLayout(CLogicPoint(on.GetAt(i)->m_nFuncColCRLF - 1,
			on.GetAt(i)->m_nFuncLineCRLF - 1), &expected);
		EXPECT_EQ(expected.y + 1, on.GetAt(i)->m_nFuncLineLAYOUT);
		if (i) EXPECT_EQ(blocks[i - 1].logicalLine + 1, static_cast<int>(on.GetAt(i)->m_nFuncLineCRLF));
	}
}

TEST_F(MarkdownFigures, NoteFenceEditsPropagateBeyondChangedLineAndUndoRedo)
{
	Insert(L"text\r\n![a](C:/a.png)\r\n![b](C:/b.png)\r\n```");
	pcEditWnd->SetNoteView(true);
	auto& view = pcEditWnd->GetActiveView();
	ASSERT_EQ(2u, view.GetTextArea().GetNoteProjection().Blocks().size());
	Select(CLogicPoint(0, 0), CLogicPoint(4, 0));
	Insert(L"```");
	EXPECT_TRUE(view.GetTextArea().GetNoteProjection().Blocks().empty());
	Command(F_UNDO);
	EXPECT_EQ(2u, view.GetTextArea().GetNoteProjection().Blocks().size());
	Command(F_REDO);
	EXPECT_TRUE(view.GetTextArea().GetNoteProjection().Blocks().empty());
	pcEditDoc->m_cDocType.GetDocumentAttributeWrite().m_eDefaultOutline = OUTLINE_TEXT;
	view.ApplyNoteView();
	EXPECT_EQ(2u, view.GetTextArea().GetNoteProjection().Blocks().size());
	pcEditDoc->m_cDocType.GetDocumentAttributeWrite().m_eDefaultOutline = OUTLINE_MARKDOWN;
	view.ApplyNoteView();
	EXPECT_TRUE(view.GetTextArea().GetNoteProjection().Blocks().empty());
}

TEST_F(MarkdownFigures, NativeTreeKeepsDocumentOrderAndDelayedSelectionAndExpansion)
{
	Insert(L"# zzz\r\n![a](C:/a.png)\r\n# aaa");
	Open();
	auto root = TreeView_GetRoot(Tree());
	ASSERT_EQ(L"zzz", Label(root));
	EXPECT_EQ(L"aaa", Label(TreeView_GetNextSibling(Tree(), root)));
	auto figure = TreeView_GetChild(Tree(), root);
	ASSERT_EQ(L"図1: a", Label(figure));
	TreeView_Expand(Tree(), root, TVE_EXPAND);
	MoveTo(CLogicPoint(0, 0));
	TreeView_SelectItem(Tree(), figure);
	Insert(L"intro\r\n");
	EXPECT_EQ(L"図1: a", Label(TreeView_GetSelection(Tree())));
	WaitForUpdate();
	EXPECT_EQ(L"図1: a", Label(TreeView_GetSelection(Tree())));
	root = TreeView_GetRoot(Tree());
	EXPECT_NE(0u, TreeView_GetItemState(Tree(), root, TVIS_EXPANDED) & TVIS_EXPANDED);
	Select(CLogicPoint(0, 2), CLogicPoint(0, 3));
	TreeView_SelectItem(Tree(), TreeView_GetChild(Tree(), root));
	Command(F_DELETE);
	EXPECT_EQ(L"図1: a", Label(TreeView_GetSelection(Tree())));
	WaitForUpdate();
	EXPECT_EQ(nullptr, TreeView_GetSelection(Tree()));
	EXPECT_EQ(2u, TreeView_GetCount(Tree()));
}

TEST_F(MarkdownFigures, NativeTreeIgnoresAllSavedSortModesAndKeepsHeadingSiblings)
{
	Insert(L"# zzz\r\n![z](C:/z.png)\r\n## aaa\r\n![b](C:/b.png)\r\n# aaa");
	const auto typeIndex = pcEditDoc->m_cDocType.GetDocumentType();
	auto type = std::make_unique<STypeConfig>();
	ASSERT_TRUE(CDocTypeManager().GetTypeConfig(typeIndex, *type));
	const auto oldSort = type->m_nOutlineSortType;
	for (int sort = SORTTYPE_DEFAULT; sort <= SORTTYPE_ZTOA; ++sort) {
		type->m_nOutlineSortType = sort;
		CDocTypeManager().SetTypeConfig(typeIndex, *type);
		Open();
		const auto root = TreeView_GetRoot(Tree());
		EXPECT_EQ(L"zzz", Label(root));
		EXPECT_EQ(L"aaa", Label(TreeView_GetNextSibling(Tree(), root)));
		const auto figure = TreeView_GetChild(Tree(), root);
		EXPECT_EQ(L"図1: z", Label(figure));
		EXPECT_EQ(L"aaa", Label(TreeView_GetNextSibling(Tree(), figure)));
		EXPECT_FALSE(::IsWindowEnabled(::GetDlgItem(pcEditWnd->m_cDlgFuncList.GetHwnd(), IDC_COMBO_nSortType)));
		::DestroyWindow(pcEditWnd->m_cDlgFuncList.GetHwnd());
	}
	type->m_nOutlineSortType = oldSort;
	CDocTypeManager().SetTypeConfig(typeIndex, *type);
}

TEST_F(MarkdownFigures, JumpBeforeTimerUsesUpdatedLogicalAnchorAndClearsDeletedTarget)
{
	Insert(L"# root\r\n![a](C:/a.png)\r\n# next");
	Open();
	MoveTo(CLogicPoint(0, 0));
	TreeView_SelectItem(Tree(), TreeView_GetChild(Tree(), TreeView_GetRoot(Tree())));
	Insert(L"intro\r\n");
	GetDllShareData().m_sWorkBuffer.m_LogicPoint = CLogicPoint(99, 99);
	::SendMessageW(pcEditWnd->m_cDlgFuncList.GetHwnd(), WM_COMMAND, IDOK, 0);
	EXPECT_EQ(CLogicPoint(0, 2), GetDllShareData().m_sWorkBuffer.m_LogicPoint);
	Select(CLogicPoint(0, 2), CLogicPoint(0, 3));
	TreeView_SelectItem(Tree(), TreeView_GetChild(Tree(), TreeView_GetRoot(Tree())));
	Command(F_DELETE);
	GetDllShareData().m_sWorkBuffer.m_LogicPoint = CLogicPoint(99, 99);
	::SendMessageW(pcEditWnd->m_cDlgFuncList.GetHwnd(), WM_COMMAND, IDOK, 0);
	EXPECT_EQ(CLogicPoint(99, 99), GetDllShareData().m_sWorkBuffer.m_LogicPoint);
	EXPECT_EQ(nullptr, TreeView_GetSelection(Tree()));
}

TEST_F(MarkdownFigures, EnterJumpScrollsToNoteBlockWithoutTogglingMode)
{
	pcEditDoc->m_cLayoutMgr.ChangeLayoutParam(4, 0, 12);
	Insert(L"# heading\r\n![a](C:/missing-a.png)\r\n![b](C:/missing-b.png)\r\ntail");
	Open();
	auto& view = pcEditWnd->GetActiveView();
	for (bool note : {false, true}) {
		pcEditWnd->SetNoteView(note);
		const auto first = TreeView_GetChild(Tree(), TreeView_GetRoot(Tree()));
		TreeView_SelectItem(Tree(), TreeView_GetNextSibling(Tree(), first));
		NMTVKEYDOWN key{};
		key.hdr.hwndFrom = Tree();
		key.hdr.code = TVN_KEYDOWN;
		key.wVKey = VK_RETURN;
		::SendMessageW(pcEditWnd->m_cDlgFuncList.GetHwnd(), WM_NOTIFY, IDC_TREE_FL, reinterpret_cast<LPARAM>(&key));
		EXPECT_EQ(CLogicPoint(0, 2), GetDllShareData().m_sWorkBuffer.m_LogicPoint);
		EXPECT_EQ(note, view.IsNoteView());
		if (note) {
			const auto& area = view.GetTextArea();
			const auto top = area.GetDisplayTop().value;
			const auto block = area.GetNoteProjection().BlockStart(1).value;
			EXPECT_GE(block, top);
			EXPECT_LT(block, top + static_cast<int>(area.m_nViewRowNum));
		}
	}
}

TEST_F(MarkdownFigures, DelayedRenameUndoRedoAndOutlineSettingSwitch)
{
	Insert(L"# heading\r\n![a](C:/a.png)");
	Open();
	Select(CLogicPoint(2, 1), CLogicPoint(3, 1));
	TreeView_SelectItem(Tree(), TreeView_GetChild(Tree(), TreeView_GetRoot(Tree())));
	Insert(L"renamed");
	EXPECT_EQ(L"図1: a", Label(TreeView_GetSelection(Tree())));
	WaitForUpdate();
	EXPECT_EQ(L"図1: renamed", Label(TreeView_GetSelection(Tree())));
	Command(F_UNDO);
	WaitForUpdate();
	EXPECT_EQ(L"図1: a", Label(TreeView_GetSelection(Tree())));
	Command(F_REDO);
	WaitForUpdate();
	EXPECT_EQ(L"図1: renamed", Label(TreeView_GetSelection(Tree())));
	pcEditDoc->m_cDocType.GetDocumentAttributeWrite().m_eDefaultOutline = OUTLINE_TEXT;
	pcEditWnd->m_cDlgFuncList.NotifyMarkdownSetting();
	WaitForUpdate();
	EXPECT_TRUE(pcEditWnd->m_cDlgFuncList.CheckListType(OUTLINE_TEXT));
	pcEditDoc->m_cDocType.GetDocumentAttributeWrite().m_eDefaultOutline = OUTLINE_MARKDOWN;
	pcEditWnd->m_cDlgFuncList.NotifyMarkdownSetting();
	WaitForUpdate();
	EXPECT_TRUE(pcEditWnd->m_cDlgFuncList.CheckListType(OUTLINE_MARKDOWN));
	EXPECT_EQ(2u, TreeView_GetCount(Tree()));
	pcEditWnd->GetActiveView().GetCommander().Command_FUNCLIST(SHOW_RELOAD, OUTLINE_BOOKMARK);
	pcEditDoc->m_cDocType.GetDocumentAttributeWrite().m_eDefaultOutline = OUTLINE_TEXT;
	pcEditWnd->m_cDlgFuncList.NotifyMarkdownSetting();
	WaitForUpdate();
	EXPECT_TRUE(pcEditWnd->m_cDlgFuncList.CheckListType(OUTLINE_BOOKMARK));
}

TEST_F(MarkdownFigures, ImeFencePreeditExcludesSuffixAndCancellationRestoresImages)
{
	Insert(L"\r\n![a](C:/a.png)\r\n```");
	pcEditWnd->SetNoteView(true);
	auto& view = pcEditWnd->GetActiveView();
	ASSERT_EQ(1u, view.GetTextArea().GetNoteProjection().Blocks().size());
	MoveTo(CLogicPoint(0, 0));
	view.BeginImeComposition();
	view.m_ime.text = L"```";
	view.m_ime.cursor = 3;
	view.RebuildImeDisplayLayout();
	EXPECT_TRUE(view.GetTextArea().GetNoteProjection().Blocks().empty());
	view.EndImeComposition();
	EXPECT_EQ(1u, view.GetTextArea().GetNoteProjection().Blocks().size());
	EXPECT_EQ(L"\r\n![a](C:/a.png)\r\n```", Text());
}

TEST_F(MarkdownFigures, IncrementalNoteParsingMatchesFullParsingAcrossLineInsertDeleteAndUndo)
{
	Insert(L"# top\r\n![a](C:/a.png)\r\n\r\n- ```\r\n  ![b](C:/b.png)\r\n  ```\r\n![c](C:/c.png)");
	pcEditWnd->SetNoteView(true);
	auto& view = pcEditWnd->GetActiveView();
	const auto check = [&] {
		const auto incremental = view.GetTextArea().GetNoteProjection().Blocks();
		view.RebuildNoteProjection();
		EXPECT_EQ(incremental, view.GetTextArea().GetNoteProjection().Blocks());
	};
	MoveTo(CLogicPoint(0, 0));
	Insert(L"```\r\n");
	check();
	Command(F_UNDO);
	check();
	Select(CLogicPoint(0, 3), CLogicPoint(0, 4));
	Command(F_DELETE);
	check();
	Command(F_UNDO);
	check();
	Select(CLogicPoint(0, 5), CLogicPoint(5, 5));
	Command(F_DELETE);
	check();
	Command(F_UNDO);
	check();
	Command(F_SELECTALL);
	Command(F_DELETE);
	check();
	Command(F_UNDO);
	check();
}

TEST_F(MarkdownFigures, DocumentLoadDefersRefreshAndDoesNotReusePreviousSelection)
{
	Insert(L"# old\r\n![old](C:/old.png)");
	Open();
	TreeView_SelectItem(Tree(), TreeView_GetChild(Tree(), TreeView_GetRoot(Tree())));
	auto& dialog = pcEditWnd->m_cDlgFuncList;
	dialog.ResetMarkdownDocument();
	Command(F_SELECTALL);
	Insert(L"# new\r\n![new](C:/new.png)");
	WaitForUpdate();
	EXPECT_EQ(L"old", Label(TreeView_GetRoot(Tree())));
	GetDllShareData().m_sWorkBuffer.m_LogicPoint = CLogicPoint(99, 99);
	::SendMessageW(dialog.GetHwnd(), WM_COMMAND, IDOK, 0);
	EXPECT_EQ(CLogicPoint(99, 99), GetDllShareData().m_sWorkBuffer.m_LogicPoint);
	dialog.FinishMarkdownLoad();
	WaitForUpdate();
	EXPECT_EQ(L"new", Label(TreeView_GetRoot(Tree())));
	EXPECT_EQ(L"図1: new", Label(TreeView_GetChild(Tree(), TreeView_GetRoot(Tree()))));
	EXPECT_EQ(nullptr, TreeView_GetSelection(Tree()));
}

namespace {
struct MarkdownScreenSettings : CPropTypesScreen {
	using CPropTypesScreen::SetData;
	using CPropTypesScreen::GetData;
};
}

TEST_F(MarkdownOutline, SettingsPageOffersMarkdownAlongsideOtherLanguages)
{
	auto page = std::make_unique<MarkdownScreenSettings>();
	auto type = std::make_unique<STypeConfig>();
	CType_Markdown().InitTypeConfig(17, *type);
	page->SetTypeData(*type);
	page->CPropTypes_Screen();
	const auto destroy = [](HWND window) { if (window) ::DestroyWindow(window); };
	std::unique_ptr<std::remove_pointer_t<HWND>, decltype(destroy)> dialog(
		::CreateDialogParamW(G_AppInstance(), MAKEINTRESOURCEW(IDD_PROP_SCREEN), ::GetDesktopWindow(),
			[](HWND, UINT, WPARAM, LPARAM) -> INT_PTR { return FALSE; }, 0), destroy);
	ASSERT_NE(nullptr, dialog.get());
	page->SetData(dialog.get());
	for (int control : {IDC_COMBO_OUTLINES, IDC_COMBO_SMARTINDENT, IDC_COMBO_SYNTAX_DISPLAY}) {
		const auto combo = ::GetDlgItem(dialog.get(), control);
		const auto markdown = ::SendMessageW(combo, CB_FINDSTRINGEXACT, -1, reinterpret_cast<LPARAM>(L"Markdown"));
		ASSERT_NE(CB_ERR, markdown);
		EXPECT_EQ(markdown, ::SendMessageW(combo, CB_GETCURSEL, 0, 0));
		::SendMessageW(combo, CB_SETCURSEL, 0, 0);
	}
	page->GetData(dialog.get());
	page->GetTypeData(*type);
	EXPECT_EQ(OUTLINE_C_CPP, type->m_eDefaultOutline);
	EXPECT_EQ(SMARTINDENT_NONE, type->m_eSmartIndent);
	EXPECT_EQ(SYNTAX_DISPLAY_NONE, type->m_eSyntaxDisplay);
	const auto display = ::GetDlgItem(dialog.get(), IDC_COMBO_SYNTAX_DISPLAY);
	const auto markdown = ::SendMessageW(display, CB_FINDSTRINGEXACT, -1, reinterpret_cast<LPARAM>(L"Markdown"));
	::SendMessageW(display, CB_SETCURSEL, markdown, 0);
	page->GetData(dialog.get());
	page->GetTypeData(*type);
	EXPECT_EQ(SYNTAX_DISPLAY_MARKDOWN, type->m_eSyntaxDisplay);
	EXPECT_EQ(OUTLINE_C_CPP, type->m_eDefaultOutline);
	EXPECT_EQ(SMARTINDENT_NONE, type->m_eSmartIndent);
	page->DispatchEvent(dialog.get(), WM_DESTROY, 0, 0);
}

namespace {
struct MarkdownSettingsIO : CShareData_IO {
	using CShareData_IO::ShareData_IO_Types;
};
}

class MarkdownInputSettings : public testing::Test, public env::ShareDataTestSuite {
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
	void Load()
	{
		profile.SetReadingMode();
		MarkdownSettingsIO::ShareData_IO_Types(profile);
	}
};

TEST_F(MarkdownInputSettings, ExistingNamesAndExtensionsDoNotOverrideSavedModeOrKeys)
{
	Configure(1, L"mArKdOwN", L"custom");
	Configure(2, L"Notes", L"md");
	Configure(3, L"Notes2", L"markdown");
	Configure(4, L"Notes3", L"mdown");
	Configure(5, L"Plain", L"txt");
	Type(2).m_nTabSpace = 2;
	Save();
	profile.SetProfileData(L"Other", L"MarkdownInputBindingsInitialized", L"0");
	Load();
	for (int index = 1; index <= 4; ++index) {
		const auto& keys = Type(index).m_typeKeyBindings;
		EXPECT_EQ(SMARTINDENT_NONE, Type(index).m_eSmartIndent);
		EXPECT_FALSE(keys[VK_RETURN].m_bOverride[0]);
		EXPECT_FALSE(keys[VK_TAB].m_bOverride[0]);
		EXPECT_FALSE(keys[VK_TAB].m_bOverride[_SHIFT]);
		EXPECT_FALSE(keys[VK_RETURN].m_bOverride[_CTRL]);
	}
	EXPECT_FALSE(Type(5).m_typeKeyBindings[VK_RETURN].m_bOverride[0]);
	EXPECT_FALSE(Type(5).m_typeKeyBindings[VK_TAB].m_bOverride[0]);
	EXPECT_STREQ(L"Notes", Type(2).m_szTypeName);
	EXPECT_STREQ(L"md", Type(2).m_szTypeExts);
	EXPECT_EQ(2, Type(2).m_nTabSpace);
}

TEST_F(MarkdownInputSettings, ExistingTypeOverridesAndExplicitUnassignmentArePreserved)
{
	Configure(1, L"Markdown", L"md");
	Type(1).m_eSmartIndent = SMARTINDENT_MARKDOWN;
	auto& keys = Type(1).m_typeKeyBindings;
	keys[VK_RETURN].m_bOverride[0] = true;
	keys[VK_RETURN].m_nFuncCodeArr[0] = F_COPY;
	keys[VK_TAB].m_bOverride[0] = true;
	keys[VK_TAB].m_nFuncCodeArr[0] = F_0;
	Save();
	Load();
	EXPECT_EQ(SMARTINDENT_MARKDOWN, Type(1).m_eSmartIndent);
	EXPECT_TRUE(Type(1).m_typeKeyBindings[VK_RETURN].m_bOverride[0]);
	EXPECT_EQ(F_COPY, Type(1).m_typeKeyBindings[VK_RETURN].m_nFuncCodeArr[0]);
	EXPECT_TRUE(Type(1).m_typeKeyBindings[VK_TAB].m_bOverride[0]);
	EXPECT_EQ(F_0, Type(1).m_typeKeyBindings[VK_TAB].m_nFuncCodeArr[0]);
	EXPECT_FALSE(Type(1).m_typeKeyBindings[VK_TAB].m_bOverride[_SHIFT]);
}

TEST_F(MarkdownInputSettings, CustomizedCommonKeysRemainInherited)
{
	Configure(1, L"Notes", L"md");
	Type(1).m_eSmartIndent = SMARTINDENT_MARKDOWN;
	auto& common = GetDllShareData().m_Common.m_sKeyBind;
	for (int i = 0; i < common.m_nKeyNameArrNum; ++i) {
		auto& key = common.m_pKeyNameArr[i];
		if (key.m_nKeyCode == VK_RETURN) key.m_nFuncCodeArr[0] = F_COPY;
		if (key.m_nKeyCode == VK_TAB) key.m_nFuncCodeArr[0] = F_0;
	}
	Save();
	Load();
	EXPECT_FALSE(Type(1).m_typeKeyBindings[VK_RETURN].m_bOverride[0]);
	EXPECT_FALSE(Type(1).m_typeKeyBindings[VK_TAB].m_bOverride[0]);
	EXPECT_FALSE(Type(1).m_typeKeyBindings[VK_TAB].m_bOverride[_SHIFT]);
}

TEST_F(MarkdownInputSettings, SavedModeCanBeEnabledAndDisabledWithLegacyKeysPresent)
{
	Configure(1, L"Notes", L"txt");
	Type(1).m_eSmartIndent = SMARTINDENT_MARKDOWN;
	Type(1).m_typeKeyBindings[VK_RETURN].m_bOverride[0] = true;
	Type(1).m_typeKeyBindings[VK_RETURN].m_nFuncCodeArr[0] = F_MARKDOWN_ENTER;
	Save();
	Type(1).m_eSmartIndent = SMARTINDENT_NONE;
	Load();
	EXPECT_EQ(SMARTINDENT_MARKDOWN, Type(1).m_eSmartIndent);
	Type(1).m_eSmartIndent = SMARTINDENT_NONE;
	Save();
	Type(1).m_eSmartIndent = SMARTINDENT_MARKDOWN;
	Load();
	EXPECT_EQ(SMARTINDENT_NONE, Type(1).m_eSmartIndent);
	EXPECT_TRUE(Type(1).m_typeKeyBindings[VK_RETURN].m_bOverride[0]);
	EXPECT_EQ(F_MARKDOWN_ENTER, Type(1).m_typeKeyBindings[VK_RETURN].m_nFuncCodeArr[0]);
}

TEST_F(MarkdownInputSettings, OutlineSelectionRoundTripsIndependentlyOfInputAssistance)
{
	Configure(1, L"Notes", L"txt");
	Type(1).m_eDefaultOutline = OUTLINE_MARKDOWN;
	Type(1).m_eSmartIndent = SMARTINDENT_NONE;
	Save();
	Type(1).m_eDefaultOutline = OUTLINE_TEXT;
	Load();
	EXPECT_EQ(OUTLINE_MARKDOWN, Type(1).m_eDefaultOutline);
	EXPECT_EQ(SMARTINDENT_NONE, Type(1).m_eSmartIndent);
	Type(1).m_eDefaultOutline = OUTLINE_TEXT;
	Type(1).m_eSmartIndent = SMARTINDENT_MARKDOWN;
	Save();
	Load();
	EXPECT_EQ(OUTLINE_TEXT, Type(1).m_eDefaultOutline);
	EXPECT_EQ(SMARTINDENT_MARKDOWN, Type(1).m_eSmartIndent);
}

TEST_F(MarkdownInputSettings, DisplaySelectionRoundTripsIndependentlyOfNameAndOtherModes)
{
	Configure(1, L"Notes", L"txt");
	Type(1).m_eSyntaxDisplay = SYNTAX_DISPLAY_MARKDOWN;
	Save();
	Type(1).m_eSyntaxDisplay = SYNTAX_DISPLAY_NONE;
	Load();
	EXPECT_EQ(SYNTAX_DISPLAY_MARKDOWN, Type(1).m_eSyntaxDisplay);
	EXPECT_EQ(SMARTINDENT_NONE, Type(1).m_eSmartIndent);
	EXPECT_EQ(OUTLINE_TEXT, Type(1).m_eDefaultOutline);

	Configure(1, L"Markdown", L"md,markdown,mdown");
	Type(1).m_eSmartIndent = SMARTINDENT_MARKDOWN;
	Type(1).m_eDefaultOutline = OUTLINE_MARKDOWN;
	Type(1).m_eSyntaxDisplay = SYNTAX_DISPLAY_NONE;
	Save();
	Type(1).m_eSyntaxDisplay = SYNTAX_DISPLAY_MARKDOWN;
	Load();
	EXPECT_EQ(SYNTAX_DISPLAY_NONE, Type(1).m_eSyntaxDisplay);
	EXPECT_EQ(SMARTINDENT_MARKDOWN, Type(1).m_eSmartIndent);
	EXPECT_EQ(OUTLINE_MARKDOWN, Type(1).m_eDefaultOutline);
}

TEST_F(MarkdownInputSettings, MissingSettingsUseCommonDefaultsRegardlessOfName)
{
	Configure(1, L"Notes", L"txt");
	Configure(2, L"Markdown", L"md");
	for (int index = 1; index <= 2; ++index) {
		Type(index).m_eSyntaxDisplay = SYNTAX_DISPLAY_MARKDOWN;
		Type(index).m_eSmartIndent = SMARTINDENT_MARKDOWN;
		Type(index).m_eDefaultOutline = OUTLINE_MARKDOWN;
		Type(index).m_bAutoCloseBrackets = true;
		Type(index).m_typeKeyBindings[VK_RETURN].m_bOverride[0] = true;
		Type(index).m_typeKeyBindings[VK_RETURN].m_nFuncCodeArr[0] = F_COPY;
	}
	profile.SetProfileData(L"Other", L"nTypesCount", L"3");
	profile.SetProfileData(L"Types(1)", L"szTypeName", L"Notes");
	profile.SetProfileData(L"Types(1)", L"szTypeExts", L"txt");
	profile.SetProfileData(L"Types(2)", L"szTypeName", L"Markdown");
	profile.SetProfileData(L"Types(2)", L"szTypeExts", L"md");
	Load();
	for (int index = 1; index <= 2; ++index) {
		EXPECT_EQ(SYNTAX_DISPLAY_NONE, Type(index).m_eSyntaxDisplay);
		EXPECT_EQ(SMARTINDENT_NONE, Type(index).m_eSmartIndent);
		EXPECT_EQ(OUTLINE_TEXT, Type(index).m_eDefaultOutline);
		EXPECT_FALSE(Type(index).m_bAutoCloseBrackets);
		EXPECT_FALSE(Type(index).m_typeKeyBindings[VK_RETURN].m_bOverride[0]);
	}
	EXPECT_STREQ(L"Notes", Type(1).m_szTypeName);
	EXPECT_STREQ(L"txt", Type(1).m_szTypeExts);
	EXPECT_STREQ(L"Markdown", Type(2).m_szTypeName);
	EXPECT_STREQ(L"md", Type(2).m_szTypeExts);
}

TEST_F(MarkdownInputSettings, LoadingSavedTypesDoesNotAddMarkdownPreset)
{
	profile.SetProfileData(L"Other", L"nTypesCount", L"2");
	profile.SetProfileData(L"Types(0)", L"szTypeName", L"Base");
	profile.SetProfileData(L"Types(0)", L"szTypeExts", L"");
	profile.SetProfileData(L"Types(1)", L"szTypeName", L"Notes");
	profile.SetProfileData(L"Types(1)", L"szTypeExts", L"txt");
	Load();
	EXPECT_EQ(2, GetDllShareData().m_nTypesCount);
	ASSERT_EQ(2u, pcShareData->GetTypeSettings().size());
	EXPECT_STREQ(L"Base", Type(0).m_szTypeName);
	EXPECT_STREQ(L"Notes", Type(1).m_szTypeName);
	EXPECT_STREQ(L"txt", Type(1).m_szTypeExts);
}
