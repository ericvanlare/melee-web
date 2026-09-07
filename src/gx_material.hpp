#pragma once

#include "dat_texture.hpp"
#include <dolphin/gx.h>

namespace melee_web {
// Stable SDK objects keep Aurora's texture identities/cache reusable across
// frames. The owning scene keeps the archive bytes alive until drawing ends.
struct GxTexture {
    explicit GxTexture(const DatTexture& source);
    DatTexture descriptor;
    GXTexObj texture{};
    GXTlutObj palette{};
    GXTlut palette_slot = GX_TLUT0;
};

// Unlit inspection material, restricted to the operations validated by the DAT
// reader. Full lighting/multitexture belongs in the original HSD material path.
void apply_diffuse_material(GXColor color, GxTexture* texture);
}
