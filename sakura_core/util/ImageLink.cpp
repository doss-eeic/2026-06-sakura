/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "util/ImageLink.h"
#include <algorithm>
#include <cwctype>

namespace ImageLink {

bool HasImageExtension(std::wstring_view path)
{
	const auto dot = path.find_last_of(L'.');
	if (dot == std::wstring_view::npos) return false;
	std::wstring ext(path.substr(dot));
	std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
	return ext == L".png" || ext == L".jpg" || ext == L".jpeg";
}

bool IsSupportedPath(std::wstring_view path)
{
	if (path.size() < 4 || !((L'A' <= path[0] && path[0] <= L'Z') || (L'a' <= path[0] && path[0] <= L'z')) ||
		path[1] != L':' || (path[2] != L'/' && path[2] != L'\\')) return false;
	for (size_t i = 2; i < path.size(); ++i) {
		const auto c = path[i];
		if (c < L' ' || c == 0x7f || c == L'<' || c == L'>' || c == L'"' || c == L'|' ||
			c == L'*' || c == L'?' || c == L':') return false;
	}
	return HasImageExtension(path);
}

std::optional<std::wstring> MakeLink(std::wstring_view path)
{
	if (!IsSupportedPath(path)) return std::nullopt;
	std::wstring normalized(path);
	std::replace(normalized.begin(), normalized.end(), L'\\', L'/');
	const auto name = std::wstring_view(normalized).substr(normalized.find_last_of(L'/') + 1);
	const auto stem = name.substr(0, name.find_last_of(L'.'));
	std::wstring text = L"![";
	for (const auto c : stem) {
		if (c == L']' || c == L'\\') text += L'\\';
		text += c;
	}
	text += L"](";
	const bool angle = normalized.find_first_of(L" ()") != std::wstring::npos ||
		std::any_of(normalized.begin(), normalized.end(), [](wchar_t c) { return std::iswspace(c) != 0; });
	if (angle) text += L'<';
	text += normalized;
	if (angle) text += L'>';
	text += L')';
	return text;
}

std::optional<Link> FindAt(std::wstring_view line, size_t position)
{
	if (position >= line.size()) return std::nullopt;
	line = line.substr(0, line.find_first_of(L"\r\n"));
	for (size_t start = 0; start < line.size() && start <= position;) {
		start = line.find(L"![", start);
		if (start == std::wstring_view::npos || start > position) break;
		size_t i = start + 2;
		while (i < line.size() && line[i] != L']') {
			if (line[i] == L'\\' && i + 1 < line.size() && (line[i + 1] == L']' || line[i + 1] == L'\\')) ++i;
			++i;
		}
		if (i + 1 >= line.size() || line[i + 1] != L'(') {
			start = i + 1;
			continue;
		}
		i += 2;
		const bool angle = i < line.size() && line[i] == L'<';
		if (angle) ++i;
		const auto pathStart = i;
		while (i < line.size() && line[i] != (angle ? L'>' : L')')) ++i;
		const auto path = line.substr(pathStart, i - pathStart);
		const auto end = i + (angle ? 2 : 1);
		const bool closed = i < line.size() && (!angle || (i + 1 < line.size() && line[i + 1] == L')'));
		const bool bareValid = angle || (path.find_first_of(L"()") == std::wstring_view::npos &&
			std::none_of(path.begin(), path.end(), [](wchar_t c) { return std::iswspace(c) != 0; }));
		if (closed && bareValid && IsSupportedPath(path) && position < end) {
			return Link{start, end, std::wstring(path)};
		}
		start = i + 1;
	}
	return std::nullopt;
}

bool IsReadableFile(const std::wstring& path)
{
	if (!IsSupportedPath(path)) return false;
	const wchar_t root[] = {path[0], L':', L'\\', L'\0'};
	if (::GetDriveTypeW(root) == DRIVE_REMOTE) return false;
	const auto file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return false;
	BY_HANDLE_FILE_INFORMATION info{};
	const bool regular = ::GetFileType(file) == FILE_TYPE_DISK && ::GetFileInformationByHandle(file, &info) &&
		!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
	::CloseHandle(file);
	return regular;
}

}
