/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "cmd/markdown/MarkdownInput.h"
#include "parse/markdown/CMarkdownBlockParser.h"
#include <algorithm>
#include <limits>
#include <utility>

namespace Markdown::Input {
namespace {
bool SamePosition(SourcePosition a, SourcePosition b)
{
	return a.line == b.line && a.offset == b.offset;
}

bool SameContainer(const ContainerInfo& a, const ContainerInfo& b)
{
	return a.kind == b.kind && SamePosition(a.start, b.start);
}

bool Editable(const LineContext& context)
{
	return context.kind == LineKind::Paragraph || context.kind == LineKind::ListItem
		|| context.kind == LineKind::Continuation || context.kind == LineKind::Blank;
}

size_t Column(std::wstring_view text, size_t tabWidth)
{
	size_t column = 0;
	for (wchar_t c : text) column += c == L'\t' ? tabWidth - column % tabWidth : 1;
	return column;
}

size_t MarkerColumn(const ContainerInfo& container)
{
	const size_t width = container.list
		? container.list->range.end.offset - container.list->range.begin.offset + 1 : 2;
	return container.contentColumn - width;
}

// 祖先リストは本文の字下げ、引用は記号として再生成する。
// sibling=trueのときだけ最後のリストを次の項目にする。
std::optional<std::wstring> Prefix(const LineContext& context, size_t depth, bool sibling)
{
	std::wstring prefix;
	for (size_t i = 0; i < depth; ++i) {
		const auto& container = context.containers[i];
		const size_t column = MarkerColumn(container);
		if (prefix.size() < column) prefix.append(column - prefix.size(), L' ');
		if (container.kind == ContainerKind::Quote) {
			prefix += L"> ";
		} else if (sibling && i + 1 == depth) {
			const auto& list = *container.list;
			if (list.kind == ListKind::Ordered) {
				if (list.number == (std::numeric_limits<unsigned int>::max)()) return std::nullopt;
				prefix += std::to_wstring(list.number + 1);
			}
			prefix += list.marker;
			prefix += L' ';
			if (list.task) prefix += L"[ ] ";
		} else if (prefix.size() < container.contentColumn) {
			prefix.append(container.contentColumn - prefix.size(), L' ');
		}
	}
	return prefix;
}

bool Contains(const LineContext& context, SourcePosition item)
{
	return std::any_of(context.containers.begin(), context.containers.end(), [&](const auto& c) {
		return c.kind == ContainerKind::ListItem && SamePosition(c.start, item);
	});
}
}

std::optional<EnterEdit> Enter(const std::vector<std::wstring_view>& lines,
	SourceRange selection, size_t tabWidth, std::wstring_view eol)
{
	const auto from = selection.begin;
	const auto to = selection.end;
	if (tabWidth == 0 || from.line > to.line || to.line >= lines.size()
		|| from.offset > lines[from.line].size() || to.offset > lines[to.line].size()
		|| (from.line == to.line && from.offset > to.offset)) return std::nullopt;
	ParseState state;
	for (size_t y = 0; y < from.line; ++y) CMarkdownBlockParser::ParseLine(lines[y], y, tabWidth, state);
	const auto before = state;
	const auto context = CMarkdownBlockParser::ParseLine(lines[from.line], from.line, tabWidth, state);
	if (!Editable(context) || context.containers.empty() || !context.bodyRange
		|| from.offset < context.bodyRange->begin.offset) return std::nullopt;
	for (size_t y = from.line + 1; y <= to.line; ++y) {
		const auto next = CMarkdownBlockParser::ParseLine(lines[y], y, tabWidth, state);
		if (!Editable(next) || next.containers.size() != context.containers.size()
			|| !std::equal(context.containers.begin(), context.containers.end(), next.containers.begin(), SameContainer)
			|| (y == to.line && (!next.bodyRange || to.offset < next.bodyRange->begin.offset))) return std::nullopt;
	}
	// 選択削除後の行を再解析する。実際の文書はまだ変更しない。
	std::wstring remaining(lines[from.line].substr(0, from.offset));
	remaining += lines[to.line].substr(to.offset);
	state = before;
	const auto joined = CMarkdownBlockParser::ParseLine(remaining, from.line, tabWidth, state);
	if (!Editable(joined) || joined.containers.empty() || !joined.bodyRange
		|| from.offset < joined.bodyRange->begin.offset) return std::nullopt;
	const auto depth = joined.containers.size();
	if (joined.bodyEmpty && joined.containers.back().prefixRange) {
		const auto prefix = Prefix(joined, depth - 1,
			joined.containers.back().kind == ContainerKind::ListItem);
		if (!prefix) return std::nullopt;
		return EnterEdit{{{from.line, 0}, {to.line, lines[to.line].size()}}, *prefix, {from.line, prefix->size()}};
	}
	// 直前の項目を抜けた後の空行で、その項目を再開しない。
	if (joined.bodyEmpty) return std::nullopt;
	const auto prefix = Prefix(joined, depth, true);
	if (!prefix) return std::nullopt;
	return EnterEdit{selection, std::wstring(eol) + *prefix, {from.line + 1, prefix->size()}};
}

std::optional<std::vector<PrefixEdit>> Indent(const std::vector<std::wstring_view>& lines,
	size_t first, size_t last, size_t tabWidth, bool unindent)
{
	if (tabWidth == 0 || first > last || last >= lines.size()) return std::nullopt;
	ParseState state;
	std::vector<LineContext> contexts;
	contexts.reserve(lines.size());
	for (size_t y = 0; y < lines.size(); ++y) contexts.push_back(CMarkdownBlockParser::ParseLine(lines[y], y, tabWidth, state));
	std::vector<SourcePosition> roots;
	for (size_t y = first; y <= last; ++y) {
		const auto& context = contexts[y];
		// 選択済みの項目に含まれる子やコード行は二重に処理しない。
		if (std::any_of(roots.begin(), roots.end(), [&](auto root) { return Contains(context, root); })) continue;
		if (!Editable(context)) return std::nullopt;
		const auto item = std::find_if(context.containers.rbegin(), context.containers.rend(), [](const auto& c) {
			return c.kind == ContainerKind::ListItem;
		});
		if (item == context.containers.rend()) return std::nullopt;
		roots.erase(std::remove_if(roots.begin(), roots.end(), [&](auto root) {
			return Contains(contexts[root.line], item->start);
		}), roots.end());
		roots.push_back(item->start);
	}
	std::sort(roots.begin(), roots.end(), [](auto a, auto b) {
		return a.line < b.line || (a.line == b.line && a.offset < b.offset);
	});
	std::vector<PrefixEdit> edits;
	for (auto root : roots) {
		const auto& context = contexts[root.line];
		const auto found = std::find_if(context.containers.begin(), context.containers.end(), [&](const auto& c) {
			return c.kind == ContainerKind::ListItem && SamePosition(c.start, root);
		});
		const size_t depth = static_cast<size_t>(found - context.containers.begin());
		const auto& item = *found;
		// 同一行に親の記号がある項目は、行分割が必要なので階層移動しない。
		if (std::any_of(context.containers.begin(), found, [&](const auto& c) {
			return c.kind == ContainerKind::ListItem && c.start.line == root.line;
		})) continue;
		const size_t markerColumn = MarkerColumn(item);
		size_t targetColumn;
		if (unindent) {
			// 引用の境界を越えて項目を移動しない。
			if (depth == 0 || context.containers[depth - 1].kind != ContainerKind::ListItem) continue;
			targetColumn = MarkerColumn(context.containers[depth - 1]);
			if (targetColumn >= markerColumn) continue;
		} else {
			const ContainerInfo* previous = nullptr;
			size_t previousBase = 0;
			size_t y = root.line;
			while (y > 0) {
				const auto& candidate = contexts[--y];
				if (candidate.containers.size() <= depth
					|| !std::equal(context.containers.begin(), found, candidate.containers.begin(), SameContainer)
					|| candidate.containers[depth].kind != ContainerKind::ListItem) break;
				const auto& sibling = candidate.containers[depth];
				if (std::any_of(roots.begin(), roots.end(), [&](auto selected) { return SamePosition(selected, sibling.start); })) {
					y = sibling.start.line;
					continue;
				}
				previous = &sibling;
				previousBase = depth == 0 ? 0 : candidate.containers[depth - 1].contentColumn;
				break;
			}
			if (!previous) continue;
			const size_t base = depth == 0 ? 0 : context.containers[depth - 1].contentColumn;
			targetColumn = base + previous->contentColumn - previousBase;
			if (targetColumn <= markerColumn) continue;
		}
		size_t end = root.line + 1;
		while (end < lines.size() && Contains(contexts[end], root)) ++end;
		for (size_t y = root.line; y < end; ++y) {
			// 末尾の仮想行や空行に新しい空白を作らない。
			if (lines[y].find_first_not_of(L" \t") == std::wstring_view::npos) continue;
			const auto& current = contexts[y];
			// 外側の引用記号はそのまま保ち、その後の字下げだけを移動する。
			size_t quoteDepth = 0;
			for (size_t d = 0; d < depth; ++d) {
				if (current.containers[d].kind == ContainerKind::Quote) quoteDepth = d + 1;
			}
			size_t offset = 0;
			std::wstring outer;
			if (quoteDepth != 0) {
				const auto& quote = current.containers[quoteDepth - 1];
				if (quote.prefixRange) {
					offset = quote.prefixRange->end.offset;
					outer = lines[y].substr(0, offset);
				} else {
					outer = *Prefix(current, quoteDepth, false);
				}
			}
			while (offset < lines[y].size() && (lines[y][offset] == L' ' || lines[y][offset] == L'\t')) ++offset;
			const size_t oldColumn = Column(lines[y].substr(0, offset), tabWidth);
			const size_t minimum = y == root.line ? markerColumn : current.containers[depth].contentColumn;
			const size_t column = std::max(oldColumn, minimum);
			const size_t newColumn = unindent ? column - (markerColumn - targetColumn) : column + (targetColumn - markerColumn);
			const size_t outerColumn = Column(outer, tabWidth);
			if (newColumn < outerColumn) return std::nullopt;
			outer.append(newColumn - outerColumn, L' ');
			if (outer != lines[y].substr(0, offset)) edits.push_back({y, offset, std::move(outer)});
		}
	}
	return edits;
}
}
