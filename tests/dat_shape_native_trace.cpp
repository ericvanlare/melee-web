#include "dat_native_joint.hpp"
#include "dat_shape_animation.hpp"
#include <fstream>
#include <iostream>

using namespace melee_web;

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw DatError("Expected MnSlMap.usd archive");
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) throw DatError("Stage selection archive is unavailable");
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        auto archive = std::make_shared<DatArchive>(bytes);
        std::optional<uint32_t> root;
        for (const auto& symbol : archive->public_symbols())
            if (symbol.name == "MnSelectStageDataTable") root = symbol.data_offset;
        if (!root) throw DatError("Stage selection table symbol is absent");
        const uint32_t row = *root + 16 + 10 * 16;
        const auto model = archive->pointer(row, 64);
        const auto shape = archive->pointer(row + 12, 12);
        if (!model || !shape) throw DatError("SSS row 10 has no shape model/animation");

        try {
            DatNativeJoint native_model(archive, *model);
            (void) native_model;
        } catch (const DatError& error) {
            std::cerr << "model=" << std::hex << *model << " shape=" << *shape
                      << " decode failed: " << error.what() << std::dec << '\n';
            throw;
        }
        DatNativeJoint native_model(archive, *model);
        const auto& graph = native_model.graph();
        unsigned shape_pobjs = 0;
        uint32_t first_vertex_value = 0;
        for (uint32_t i = 0; i < graph.pobj_count; ++i) {
            if (!graph.pobjs[i].shape) continue;
            ++shape_pobjs;
            const auto* shape_set = graph.pobjs[i].shape;
            if (shape_set->shape_count != 6 || shape_set->vertex_index_count != 62 ||
                shape_set->normal_index_count == 0 || !shape_set->vertex_index_lists ||
                !shape_set->normal_index_lists)
                throw DatError("SSS row 10 shape metadata is incomplete");
            if (i == 1) {
                const auto* attribute = &graph.pobjs[i].geometry.attributes[shape_set->vertex_attribute];
                // The first indexed POS value is read as big-endian F32 by the
                // DatArchive validator; this catches accidental host-endian
                // interpretation while the native owner keeps source bytes.
                if (attribute->comp_type != 4 || attribute->byte_size < 12)
                    throw DatError("SSS row 10 POS source format changed");
                const auto* p = static_cast<const uint8_t*>(attribute->data);
                first_vertex_value = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
                                      (uint32_t(p[2]) << 8) | p[3];
            }
        }
        if (shape_pobjs != 2 || first_vertex_value == 0)
            throw DatError("SSS row 10 shape PObj inventory or endian proof changed");
        DatShapeAnimation animation(archive, *shape, graph);
        if (!animation.descriptor() || animation.joint_count() != 3 ||
            animation.dobj_count() != 3) {
            std::cerr << "shape counts joints=" << animation.joint_count()
                      << " dobjs=" << animation.dobj_count() << '\n';
            throw DatError("SSS row 10 shape animation topology changed");
        }
        std::cout << "SSS row 10 original shape geometry: 2 PObjs, 3 shape joints, "
                  << "3 shape DObj descriptors, big-endian source scalars passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
