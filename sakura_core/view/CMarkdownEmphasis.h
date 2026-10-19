/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#pragma once

#include <cstddef>
#include <string_view>
#include <unordered_map>
#include <vector>

class CDocLine;
struct STypeConfig;

// Paint-local cache: never retains document pointers across edits or type changes.
class CMarkdownEmphasis {
public:
	enum Style : unsigned char { Normal = 0, Bold = 1, Italic = 2 };
	struct BlockState {
		wchar_t fence = 0;
		size_t fenceLength = 0;
	};
	struct BlockInfo {
		bool code;
		bool boundary;
	};

	static bool IsEnabled(const STypeConfig& type);
	static std::vector<unsigned char> Parse(std::wstring_view text);
	static BlockInfo ScanBlock(std::wstring_view text, BlockState& state);
	void Prepare(const CDocLine* line);
	unsigned char GetStyle(size_t position) const;

private:
	struct LineRange { size_t offset; size_t length; };
	std::unordered_map<const CDocLine*, LineRange> m_lines;
	std::vector<unsigned char> m_styles;
	LineRange m_current{};
	const CDocLine* m_nextLine = nullptr;
	BlockState m_block;
};
