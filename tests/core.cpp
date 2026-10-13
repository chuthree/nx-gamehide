#include "protocol.hpp"
#include "filter.hpp"
#include "file_store.hpp"
#include "i18n.hpp"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <random>
#include <cstring>
#include <utility>
using namespace std::string_literals;

namespace fs = std::filesystem;
// Record payload follows the pinned libnx ns.h; filtering must preserve all fields.
struct Record {
    uint64_t application_id;
    uint8_t last_event, attributes, reserved[6];
    uint64_t last_updated;
    bool operator==(const Record &) const = default;
};
int main() {
    using namespace hgm::i18n;
    assert(languageFor("zh-CN") == Language::SimplifiedChinese);
    assert(languageFor("zh-Hans") == Language::SimplifiedChinese);
    assert(languageFor("zh-TW") == Language::TraditionalChinese);
    assert(languageFor("zh-Hant") == Language::TraditionalChinese);
    for (const char *code : {"en-US", "en-GB", "ja", "fr", "", "unknown"})
        assert(languageFor(code) == Language::English);
    for (auto language : {Language::English, Language::SimplifiedChinese, Language::TraditionalChinese}) {
        for (size_t i = 0; i < static_cast<size_t>(Text::Count); ++i)
            assert(text(static_cast<Text>(i), language)[0]);
        char counts[128], prompt[128];
        std::snprintf(counts, sizeof(counts), text(Text::Summary, language), size_t(4096), size_t(4097));
        std::snprintf(prompt, sizeof(prompt), text(Text::RestorePrompt, language), size_t(4096));
        assert(std::strstr(counts, "4096") && std::strstr(counts, "4097"));
        assert(std::strstr(prompt, "4096"));
    }
    uint64_t ids[hgm::MaxHidden];
    size_t count;
    auto parse = [&](const std::string &s) { return hgm::parsePolicy(s.data(), s.size(), ids, count); };
    assert(parse("") && count == 0);
    assert(parse("0100000000010000\r\n0100000000010000\n\n") && count == 1 && ids[0] == 0x0100000000010000ULL);
    assert(parse("01000000000ab000") && count == 1 && ids[0] == 0x01000000000AB000ULL);
    assert(parse("0100000000020000\n0100000000010000") && count == 2);
    assert((std::set<uint64_t>(ids, ids + count) == std::set<uint64_t>{0x0100000000010000ULL, 0x0100000000020000ULL}));
    for (auto s : std::vector<std::string>{"1", "010000000001000G", "0100000000010000\0X"s,
                   "0000000000010000", "FFFFFFFFFFFFF000", "0100000000010800",
                   "0100000000010000 \n"}) assert(!parse(s));
    std::string full;
    for (size_t i = 0; i < hgm::MaxHidden + 1; ++i) {
        char line[18];
        std::snprintf(line, sizeof(line), "%016llX\n", 0x0100000000010000ULL + i * 0x1000);
        full += line;
        if (i + 1 == hgm::MaxHidden) {
            assert(parse(full) && count == hgm::MaxHidden);
            assert(parse(full + "0100000000010000\n") && count == hgm::MaxHidden);
        }
    }
    assert(!parse(full) && count == 0);
    assert(!parse("0100000000010000\nbroken") && count == 0);
    assert(!parse(std::string(hgm::MaxPolicyBytes + 1, '\n')));
    uint32_t mode;
    for (auto [input, expected] : {std::pair{"0", hgm::DiagnosticsOnly},
                                   std::pair{"1\r\n", hgm::ForwardOnly},
                                   std::pair{"2\n", hgm::Filter}}) {
        assert(hgm::parseBootMode(input, std::strlen(input), mode));
        assert(mode == expected);
    }
    for (auto input : {"", "3", "2x", "2\n\n", " 2"}) {
        mode = hgm::Filter;
        assert(!hgm::parseBootMode(input, std::strlen(input), mode));
        assert(mode == hgm::DiagnosticsOnly);
    }

    auto checkPagination = [](size_t size, auto hidden, int32_t offset,
                              size_t capacity, size_t fetchLimit) {
        std::vector<Record> raw, expected;
        for (size_t i = 0; i < size; ++i) {
            Record record{i, uint8_t(i % 256), uint8_t(i % 3), {1, 2, 3, 4, 5, 6}, 1000 + i};
            raw.push_back(record);
            if (!hidden(record.application_id)) expected.push_back(record);
        }
        Record sentinel{};
        sentinel.application_id = UINT64_MAX;
        std::vector<Record> output(capacity + 1, sentinel);
        uint64_t total, removed;
        int32_t written;
        auto fetch = [&](int32_t start, Record *page, size_t cap, int32_t &got) {
            assert(start >= 0 && static_cast<size_t>(start) <= raw.size());
            got = std::min({cap, fetchLimit, raw.size() - static_cast<size_t>(start)});
            std::copy_n(raw.begin() + start, got, page);
            return true;
        };
        assert(hgm::scanVisible(offset, output.data(), capacity, total, written, removed, fetch, hidden) == hgm::ScanResult::Complete);
        const size_t wanted = static_cast<size_t>(offset) >= expected.size() ? 0 : std::min(capacity, expected.size() - offset);
        assert(static_cast<size_t>(written) == wanted);
        for (size_t i = 0; i < wanted; ++i) assert(output[i] == expected[offset + i]);
        for (size_t i = wanted; i < output.size(); ++i) assert(output[i] == sentinel);
        // Only count-only scans promise the total for the whole catalog.
        assert(hgm::scanVisible<Record>(0, nullptr, 0, total, written, removed, fetch, hidden) == hgm::ScanResult::Complete);
        assert(total == expected.size() && removed == size - expected.size() && written == 0);
    };
    auto noneHidden = [](uint64_t) { return false; };
    auto allHidden = [](uint64_t) { return true; };
    auto hiddenPrefix = [](uint64_t id) { return id < 260; };
    checkPagination(0, noneHidden, 0, 4, 1000);       // empty catalog
    checkPagination(300, noneHidden, 0, 150, 1000);   // output spans raw pages; payload/order preserved
    checkPagination(300, hiddenPrefix, 0, 10, 1000);  // fully hidden pages must not end the visible list
    checkPagination(300, hiddenPrefix, 7, 50, 1000);  // offset applies after filtering; partial final output
    checkPagination(300, allHidden, 0, 4, 1000);
    checkPagination(300, hiddenPrefix, 40, 4, 1000);  // offset at visible EOF
    checkPagination(300, hiddenPrefix, 50, 4, 1000);  // offset beyond visible EOF
    checkPagination(300, hiddenPrefix, 2, 10, 7);     // a short nonempty fetch is not necessarily EOF
    std::mt19937 rng(42);
    for (int trial = 0; trial < 1000; ++trial) {
        const size_t size = rng() % 1500;
        const int32_t offset = rng() % 1600;
        const size_t capacity = 1 + rng() % 300;
        checkPagination(size, [trial](uint64_t id) { return trial % 3 == 0 || id % 7 < 4; },
                        offset, capacity, 1000);
    }
    uint64_t total, removed; int32_t written;
    auto bad = [](int32_t, Record *, size_t cap, int32_t &got) { got = static_cast<int32_t>(cap) + 1; return true; };
    assert(hgm::scanVisible<Record>(0, nullptr, 0, total, written, removed, bad, noneHidden) == hgm::ScanResult::InvalidPage);
    auto negative = [](int32_t, Record *, size_t, int32_t &got) { got = -1; return true; };
    assert(hgm::scanVisible<Record>(0, nullptr, 0, total, written, removed, negative, noneHidden) == hgm::ScanResult::InvalidPage);
    auto failed = [](int32_t, Record *, size_t, int32_t &) { return false; };
    assert(hgm::scanVisible<Record>(0, nullptr, 0, total, written, removed, failed, noneHidden) == hgm::ScanResult::FetchFailed);
    Record output{};
    auto failAfterPage = [](int32_t start, Record *page, size_t, int32_t &got) {
        if (start) return false;
        page[0].application_id = 1;
        got = 1;
        return true;
    };
    assert(hgm::scanVisible(1, &output, 1, total, written, removed, failAfterPage, noneHidden) == hgm::ScanResult::FetchFailed);
    auto forever = [](int32_t, Record *, size_t cap, int32_t &got) { got = static_cast<int32_t>(cap); return true; };
    assert(hgm::scanVisible<Record>(0, nullptr, 0, total, written, removed, forever, noneHidden) == hgm::ScanResult::LimitReached);

    char dir[] = "/tmp/nx-gamehide-tests-XXXXXX";
    assert(mkdtemp(dir)); const std::string path = std::string(dir) + "/policy";
    std::set<uint64_t> policy{123};
    assert(hgm::loadPolicyFile(path, policy) && policy.empty());
    assert(hgm::writeSynced(path + ".tmp", "0100000000010000\n", 17));
    assert(hgm::replaceFile(path) == hgm::ReplaceResult::Saved);
    assert(hgm::loadPolicyFile(path, policy) && policy == std::set<uint64_t>{0x0100000000010000ULL});
    assert(hgm::replaceFile(path) == hgm::ReplaceResult::Failed); // no temp: rollback
    assert(hgm::loadPolicyFile(path, policy) && policy == std::set<uint64_t>{0x0100000000010000ULL});
    assert(hgm::writeSynced(path + ".tmp", "0100000000020000\n", 17));
    assert(hgm::replaceFile(path) == hgm::ReplaceResult::Saved);
    assert(hgm::loadPolicyFile(path, policy) && policy == std::set<uint64_t>{0x0100000000020000ULL});
    assert(hgm::writeSynced(path, "broken", 6)); // external corruption must not replace loaded state
    assert(!hgm::loadPolicyFile(path, policy) && policy == std::set<uint64_t>{0x0100000000020000ULL});
    const std::string oversized(hgm::MaxPolicyBytes + 1, '\n');
    assert(hgm::writeSynced(path, oversized.data(), oversized.size()));
    assert(!hgm::loadPolicyFile(path, policy) && policy == std::set<uint64_t>{0x0100000000020000ULL});
    assert(hgm::writeSynced(path, "0100000000010000\n", 17));
    fs::remove(path + ".old");
    fs::rename(path, path + ".old"); // interrupted replacement: valid recovery copy, no destination
    assert(!hgm::loadPolicyFile(path, policy) && policy == std::set<uint64_t>{0x0100000000020000ULL});
    fs::rename(path + ".old", path);
    assert(hgm::loadPolicyFile(path, policy) && policy == std::set<uint64_t>{0x0100000000010000ULL});
    assert(hgm::writeSynced(path + ".tmp", "0100000000020000\n", 17));
    fs::create_directory(path + ".old"); // cannot unlink a directory: preserve original
    assert(hgm::replaceFile(path) == hgm::ReplaceResult::Failed);
    assert(hgm::loadPolicyFile(path, policy) && policy == std::set<uint64_t>{0x0100000000010000ULL});
    assert(!hgm::writeSynced(path + ".old", "x", 1));
    fs::remove_all(path + ".old");
    assert(hgm::writeSynced(path + ".tmp", "", 0));
    assert(hgm::replaceFile(path) == hgm::ReplaceResult::Saved);
    assert(hgm::loadPolicyFile(path, policy) && policy.empty()); // restore all
    fs::remove_all(dir);
    std::cout << "parser, pagination boundaries, 1000 randomized cases, limits and file recovery passed\n";
}
