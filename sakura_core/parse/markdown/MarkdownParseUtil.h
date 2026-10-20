/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#pragma once

#include <Windows.h>
#include <string_view>

namespace Markdown::Detail {
inline bool IsSpace(wchar_t c)
{
	if (c < 0x80) return c == 0 || c == L' ' || (L'\t' <= c && c <= L'\r');
	WORD category = 0;
	::GetStringTypeW(CT_CTYPE1, &c, 1, &category);
	return (category & C1_SPACE) != 0;
}

inline size_t RunEnd(std::wstring_view text, size_t start)
{
	size_t end = start + 1;
	while (end < text.size() && text[end] == text[start]) ++end;
	return end;
}
}
