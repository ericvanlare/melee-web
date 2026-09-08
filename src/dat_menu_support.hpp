#pragma once

#include "dat_archive.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace melee_web {

enum class DatMenuSupportKind {
    CardIcons,
    CardScene,
};

enum class DatMenuSupportLanguage {
    English,
    Other,
};

// Owns the two archives registered by lbCardGame_LoadArchive. This is the
// narrow CSS card dependency: CardScene's published SceneDesc is safe for the
// exact lb_8001CF18 consumer, which reads model 0/camera 0 and their animation
// descriptors. It is not a general scene-service owner because lights/fogs
// are intentionally left unpublished.
class DatMenuSupport {
public:
    DatMenuSupport(std::shared_ptr<const DatArchive>, DatMenuSupportKind,
                   DatMenuSupportLanguage setting_language =
                       DatMenuSupportLanguage::English,
                   DatMenuSupportLanguage saved_language =
                       DatMenuSupportLanguage::English);
    ~DatMenuSupport();
    DatMenuSupport(const DatMenuSupport&) = delete;
    DatMenuSupport& operator=(const DatMenuSupport&) = delete;

    [[nodiscard]] void* descriptor() const noexcept;
    [[nodiscard]] std::string_view source_basename() const noexcept;
    [[nodiscard]] std::string_view resolved_filename() const noexcept;
    [[nodiscard]] std::string_view symbol_name() const noexcept;

    // CardIcons root: the source table has one entry per image followed by a
    // null pointer. Payloads are the original archive byte spans; their
    // format is intentionally left to the card reader.
    [[nodiscard]] std::size_t icon_count() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t>
    icon_payload(std::size_t index) const;

    // CardScene root: these counts describe the source SceneDesc arrays that
    // are published in descriptor(). CSS consumes model 0 and camera 0.
    [[nodiscard]] std::size_t model_count() const noexcept;
    [[nodiscard]] std::size_t camera_count() const noexcept;
    [[nodiscard]] std::size_t camera_animation_count() const noexcept;

    // The source card path does not dereference SceneDesc lights/fogs. The
    // counts make that boundary explicit instead of silently claiming that
    // the complete general scene service was hydrated.
    [[nodiscard]] std::size_t unconsumed_light_list_count() const noexcept;
    [[nodiscard]] bool has_unconsumed_fog() const noexcept;

    // Mirrors lbFileGetFullName for the names used by lbCardGame_LoadArchive.
    // Existing extensions are preserved; a trailing dot uses the setting
    // locale and a missing extension uses the saved-language locale.
    [[nodiscard]] static std::string
    resolve_filename(std::string_view basename,
                     DatMenuSupportLanguage setting_language,
                     DatMenuSupportLanguage saved_language);

private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};

} // namespace melee_web
