#include "runtime_archive_cache.hpp"

#include <algorithm>

namespace melee_web {

std::shared_ptr<const DatArchive>
RuntimeArchiveCache::archive(std::string_view name, DatExternalPolicy policy)
{
    const auto file = files_.find(name);
    if (file == files_.end() || file->second.empty() ||
        file->second.size() > DatArchive::max_archive_bytes) {
        throw DatError("Missing or invalid runtime file: " + std::string(name));
    }
    const Key key{std::string(name), policy};
    if (const auto cached = entries_.find(key); cached != entries_.end()) {
        if (cached->second.source_data != file->second.data() ||
            cached->second.source_size != file->second.size()) {
            throw DatError("Runtime archive bytes changed while cached: " +
                           std::string(name));
        }
        return cached->second.archive;
    }
    auto parsed = std::make_shared<const DatArchive>(file->second, policy);
    Entry entry;
    entry.archive = parsed;
    entry.baseline.assign(parsed->data().begin(), parsed->data().end());
    entry.source_data = file->second.data();
    entry.source_size = file->second.size();
    entries_.emplace(key, std::move(entry));
    return parsed;
}

void RuntimeArchiveCache::verify(
    const std::shared_ptr<const DatArchive>& archive) const
{
    if (!archive) throw DatError("Cached runtime archive is null");
    for (const auto& [_, entry] : entries_) {
        if (entry.archive != archive) continue;
        const auto bytes = archive->data();
        if (!std::equal(bytes.begin(), bytes.end(), entry.baseline.begin(),
                        entry.baseline.end())) {
            throw DatError("Source mutated an immutable cached runtime archive");
        }
        return;
    }
    throw DatError("Runtime archive does not belong to this cache");
}

} // namespace melee_web
