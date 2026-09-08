#pragma once
#include "dat_item_registry.hpp"
#include <memory>
namespace melee_web {
/* Owned registration-only Article roots. The arena must outlive the published
 * registry and every fighter/item referring to it. Unresolved graph masks are
 * explicit; this constructor does not establish item creation readiness. */
class DatItemRegistryNative {
public:
    explicit DatItemRegistryNative(std::shared_ptr<const DatArchive>);
    ~DatItemRegistryNative();
    DatItemRegistryNative(const DatItemRegistryNative&)=delete;
    DatItemRegistryNative& operator=(const DatItemRegistryNative&)=delete;
    void* const* articles() const noexcept;
    const std::array<uint32_t,MELEE_WEB_ITEM_REGISTRY_COUNT>& unresolved_masks() const noexcept;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
