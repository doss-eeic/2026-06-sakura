/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "window/CImagePreviewWnd.h"
#include "util/ImageLink.h"
#include "cxx/com_pointer.hpp"
#include <algorithm>
#include <wincodec.h>

bool CImagePreviewWnd::IsAllowedSize(UINT width, UINT height)
{
	// At most 80 MB of source RGBA pixels, with bounded codec row/column sizes.
	return width && height && width <= 8192 && height <= 8192 &&
		static_cast<std::uint64_t>(width) * height <= 20000000;
}

std::optional<CImagePreviewWnd::Image> CImagePreviewWnd::Decode(const std::wstring& path, UINT maxWidth, UINT maxHeight)
{
	if (!maxWidth || !maxHeight || !ImageLink::IsReadableFile(path)) return std::nullopt;
	cxx::com_pointer<IWICImagingFactory> factory;
	if (FAILED(factory.CreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER))) return std::nullopt;
	cxx::com_pointer<IWICBitmapDecoder> decoder;
	if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder))) return std::nullopt;
	GUID format{};
	if (FAILED(decoder->GetContainerFormat(&format)) ||
		(!IsEqualGUID(format, GUID_ContainerFormatPng) && !IsEqualGUID(format, GUID_ContainerFormatJpeg))) return std::nullopt;
	cxx::com_pointer<IWICBitmapFrameDecode> frame;
	if (FAILED(decoder->GetFrame(0, &frame))) return std::nullopt;
	UINT width = 0, height = 0;
	if (FAILED(frame->GetSize(&width, &height)) || !IsAllowedSize(width, height)) return std::nullopt;
	const double ratio = std::min({1.0, static_cast<double>(maxWidth) / width, static_cast<double>(maxHeight) / height});
	const UINT targetWidth = std::max(1u, static_cast<UINT>(width * ratio));
	const UINT targetHeight = std::max(1u, static_cast<UINT>(height * ratio));
	cxx::com_pointer<IWICBitmapScaler> scaler;
	if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
		FAILED(scaler->Initialize(frame, targetWidth, targetHeight, WICBitmapInterpolationModeFant))) return std::nullopt;
	cxx::com_pointer<IWICFormatConverter> converter;
	if (FAILED(factory->CreateFormatConverter(&converter)) ||
		FAILED(converter->Initialize(scaler, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
			nullptr, 0, WICBitmapPaletteTypeCustom))) return std::nullopt;
	Image image{targetWidth, targetHeight, std::vector<BYTE>(static_cast<size_t>(targetWidth) * targetHeight * 4)};
	if (FAILED(converter->CopyPixels(nullptr, targetWidth * 4, static_cast<UINT>(image.pixels.size()), image.pixels.data()))) return std::nullopt;
	// Composite transparent PNGs on white; the popup itself remains opaque.
	for (size_t i = 0; i < image.pixels.size(); i += 4) {
		const BYTE white = 255 - image.pixels[i + 3];
		for (size_t channel = 0; channel < 3; ++channel) image.pixels[i + channel] += white;
		image.pixels[i + 3] = 255;
	}
	return image;
}

CImagePreviewWnd::~CImagePreviewWnd()
{
	Hide();
	{
		std::lock_guard lock(m_mutex);
		m_worker.request_stop();
	}
	m_wake.notify_one();
	if (m_worker.joinable()) m_worker.join();
	if (m_hwnd) ::DestroyWindow(m_hwnd);
}

void CImagePreviewWnd::Hide()
{
	if (m_hwnd) ::ShowWindow(m_hwnd, SW_HIDE);
	m_image.reset();
	std::lock_guard lock(m_mutex);
	++m_generation;
	m_request.reset();
	m_ready.reset();
}

