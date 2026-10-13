#pragma once
#include "protocol.hpp"
#include <cerrno>
#include <cstdio>
#include <set>
#include <string>
#include <vector>
#include <unistd.h>

namespace hgm {
inline bool loadPolicyFile(const std::string &path, std::set<uint64_t> &out) {
    FILE *file = std::fopen(path.c_str(), "rb");
    if (!file) {
        const int reason = errno;
        // A missing destination plus .old indicates an interrupted replacement.
        // Refuse to silently overwrite its recovery copy on the next save.
        if (reason == ENOENT) {
            errno = 0;
            const int oldResult = access((path + ".old").c_str(), F_OK);
            if (oldResult != 0 && errno == ENOENT) {
                out.clear(); return true;
            }
        }
        return false;
    }
    std::vector<char> data(MaxPolicyBytes + 1);
    const size_t bytes = std::fread(data.data(), 1, data.size(), file);
    bool ok = !std::ferror(file) && bytes <= MaxPolicyBytes;
    if (std::fclose(file) != 0) ok = false;
    std::vector<uint64_t> ids(MaxHidden);
    size_t count = 0;
    if (!ok || !parsePolicy(data.data(), bytes, ids.data(), count)) {
        return false;
    }
    out = std::set<uint64_t>(ids.begin(), ids.begin() + count);
    return true;
}
inline bool writeSynced(const std::string &path, const void *data, size_t bytes) {
    FILE *file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    bool ok = std::fwrite(data, 1, bytes, file) == bytes;
    if (std::fflush(file) != 0) ok = false;
    if (fsync(fileno(file)) != 0) ok = false;
    if (std::fclose(file) != 0) ok = false;
    return ok;
}
enum class ReplaceResult { Saved, Failed, RecoveryRequired };
// FAT does not guarantee replacement of an existing destination by rename.
// Keep .old until the next transaction and roll back failed installations.
inline ReplaceResult replaceFile(const std::string &path) {
    const std::string old = path + ".old";
    if (unlink(old.c_str()) && errno != ENOENT) return ReplaceResult::Failed;
    const bool existed = rename(path.c_str(), old.c_str()) == 0;
    if (!existed && errno != ENOENT) return ReplaceResult::Failed;
    if (rename((path + ".tmp").c_str(), path.c_str()) == 0) return ReplaceResult::Saved;
    if (existed && rename(old.c_str(), path.c_str())) return ReplaceResult::RecoveryRequired;
    return ReplaceResult::Failed;
}
}
