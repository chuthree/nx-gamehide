#pragma once
#include <cstddef>
#include <cstdint>

namespace hgm {
enum class ScanResult { Complete, FetchFailed, InvalidPage, LimitReached };
// Fetch receives a raw offset, a page buffer and its capacity. It returns false
// on IPC failure and writes the number of records only on success.
// Storage is bounded independently of the caller's output buffer size.
template<class Record, class Fetch, class Hidden>
ScanResult scanVisible(int32_t offset, Record *out, size_t capacity,
                       uint64_t &total, int32_t &written, uint64_t &removed,
                       Fetch fetch, Hidden hidden) {
    constexpr int32_t Page = 128, MaxRaw = 32768;
    Record page[Page]{};
    total = 0; written = 0; removed = 0;
    if (offset < 0) return ScanResult::InvalidPage;
    if (out && !capacity) return ScanResult::Complete;
    for (int32_t rawOffset = 0; rawOffset < MaxRaw;) {
        int32_t got = 0;
        if (!fetch(rawOffset, page, Page, got)) return ScanResult::FetchFailed;
        if (got < 0 || got > Page) return ScanResult::InvalidPage;
        if (!got) return ScanResult::Complete;
        for (int32_t i = 0; i < got; ++i) {
            if (hidden(page[i].application_id)) { ++removed; continue; }
            if (out && total >= static_cast<uint64_t>(offset) &&
                static_cast<size_t>(written) < capacity) out[written++] = page[i];
            ++total;
        }
        rawOffset += got;
        if (out && static_cast<size_t>(written) == capacity) return ScanResult::Complete;
    }
    return ScanResult::LimitReached;
}
}