void CImagePreviewWnd::Begin(HWND owner, const std::wstring& path, POINT screenPoint)
{
	Hide();
	m_owner = owner;
	m_point = screenPoint;
	MONITORINFO monitor{sizeof(monitor)};
	if (!::GetMonitorInfoW(::MonitorFromPoint(screenPoint, MONITOR_DEFAULTTONEAREST), &monitor)) return;
	m_workArea = monitor.rcWork;
	const UINT dpi = ::GetDpiForWindow(owner);
	m_gap = ::MulDiv(16, dpi ? dpi : 96, 96);
	const int availableHeight = std::max(screenPoint.y - m_workArea.top, m_workArea.bottom - screenPoint.y) - m_gap - 2;
	const int maxWidth = std::min(::MulDiv(480, dpi ? dpi : 96, 96), static_cast<int>(m_workArea.right - m_workArea.left) - 2);
	const int maxHeight = std::min(::MulDiv(360, dpi ? dpi : 96, 96), availableHeight);
	if (maxWidth <= 0 || maxHeight <= 0) return;
	try {
		if (!m_worker.joinable()) m_worker = std::jthread([this](std::stop_token stop) { Work(stop); });
		{
			std::lock_guard lock(m_mutex);
			m_request = Request{m_generation, path, static_cast<UINT>(maxWidth), static_cast<UINT>(maxHeight)};
		}
		m_wake.notify_one();
	} catch (const std::exception&) {
		// Hover failures are silent; normal editing must remain available.
	}
}

void CImagePreviewWnd::Work(std::stop_token stop)
{
	const HRESULT com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(com)) return;
	while (!stop.stop_requested()) {
		std::optional<Request> request;
		{
			std::unique_lock lock(m_mutex);
			m_wake.wait(lock, [&] { return stop.stop_requested() || m_request.has_value(); });
			if (stop.stop_requested()) break;
			request = std::move(m_request);
			m_request.reset();
		}
		try {
			auto image = Decode(request->path, request->maxWidth, request->maxHeight);
			std::lock_guard lock(m_mutex);
			if (!stop.stop_requested() && request->generation == m_generation) m_ready = std::move(image);
		} catch (const std::exception&) {
			// Allocation/codec failures do not escape the worker.
		}
	}
	::CoUninitialize();
}

void CImagePreviewWnd::Poll()
{
	{
		std::lock_guard lock(m_mutex);
		if (!m_ready) return;
		m_image = std::move(m_ready);
		m_ready.reset();
	}
	if (!m_hwnd) {
		WNDCLASSW wc{};
		wc.hInstance = ::GetModuleHandleW(nullptr);
		wc.lpszClassName = L"SakuraImagePreview";
		wc.lpfnWndProc = WndProc;
		::RegisterClassW(&wc);
		m_hwnd = ::CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
			wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, m_owner, nullptr, wc.hInstance, this);
		if (!m_hwnd) { m_image.reset(); return; }
	}
	const int width = static_cast<int>(m_image->width) + 2;
	const int height = static_cast<int>(m_image->height) + 2;
	int x = m_point.x + m_gap;
	int y = m_point.y + m_gap;
	if (x + width > m_workArea.right) x = m_point.x - m_gap - width;
	if (y + height > m_workArea.bottom) y = m_point.y - m_gap - height;
	x = std::clamp(x, static_cast<int>(m_workArea.left), static_cast<int>(m_workArea.right) - width);
	y = std::clamp(y, static_cast<int>(m_workArea.top), static_cast<int>(m_workArea.bottom) - height);
	::SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
	::InvalidateRect(m_hwnd, nullptr, FALSE);
}

LRESULT CALLBACK CImagePreviewWnd::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_NCCREATE) {
		const auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
		::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
	}
	auto self = reinterpret_cast<CImagePreviewWnd*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	switch (message) {
	case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
	case WM_NCHITTEST: return HTTRANSPARENT;
	case WM_ERASEBKGND: return 1;
	case WM_NCDESTROY:
		if (self) self->m_hwnd = nullptr;
		break;
	case WM_PAINT: {
		PAINTSTRUCT paint{};
		HDC dc = ::BeginPaint(hwnd, &paint);
		if (self && self->m_image) {
			const auto& image = *self->m_image;
			BITMAPINFO info{};
			info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			info.bmiHeader.biWidth = image.width;
			info.bmiHeader.biHeight = -static_cast<LONG>(image.height);
			info.bmiHeader.biPlanes = 1;
			info.bmiHeader.biBitCount = 32;
			info.bmiHeader.biCompression = BI_RGB;
			::SetDIBitsToDevice(dc, 1, 1, image.width, image.height, 0, 0, 0, image.height, image.pixels.data(), &info, DIB_RGB_COLORS);
			RECT rect{};
			::GetClientRect(hwnd, &rect);
			::FrameRect(dc, &rect, static_cast<HBRUSH>(::GetStockObject(GRAY_BRUSH)));
		}
		::EndPaint(hwnd, &paint);
		return 0;
	}
	}
	return ::DefWindowProcW(hwnd, message, wParam, lParam);
}
