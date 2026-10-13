#pragma once
#include <cstdint>
#include <cstddef>

namespace hgm {
constexpr const char *Directory = "sdmc:/config/nx-gamehide";
constexpr const char *PolicyPath = "sdmc:/config/nx-gamehide/hidden_games.txt";
constexpr const char *DisablePath = "sdmc:/config/nx-gamehide/disable.flag";
constexpr const char *ModePath = "sdmc:/config/nx-gamehide/boot_mode.txt";
constexpr const char *LogPath = "sdmc:/config/nx-gamehide/boot.log";
constexpr const char *PreviousLogPath = "sdmc:/config/nx-gamehide/boot.previous.log";
constexpr const char *Version = "0.1.0";
constexpr const char *ServiceName = "hgm:ctl";
constexpr size_t MaxHidden = 4096;
constexpr size_t MaxPolicyBytes = MaxHidden * 18;
constexpr uint32_t Protocol = 4;
enum BootMode : uint32_t { DiagnosticsOnly = 0, ForwardOnly = 1, Filter = 2 };
enum Flags : uint32_t { PolicyInvalid = 1, EmergencyDisabled = 2, ModernPathUnknown = 4, MitmUnavailable = 8, ModeInvalid = 16 };
struct Status {
    uint32_t protocol, flags, hos_version, hidden_count;
    uint64_t sessions, manager_getters, list_calls, count_calls, filtered_records;
    uint64_t readonly_getters, forward_fallbacks;
    uint32_t last_result, reserved;
    uint32_t boot_mode, stage, last_command, log_result;
    uint64_t readonly_wrapped, readonly_list_calls;
    uint64_t reloads, update_event_requests;
    uint32_t refresh_result, event_ready;
};
static_assert(sizeof(Status) == 136);
static_assert(offsetof(Status, reloads) == 112);

inline bool isApplicationId(uint64_t id) {
    return id >= UINT64_C(0x0100000000010000) &&
           id <= UINT64_C(0x01FFFFFFFFFFFFFF) && !(id & 0xFFF);
}

// Invalid mode text must never opt in to interception. Missing-file defaults are chosen by the caller.
inline bool parseBootMode(const char *data, size_t size, uint32_t &mode) {
    mode = DiagnosticsOnly;
    if (size && data[size - 1] == '\n') --size;
    if (size && data[size - 1] == '\r') --size;
    if (size != 1 || data[0] < '0' || data[0] > '2') return false;
    mode = static_cast<uint32_t>(data[0] - '0');
    return true;
}

// Accept 16-digit hexadecimal application IDs with CRLF or LF endings.
// Reject the whole policy on malformed input, overlong lines or overflow.
inline bool parsePolicy(const char *data, size_t size, uint64_t *ids, size_t &count) {
    count = 0;
    if (size > MaxPolicyBytes) return false;
    size_t parsed = 0;
    size_t pos = 0;
    while (pos < size) {
        size_t start = pos;
        while (pos < size && data[pos] != '\n') ++pos;
        size_t len = pos - start;
        if (pos < size) ++pos;
        if (len && data[start + len - 1] == '\r') --len;
        if (!len) continue;
        if (len != 16) return false;
        uint64_t id = 0;
        for (size_t j = 0; j < len; ++j) {
            unsigned char c = data[start + j];
            unsigned d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else return false;
            id = (id << 4) | d;
        }
        // Only application base IDs. No system titles, updates or DLC.
        if (!isApplicationId(id)) return false;
        bool exists = false;
        for (size_t j = 0; j < parsed; ++j) if (ids[j] == id) { exists = true; break; }
        if (!exists) {
            if (parsed == MaxHidden) return false;
            ids[parsed++] = id;
        }
    }
    count = parsed;
    return true;
}
}
