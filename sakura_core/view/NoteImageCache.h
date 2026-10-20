/*
 * Copyright (C) 2026, Sakura Editor Organization
 * SPDX-License-Identifier: Zlib
 */
#pragma once
#include "util/ImageDecoder.h"
#include <condition_variable>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <thread>

namespace NoteView {
struct ImageKey {
	std::wstring path;
	UINT width = 0;
	UINT height = 0;
	bool operator==(const ImageKey&) const = default;
};

// One cache/worker per editor window. Worker code never accesses editor objects or HWNDs.
class ImageCache {
public:
	enum class State { Loading, Ready, Failed };
	struct Result {
		State state;
		std::shared_ptr<const ImageDecoder::Image> image;
	};
	static constexpr size_t MaxEntries = 16;
	static constexpr size_t MaxBytes = 32 * 1024 * 1024;
	ImageCache() = default;
	~ImageCache();
	ImageCache(const ImageCache&) = delete;
	ImageCache& operator=(const ImageCache&) = delete;
	Result Get(const ImageKey& key);
	void Reset();
	size_t ByteCount() const;
	size_t EntryCount() const;
private:
	struct Entry {
		ImageKey key;
		std::uint64_t id;
		State state = State::Loading;
		bool started = false;
		std::shared_ptr<const ImageDecoder::Image> image;
	};
	void Work(std::stop_token stop);
	void Erase(std::list<Entry>::iterator entry);
	mutable std::mutex m_mutex;
	std::condition_variable m_wake;
	std::list<Entry> m_entries;
	size_t m_bytes = 0;
	std::uint64_t m_nextId = 0; // IDs never reused after a document/view reset.
	std::jthread m_worker;
};
}
