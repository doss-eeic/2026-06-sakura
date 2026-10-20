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
	auto operator<=>(const Block&) const = default;
};

class Projection {
public:
	static constexpr std::int64_t BlockRows = 12;
	struct Hit {
		std::int64_t layoutLine;
		const Block* block;
		int offset;
	};

	const std::vector<Block>& Blocks() const { return m_blocks; }
	void Clear() { m_blocks.clear(); }

	// The caller supplies blocks in document order, one per standalone logical line.
	void Reset(std::vector<Block> blocks) { m_blocks = std::move(blocks); }

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
		return DisplayRow(Add(line, static_cast<std::int64_t>(it - m_blocks.begin()) * BlockRows));
	}

	DisplayRow BlockStart(size_t index) const
	{
		return DisplayRow(Add(m_blocks[index].lastLayoutLine,
			Add(1, static_cast<std::int64_t>(index) * BlockRows)));
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
			if (offset < BlockRows) return {m_blocks[index].lastLayoutLine, &m_blocks[index], static_cast<int>(offset)};
		}
		return {row.value - static_cast<std::int64_t>(first) * BlockRows, nullptr, 0};
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
		return true;
	}

private:
	std::vector<Block> m_blocks;
};
}
