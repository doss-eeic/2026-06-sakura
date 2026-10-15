/*! @file */
/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "view/CFontFallback.h"
#include <algorithm>
#include <dwrite.h>
#include <wrl/client.h>

#ifdef _MSC_VER
#pragma comment(lib, "dwrite.lib")
#endif

struct CFontFallback::Font {
	std::array<HFONT, 4> handles{};
	HDC dc = nullptr;
	HGDIOBJ oldFont = nullptr;
	TEXTMETRIC metrics{};
	Microsoft::WRL::ComPtr<IDWriteFontFace> face;

	Font(const LOGFONT& source, IDWriteGdiInterop* interop)
	{
		for (int style = 0; style < 4; ++style) {
			LOGFONT lf = source;
			if (style & 1) lf.lfWeight = std::min<LONG>(1000, lf.lfWeight + 300);
			if (style & 2) lf.lfUnderline = TRUE;
			handles[style] = ::CreateFontIndirectW(&lf);
		}
		dc = ::CreateCompatibleDC(nullptr);
		oldFont = ::SelectObject(dc, handles[0]);
		::GetTextMetricsW(dc, &metrics);
		if (interop) interop->CreateFontFaceFromHdc(dc, face.GetAddressOf());
	}

	~Font()
	{
		if (oldFont) ::SelectObject(dc, oldFont);
		if (dc) ::DeleteDC(dc);
		for (HFONT font : handles) if (font) ::DeleteObject(font);
	}

	bool HasGlyph(char32_t codePoint) const
	{
		// DirectWrite accepts Unicode scalar values, including supplementary planes.
		// It is used only to inspect coverage; rendering remains GDI-based.
		if (face) {
			const UINT32 code = static_cast<UINT32>(codePoint);
			UINT16 glyph = 0;
			return SUCCEEDED(face->GetGlyphIndices(&code, 1, &glyph)) && glyph != 0;
		}
		// Bitmap fonts may not expose a DirectWrite face.
		if (codePoint > 0xffff) return false;
		const wchar_t text = static_cast<wchar_t>(codePoint);
		WORD glyph = 0xffff;
		return ::GetGlyphIndicesW(dc, &text, 1, &glyph, GGI_MARK_NONEXISTING_GLYPHS) != GDI_ERROR
			&& glyph != 0xffff;
	}
};

bool CFontFallback::IsAvailable(const wchar_t* faceName)
{
	LOGFONT lf{};
	lf.lfCharSet = DEFAULT_CHARSET;
	::wcsncpy_s(lf.lfFaceName, faceName, _TRUNCATE);
	bool found = false;
	HDC dc = ::CreateCompatibleDC(nullptr);
	::EnumFontFamiliesExW(dc, &lf,
		[](const LOGFONT*, const TEXTMETRIC*, DWORD, LPARAM data) -> int {
			*reinterpret_cast<bool*>(data) = true;
			return 0;
		}, reinterpret_cast<LPARAM>(&found), 0);
	::DeleteDC(dc);
	return found;
}

CFontFallback::CFontFallback(const LOGFONT& primary, const SFontFallbacks& fallbacks)
{
	Microsoft::WRL::ComPtr<IDWriteFactory> factory;
	Microsoft::WRL::ComPtr<IDWriteGdiInterop> interop;
	if (SUCCEEDED(::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
		reinterpret_cast<IUnknown**>(factory.GetAddressOf())))) {
		factory->GetGdiInterop(interop.GetAddressOf());
	}
	m_fonts.push_back(std::make_unique<Font>(primary, interop.Get()));
	for (const auto& name : fallbacks) {
		if (name[0] == L'\0' || !IsAvailable(name)) continue;
		LOGFONT lf = primary;
		::wcscpy_s(lf.lfFaceName, static_cast<const wchar_t*>(name));
		lf.lfWidth = 0;
		lf.lfCharSet = DEFAULT_CHARSET;
		lf.lfPitchAndFamily = DEFAULT_PITCH;
		m_fonts.push_back(std::make_unique<Font>(lf, interop.Get()));
	}
	for (const auto& font : m_fonts) {
		m_ascent = std::max<int>(m_ascent, font->metrics.tmAscent);
		m_descent = std::max<int>(m_descent, font->metrics.tmDescent);
	}
}

CFontFallback::~CFontFallback() = default;

int CFontFallback::Resolve(char32_t codePoint) const
{
	if (const auto it = m_choices.find(codePoint); it != m_choices.end()) return it->second;
	int selected = 0;
	for (int i = 0; i < static_cast<int>(m_fonts.size()); ++i) {
		if (m_fonts[i]->HasGlyph(codePoint)) {
			selected = i;
			break;
		}
	}
	// No explicit font has the glyph: let GDI fall back from the primary font.
	m_choices.emplace(codePoint, selected);
	return selected;
}

HFONT CFontFallback::GetFont(int index, bool bold, bool underline) const
{
	return m_fonts.at(index)->handles[(bold ? 1 : 0) | (underline ? 2 : 0)];
}

HDC CFontFallback::GetDC(int index) const
{
	return m_fonts.at(index)->dc;
}

int CFontFallback::GetTopOffset(int index) const
{
	return m_ascent - m_fonts.at(index)->metrics.tmAscent;
}

char32_t CFontFallback::DecodePair(wchar_t high, wchar_t low)
{
	return 0x10000 + ((static_cast<char32_t>(high) - 0xd800) << 10)
		+ (static_cast<char32_t>(low) - 0xdc00);
}
