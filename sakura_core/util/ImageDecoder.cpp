/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "util/ImageDecoder.h"
#include "util/ImageLink.h"
#include "cxx/com_pointer.hpp"
#include <algorithm>
#include <wincodec.h>

bool ImageDecoder::IsAllowedSize(UINT width, UINT height)
{
	return width && height && width <= 8192 && height <= 8192 &&
		static_cast<std::uint64_t>(width) * height <= 20000000;
}

std::optional<ImageDecoder::Image> ImageDecoder::Decode(const std::wstring& path, UINT maxWidth, UINT maxHeight)
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
	for (size_t i = 0; i < image.pixels.size(); i += 4) {
		const BYTE white = 255 - image.pixels[i + 3];
		for (size_t channel = 0; channel < 3; ++channel) image.pixels[i + channel] += white;
		image.pixels[i + 3] = 255;
	}
	return image;
}
