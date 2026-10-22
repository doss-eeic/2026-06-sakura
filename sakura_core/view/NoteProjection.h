/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#pragma once

#include <algorithm>
#include <compare>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

// Display rows are deliberately not CLayoutInt: virtual rows are never document positions.
namespace NoteView {
struct DisplayRow {
	std::int64_t value = 0;
	explicit constexpr DisplayRow(std::int64_t value = 0) : value(value) {}
	auto operator<=>(const DisplayRow&) const = default;
};

struct Block {
	std::int64_t logicalLine = 0;
	std::int64_t lastLayoutLine = 0;
	std::wstring path;
	int maxRows = 12;
	int rows = 12;
	// Zero dimensions mean pending; failed images use rows=3.
	unsigned sourceWidth = 0;
	unsigned sourceHeight = 0;
	bool failed = false;
	auto operator<=>(const Block&) const = default;
};

class Projection {
public:
	static constexpr std::int64_t DefaultRows = 12;
	static constexpr std::int64_t MaxRows = 40;
	struct Hit {
		std::int64_t layoutLine;
		const Block* block;
		int offset;
	};

	const std::vector<Block>& Blocks() const { return m_blocks; }
	void Clear() { m_blocks.clear(); m_prefix.assign(1, 0); }

	// The caller supplies blocks in document order, one per standalone logical line.
	void Reset(std::vector<Block> blocks) { m_blocks = std::move(blocks); Reindex(); }
	void Reindex()
	{
		m_prefix.clear();
		m_prefix.reserve(m_blocks.size() + 1);
		m_prefix.push_back(0);
		for (const auto& block : m_blocks) m_prefix.push_back(Add(m_prefix.back(), block.rows));
	}
	bool SetRows(size_t index, int rows)
	{
		if (index >= m_blocks.size() || m_blocks[index].rows == rows) return false;
		m_blocks[index].rows = std::clamp(rows, 3, 40);
		Reindex();
		return true;
	}
	// Batch geometry changes: visit each block once and rebuild prefix sums once.
	// The callback must preserve logical/layout anchors and document order.
	template<class Update>
	bool UpdateBlocks(Update&& update)
	{
		bool changed = false;
		for (auto& block : m_blocks) changed = update(block) || changed;
		if (changed) Reindex();
		return changed;
	}

	static std::int64_t Add(std::int64_t a, std::int64_t b)
	{
		constexpr auto max = (std::numeric_limits<std::int64_t>::max)();
		constexpr auto min = (std::numeric_limits<std::int64_t>::min)();
		if (b > 0 && a > max - b) return max;
		if (b < 0 && a < min - b) return min;
		return a + b;
	}

	DisplayRow FromLayout(std::int64_t line) const
	{
		const auto it = std::lower_bound(m_blocks.begin(), m_blocks.end(), line,
			[](const Block& block, std::int64_t row) { return block.lastLayoutLine < row; });
		return DisplayRow(Add(line, m_prefix[it - m_blocks.begin()]));
	}

	DisplayRow BlockStart(size_t index) const
	{
		return DisplayRow(Add(m_blocks[index].lastLayoutLine, Add(1, m_prefix[index])));
	}

	size_t FirstVisibleBlock(DisplayRow top) const
	{
		const auto line = ToLayout(top).layoutLine;
		return std::lower_bound(m_blocks.begin(), m_blocks.end(), line,
			[](const Block& block, std::int64_t row) { return block.lastLayoutLine < row; }) - m_blocks.begin();
	}

	Hit ToLayout(DisplayRow row) const
	{
		// Find the last block whose first virtual row is at or before row.
		size_t first = 0, last = m_blocks.size();
		while (first < last) {
			const auto mid = first + (last - first) / 2;
			if (BlockStart(mid) <= row) first = mid + 1;
			else last = mid;
		}
		if (first) {
			const auto index = first - 1;
			const auto offset = row.value - BlockStart(index).value;
			if (offset < m_blocks[index].rows) return {m_blocks[index].lastLayoutLine, &m_blocks[index], static_cast<int>(offset)};
		}
		return {row.value - m_prefix[first], nullptr, 0};
	}

	// Reparse only changed logical lines. Unchanged blocks retain their paths and move
	// by the actual logical/layout deltas, including edits before the first image.
	bool Replace(std::int64_t firstLine, std::int64_t oldLastLine,
		std::int64_t logicalDelta, std::int64_t layoutDelta, std::vector<Block> replacement)
	{
		const auto first = std::lower_bound(m_blocks.begin(), m_blocks.end(), firstLine,
			[](const Block& block, std::int64_t line) { return block.logicalLine < line; });
		const auto last = std::upper_bound(first, m_blocks.end(), oldLastLine,
			[](std::int64_t line, const Block& block) { return line < block.logicalLine; });
		const bool shifted = (logicalDelta != 0 || layoutDelta != 0) && last != m_blocks.end();
		if (!shifted && std::equal(first, last, replacement.begin(), replacement.end())) return false;
		const auto index = first - m_blocks.begin();
		const auto tail = m_blocks.erase(first, last);
		if (shifted) {
			for (auto it = tail; it != m_blocks.end(); ++it) {
				it->logicalLine = Add(it->logicalLine, logicalDelta);
				it->lastLayoutLine = Add(it->lastLayoutLine, layoutDelta);
			}
		}
		m_blocks.insert(m_blocks.begin() + index,
			std::make_move_iterator(replacement.begin()), std::make_move_iterator(replacement.end()));
		Reindex();
		return true;
	}

private:
	std::vector<Block> m_blocks;
	std::vector<std::int64_t> m_prefix{0};
};
}
