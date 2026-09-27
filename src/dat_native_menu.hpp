#pragma once
#include "dat_native_joint.hpp"
#include <string>
#include <string_view>
#include <vector>
namespace melee_web {
enum class NativeMenuKind { Characters, Stages, Main, Title };
struct NativeMenuExport {
    std::string name;
    void* native_data = nullptr;
};
// Owned typed descriptors for the original menu scene table. Every referenced
// graph is decoded; unsupported active data fails before publication. This is
// asset hydration, not evidence that the source menu scene has executed.
class DatNativeMenu {
public:
    DatNativeMenu(std::shared_ptr<const DatArchive>, NativeMenuKind);
    ~DatNativeMenu();
    DatNativeMenu(const DatNativeMenu&)=delete;
    DatNativeMenu& operator=(const DatNativeMenu&)=delete;
    void* descriptor()const noexcept;
    unsigned model_count()const noexcept;
    [[nodiscard]] void* export_data(std::string_view name) const noexcept;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
