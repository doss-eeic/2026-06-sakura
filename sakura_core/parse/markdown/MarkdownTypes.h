/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace Markdown {
enum Style : unsigned char { Normal = 0, Bold = 1, Italic = 2, Strike = 4 };

struct BlockState {
	wchar_t fence = 0;
	size_t fenceLength = 0;
};

struct BlockInfo {
	bool code;
	bool boundary;
};

// 原文上の位置。0origin
struct SourcePosition {
	size_t line = 0;
	size_t offset = 0;
};

struct SourceRange {
	SourcePosition begin;
	SourcePosition end;
};

enum class ListKind {
	Bullet,
	Ordered
};

struct TaskMarkerInfo {
	SourceRange range;
	bool checked = false;
};

struct ListMarkerInfo {
	ListKind kind = ListKind::Bullet;
	wchar_t marker = L'-';
	unsigned int number = 1; // Orderedの原文の番号
	SourceRange range; // リスト記号本体。空白除く
	std::optional<TaskMarkerInfo> task;
};

enum class ContainerKind {
	Quote,
	ListItem
};

struct ContainerInfo {
	ContainerKind kind = ContainerKind::Quote;
	SourcePosition start; // コンテナの原文での開始位置
	std::optional<SourcePosition> parentItem; //所属する親リスト項目
	std::optional<SourceRange> prefixRange; //現在行で明示された接頭辞の範囲
	size_t contentColumn = 0; // タブを展開した、0始まりの構文上の列
	std::optional<ListMarkerInfo> list;
};

enum class LineKind {
	Unknown, Paragraph, ListItem, Continuation, Blank,
	Fence, FencedCode, IndentedCode, Heading, ThematicBreak
};

struct LineContext {
	LineKind kind = LineKind::Unknown;
	std::vector<ContainerInfo> containers; // 現在行が属する引用・リスト項目
	std::optional<SourceRange> bodyRange; // 接頭辞を除いた本文範囲
	bool bodyEmpty = false;
};

struct FenceInfo {
	wchar_t marker = L'`';
	size_t length = 0;
	SourcePosition start;
	size_t containerDepth = 0;
	size_t indentColumns = 0; // コンテナ開始列からの字下げ量
};

struct ParseState {
	std::vector<ContainerInfo> containers;
	std::optional<FenceInfo> fence;
	bool paragraphOpen = false;
	bool previousLineBlank = false;
};
}
