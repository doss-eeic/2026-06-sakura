/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#pragma once

#include "parse/markdown/MarkdownTypes.h"
#include <string>
#include <string_view>

// 編集方針。構文認識はBlockParserへ委譲し、文書・ビューには依存しない。
namespace Markdown::Input {
struct EnterEdit {
	SourceRange range;
	std::wstring text;
	SourcePosition caret;
};

struct PrefixEdit {
	size_t line;
	size_t oldLength;
	std::wstring text;
};

// linesは改行を除いた論理行。nulloptは既存コマンドへ委譲する場合。
std::optional<EnterEdit> Enter(const std::vector<std::wstring_view>& lines,
	SourceRange selection, size_t tabWidth, std::wstring_view eol);

// 空の配列は処理済み（移動先がない等）。first/lastは両端を含む。
std::optional<std::vector<PrefixEdit>> Indent(const std::vector<std::wstring_view>& lines,
	size_t first, size_t last, size_t tabWidth, bool unindent);
}
