/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#pragma once

#include <memory>
#include <unordered_map>
#include <vector>
#include "env/SFontFallbacks.h"

// Used by both drawing and measuring. Index 0 always denotes the primary font.
// Callers serialize access to this object when sharing it between threads.
class CFontFallback final {
public:
	CFontFallback(const LOGFONT& primary, const SFontFallbacks& fallbacks);
	~CFontFallback();
	CFontFallback(const CFontFallback&) = delete;
	CFontFallback& operator=(const CFontFallback&) = delete;

	int Resolve(char32_t codePoint) const;
	HFONT GetFont(int index, bool bold = false, bool underline = false, bool italic = false) const;
	HDC GetDC(int index) const;
	int GetTopOffset(int index) const;
	int GetHeight() const { return m_ascent + m_descent; }
	static bool IsAvailable(const wchar_t* faceName);
	static char32_t DecodePair(wchar_t high, wchar_t low);

private:
	struct Font;
	std::vector<std::unique_ptr<Font>> m_fonts;
	mutable std::unordered_map<char32_t, int> m_choices;
	int m_ascent = 0;
	int m_descent = 0;
};
