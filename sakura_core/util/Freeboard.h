/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#pragma once
#include <Windows.h>
#include <filesystem>
#include <string>

namespace Freeboard {
// Keeps exclusive ownership from CREATE_NEW through text insertion. No path-based
// deletion or reopening is allowed; an abandoned operation rolls back its own handle.
class Creation {
public:
	Creation() = default;
	~Creation();
	Creation(const Creation&) = delete;
	Creation& operator=(const Creation&) = delete;
	bool Create(const std::filesystem::path& directory, const std::wstring& stem);
	bool Cancel(); // false: could not remove the owned file; Path() identifies the residue
	void Keep();   // retain PNG after successful insertion or an ambiguous insertion failure
	const std::wstring& Path() const { return m_path; }
	const std::wstring& Link() const { return m_link; }
private:
	HANDLE m_file = INVALID_HANDLE_VALUE;
	std::wstring m_path;
	std::wstring m_link;
};
}
