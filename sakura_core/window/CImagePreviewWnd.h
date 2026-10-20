/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#pragma once

#include <Windows.h>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include "util/ImageDecoder.h"

// A display-only popup. The worker never accesses editor state or HWNDs.
class CImagePreviewWnd {
public:
	using Image = ImageDecoder::Image;
	CImagePreviewWnd() = default;
	~CImagePreviewWnd();
	CImagePreviewWnd(const CImagePreviewWnd&) = delete;
	CImagePreviewWnd& operator=(const CImagePreviewWnd&) = delete;

	void Begin(HWND owner, const std::wstring& path, POINT screenPoint);
	void Poll();
	void Hide();

	// Caller initializes COM. Header dimensions are checked before pixel decoding.
	static std::optional<Image> Decode(const std::wstring& path, UINT maxWidth, UINT maxHeight);
	static bool IsAllowedSize(UINT width, UINT height);

private:
	struct Request {
		std::uint64_t generation;
		std::wstring path;
		UINT maxWidth;
		UINT maxHeight;
	};
	void Work(std::stop_token stop);
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

	HWND m_hwnd = nullptr;
	HWND m_owner = nullptr;
	POINT m_point{};
	RECT m_workArea{};
	int m_gap = 16;
	std::optional<Image> m_image;
	std::mutex m_mutex;
	std::condition_variable m_wake;
	std::uint64_t m_generation = 0;
	std::optional<Request> m_request;
	std::optional<Image> m_ready;
	std::jthread m_worker; // joined before the shared state is destroyed
};
