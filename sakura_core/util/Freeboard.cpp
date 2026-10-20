/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "util/Freeboard.h"
#include "util/ImageLink.h"
#include "cxx/com_pointer.hpp"
#include <wincodec.h>

namespace {
std::optional<std::vector<BYTE>> EncodeWhitePng()
{
	struct ComScope {
		HRESULT result = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		~ComScope() { if (SUCCEEDED(result)) ::CoUninitialize(); }
	} com;
	if (FAILED(com.result) && com.result != RPC_E_CHANGED_MODE) return std::nullopt;
	cxx::com_pointer<IWICImagingFactory> factory;
	if (FAILED(factory.CreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER))) return std::nullopt;
	cxx::com_pointer<IStream> stream;
	if (FAILED(::CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return std::nullopt;
	cxx::com_pointer<IWICBitmapEncoder> encoder;
	if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
		FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache))) return std::nullopt;
	cxx::com_pointer<IWICBitmapFrameEncode> frame;
	if (FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
		FAILED(frame->SetSize(1200, 800))) return std::nullopt;
	GUID format = GUID_WICPixelFormat32bppBGRA;
	if (FAILED(frame->SetPixelFormat(&format)) || !IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)) return std::nullopt;
	std::vector<BYTE> row(1200 * 4, 255);
	for (UINT y = 0; y < 800; ++y)
		if (FAILED(frame->WritePixels(1, static_cast<UINT>(row.size()), static_cast<UINT>(row.size()), row.data()))) return std::nullopt;
	if (FAILED(frame->Commit()) || FAILED(encoder->Commit())) return std::nullopt;
	STATSTG stat{};
	if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) || stat.cbSize.QuadPart > MAXDWORD) return std::nullopt;
	std::vector<BYTE> png(static_cast<size_t>(stat.cbSize.QuadPart));
	ULONG read = 0;
	if (FAILED(stream->Seek(LARGE_INTEGER{}, STREAM_SEEK_SET, nullptr)) ||
		FAILED(stream->Read(png.data(), static_cast<ULONG>(png.size()), &read)) || read != png.size()) return std::nullopt;
	return png;
}
}

Freeboard::Creation::~Creation()
{
	if (!Cancel()) Keep(); // Leave a residue rather than ever deleting an unowned pathname.
}

bool Freeboard::Creation::Create(const std::filesystem::path& directory, const std::wstring& stem)
{
	if (m_file != INVALID_HANDLE_VALUE || !m_path.empty() || stem.empty() ||
		stem.find_first_of(L"/\\") != stem.npos) return false;
	try {
		const auto png = EncodeWhitePng();
		if (!png) return false;
		for (unsigned number = 1; number < 10000; ++number) {
			const auto filename = stem + (number == 1 ? L"" : L"_" + std::to_wstring(number)) + L".png";
			const auto candidate = (directory / filename).wstring();
			const auto link = ImageLink::MakeLink(candidate);
			if (candidate.size() >= MAX_PATH || filename.size() > 255 || !link) return false;
			// Resolve and allocate all strings before acquiring the file.
			m_path = candidate;
			m_link = *link;
			m_file = ::CreateFileW(m_path.c_str(), GENERIC_WRITE | DELETE, 0, nullptr,
				CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (m_file == INVALID_HANDLE_VALUE) {
				const auto error = ::GetLastError();
				if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) continue;
				return false;
			}
			DWORD written = 0;
			return ::WriteFile(m_file, png->data(), static_cast<DWORD>(png->size()), &written, nullptr) &&
				written == png->size() && ::FlushFileBuffers(m_file);
		}
	} catch (const std::exception&) { }
	return false;
}

bool Freeboard::Creation::Cancel()
{
	if (m_file == INVALID_HANDLE_VALUE) return true;
	FILE_DISPOSITION_INFO disposition{TRUE};
	if (!::SetFileInformationByHandle(m_file, FileDispositionInfo, &disposition, sizeof(disposition))) return false;
	Keep();
	return true;
}

void Freeboard::Creation::Keep()
{
	if (m_file != INVALID_HANDLE_VALUE) ::CloseHandle(m_file);
	m_file = INVALID_HANDLE_VALUE;
}
