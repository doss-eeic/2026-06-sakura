/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#pragma once

#include "parse/markdown/MarkdownTypes.h"
#include <string_view>

class CMarkdownBlockParser {
public:
	static bool IsCodeLine(const Markdown::LineContext& context)
	{
		return context.kind == Markdown::LineKind::Fence || context.kind == Markdown::LineKind::FencedCode ||
			context.kind == Markdown::LineKind::IndentedCode;
	}

	// 論理行を先頭から順に解析する。文書先頭ではstateを初期化する。
	static Markdown::LineContext ParseLine(
		std::wstring_view text, // 改行を除いた1行の文字列
		size_t lineIndex, // 0始まりの論理行番号
		size_t tabWidth, // タイプ設定のタブ幅。1以上。同じ解析中は同じ幅を使う
		Markdown::ParseState& state
	);

	static Markdown::BlockInfo ScanBlock(std::wstring_view text, Markdown::BlockState& state);
};
