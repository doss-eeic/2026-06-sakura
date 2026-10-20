/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#pragma once

#include <cstddef>
#include "parse/markdown/MarkdownTypes.h"
#include <unordered_map>
#include <vector>

class CDocLine;
struct STypeConfig;

// Paint-local cache: never retains document pointers across edits or type changes.
class CMarkdownEmphasis {
public:
	static bool IsEnabled(const STypeConfig& type);
	void Prepare(const CDocLine* line);
	unsigned char GetStyle(size_t position) const;

private:
	struct LineRange { size_t offset; size_t length; };
	std::unordered_map<const CDocLine*, LineRange> m_lines;
	std::vector<unsigned char> m_styles;
	LineRange m_current{};
	const CDocLine* m_nextLine = nullptr;
	Markdown::BlockState m_block;
};
