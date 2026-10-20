/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#include "StdAfx.h"
#include "view/NoteImageCache.h"
#include <cmath>

namespace NoteView {
namespace {
WIN32_FILE_ATTRIBUTE_DATA FileStamp(const std::wstring& path)
{
	WIN32_FILE_ATTRIBUTE_DATA stamp{};
	::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &stamp);
	return stamp;
}
bool SameStamp(const WIN32_FILE_ATTRIBUTE_DATA& a, const WIN32_FILE_ATTRIBUTE_DATA& b)
{
	return a.ftLastWriteTime.dwLowDateTime == b.ftLastWriteTime.dwLowDateTime &&
		a.ftLastWriteTime.dwHighDateTime == b.ftLastWriteTime.dwHighDateTime &&
		a.nFileSizeLow == b.nFileSizeLow && a.nFileSizeHigh == b.nFileSizeHigh && a.dwFileAttributes == b.dwFileAttributes;
}
}
ImageCache::~ImageCache()
{
	{
		std::lock_guard lock(m_mutex);
		m_worker.request_stop();
	}
	m_wake.notify_one();
	if (m_worker.joinable()) m_worker.join();
}

void ImageCache::Reset()
{
	std::lock_guard lock(m_mutex);
	m_entries.clear();
	m_stamps.clear();
	m_sources.clear();
	m_bytes = 0;
}

void ImageCache::Invalidate(const std::wstring& path)
{
	std::lock_guard lock(m_mutex);
	m_stamps.erase(path);
	m_sources.erase(path);
	for (auto it = m_entries.begin(); it != m_entries.end();) {
		if (it->key.path == path) { auto remove = it++; Erase(remove); }
		else ++it;
	}
}

std::vector<std::wstring> ImageCache::ChangedPaths() const
{
	std::vector<std::pair<std::wstring, WIN32_FILE_ATTRIBUTE_DATA>> snapshots;
	{
		std::lock_guard lock(m_mutex);
		for (const auto& entry : m_stamps) snapshots.push_back(entry);
	}
	std::vector<std::wstring> changed;
	for (const auto& [path, stamp] : snapshots)
		if (!SameStamp(stamp, FileStamp(path)) && std::find(changed.begin(), changed.end(), path) == changed.end())
			changed.push_back(path);
	return changed;
}

size_t ImageCache::ByteCount() const { std::lock_guard lock(m_mutex); return m_bytes; }
size_t ImageCache::EntryCount() const { std::lock_guard lock(m_mutex); return m_entries.size(); }

ImageCache::SourceInfo ImageCache::GetSourceInfo(const std::wstring& path) const
{
	std::lock_guard lock(m_mutex);
	const auto found = m_sources.find(path);
	return found == m_sources.end() ? SourceInfo{} : found->second;
}

void ImageCache::Erase(std::list<Entry>::iterator entry)
{
	if (entry->image) m_bytes -= entry->image->pixels.size();
	m_entries.erase(entry);
}

ImageCache::Result ImageCache::Get(const ImageKey& key)
{
	std::lock_guard lock(m_mutex);
	auto entry = std::find_if(m_entries.begin(), m_entries.end(), [&](const auto& e) { return e.key == key; });
	if (entry != m_entries.end()) {
		m_entries.splice(m_entries.end(), m_entries, entry);
		return {entry->state, entry->image};
	}
	if (!key.width || !key.height) return {State::Failed, {}};
	if (m_entries.size() == MaxEntries) {
		// Do not evict in-flight work: a very small font can expose more than 16 images.
		// Deferred callers retry from the UI timer, after completed rectangles are painted.
		entry = std::find_if(m_entries.begin(), m_entries.end(), [](const auto& e) { return e.state != State::Loading; });
		if (entry == m_entries.end()) return {State::Loading, {}};
		Erase(entry);
	}
	try {
		if (!m_worker.joinable()) m_worker = std::jthread([this](std::stop_token stop) { Work(stop); });
		m_entries.push_back({key, ++m_nextId});
		m_entries.back().stamp = FileStamp(key.path);
		m_stamps.try_emplace(key.path, m_entries.back().stamp);
		m_wake.notify_one();
		return {State::Loading, {}};
	} catch (const std::exception&) {
		return {State::Failed, {}};
	}
}

void ImageCache::Work(std::stop_token stop)
{
	const auto com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	while (!stop.stop_requested()) {
		ImageKey key;
		std::uint64_t id;
		{
			std::unique_lock lock(m_mutex);
			m_wake.wait(lock, [&] {
				return stop.stop_requested() || std::any_of(m_entries.begin(), m_entries.end(),
					[](const auto& e) { return e.state == State::Loading && !e.started; });
			});
			if (stop.stop_requested()) break;
			auto entry = std::find_if(m_entries.begin(), m_entries.end(),
				[](const auto& e) { return e.state == State::Loading && !e.started; });
			key = entry->key;
			id = entry->id;
			entry->started = true;
		}
		std::shared_ptr<const ImageDecoder::Image> image;
		try {
			if (SUCCEEDED(com)) {
				auto decoded = ImageDecoder::Decode(key.path, key.width, key.height, true);
				// At extreme DPI/font sizes a single thumbnail can exceed the entire cache.
				// Keep it within the same aspect-ratio bounding box and memory budget.
				if (decoded && decoded->pixels.size() > MaxBytes) {
					const double ratio = std::sqrt(static_cast<double>(MaxBytes - 4) / decoded->pixels.size());
					const UINT width = std::max(1u, static_cast<UINT>(decoded->width * ratio));
					const UINT height = std::max(1u, static_cast<UINT>(decoded->height * ratio));
					decoded.reset();
					decoded = ImageDecoder::Decode(key.path, width, height, true);
				}
				if (decoded) image = std::make_shared<ImageDecoder::Image>(std::move(*decoded));
			}
		} catch (const std::exception&) { }
		std::lock_guard lock(m_mutex);
		auto entry = std::find_if(m_entries.begin(), m_entries.end(), [&](const auto& e) { return e.id == id && e.key == key; });
		if (stop.stop_requested() || entry == m_entries.end()) continue;
		if (image && image->pixels.size() <= MaxBytes) {
			while (m_bytes > MaxBytes - image->pixels.size()) {
				auto oldest = std::find_if(m_entries.begin(), m_entries.end(), [&](const auto& e) { return e.id != id && e.image; });
				if (oldest == m_entries.end()) break;
				Erase(oldest);
			}
			entry->image = std::move(image);
			m_bytes += entry->image->pixels.size();
			entry->state = State::Ready;
		} else entry->state = State::Failed;
		m_sources[key.path] = {entry->state, entry->image ? entry->image->sourceWidth : 0,
			entry->image ? entry->image->sourceHeight : 0};
	}
	if (SUCCEEDED(com)) ::CoUninitialize();
}
}
