/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace ImageLink {

struct Link {
	size_t begin;
	size_t end; // exclusive, UTF-16 offsets in a logical line
	std::wstring path;
};

bool HasImageExtension(std::wstring_view path);
bool IsSupportedPath(std::wstring_view path);
std::optional<std::wstring> MakeLink(std::wstring_view path);
std::optional<Link> FindAt(std::wstring_view line, size_t position);
// A standalone logical line, excluding its line delimiter. No file I/O is performed.
std::optional<Link> FindStandalone(std::wstring_view line);
// Checks access to a local regular file, without decoding or modifying it.
bool IsReadableFile(const std::wstring& path);

}
