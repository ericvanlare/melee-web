#pragma once
#include "dat_material.hpp"
#include "hsd_material_bridge.h"
#include <memory>

namespace melee_web {
using HsdMaterialHandle = std::unique_ptr<MeleeWebHsdMaterial, decltype(&melee_web_hsd_material_destroy)>;
// The scene owns both this handle and the immutable archive backing its images.
HsdMaterialHandle make_hsd_material(const DatMaterial& source);
}
