#include "rigid_model.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using melee_web::DatArchive;
using melee_web::DatError;
using melee_web::RigidModel;
using Bytes = std::vector<std::uint8_t>;

namespace {

void check(bool condition, const char* description)
{
    if (!condition) throw std::runtime_error(description);
}

template <typename F> void rejects(F operation)
{
    try { operation(); }
    catch (const DatError&) { return; }
    throw std::runtime_error("expected DatError");
}

void put16(Bytes& bytes, std::size_t offset, std::uint16_t value)
{
    bytes.at(offset) = static_cast<std::uint8_t>(value >> 8);
    bytes.at(offset + 1) = static_cast<std::uint8_t>(value);
}

void put32(Bytes& bytes, std::size_t offset, std::uint32_t value)
{
    put16(bytes, offset, static_cast<std::uint16_t>(value >> 16));
    put16(bytes, offset + 2, static_cast<std::uint16_t>(value));
}

void putf32(Bytes& bytes, std::size_t offset, float value)
{
    put32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

// A tiny independent HSD asset: one identity joint, one DObj/MObj/PObj,
// diffuse material, indexed XYZ array and a GX surface primitive. Offsets are
// taken from the upstream descriptor layouts. No production parser is mocked.
struct Fixture {
    static constexpr std::uint32_t joint = 64, dobj = 128, mobj = 144,
        material = 168, pobj = 188, descriptors = 212, display = 288;
    Bytes data = Bytes(320, 0);
    std::vector<std::uint32_t> relocations;

    explicit Fixture(bool normals = false, bool index16 = false)
    {
        const std::array<std::int16_t, 9> positions = {-2, -1, 0, 2, -1, 0, 0, 3, 1};
        for (std::size_t i = 0; i < positions.size(); ++i)
            put16(data, i * 2, static_cast<std::uint16_t>(positions[i]));
        for (std::size_t i = 0; i < 3; ++i) put16(data, 32 + i * 6 + 4, 0x4000);
        for (std::size_t i = 0; i < 3; ++i) putf32(data, joint + 32 + i * 4, 1.F);
        link(joint + 16, dobj);
        link(dobj + 8, mobj);
        link(dobj + 12, pobj);
        put32(data, mobj + 4, normals ? 4 : 1); // Lit diffuse requiresNRM; otherwise constant color.
        link(mobj + 12, material);
        data[material + 4] = 0x12;
        data[material + 5] = 0x34;
        data[material + 6] = 0x56;
        data[material + 7] = 0xff;
        putf32(data, material + 12, 1.F);
        link(pobj + 8, descriptors);
        put16(data, pobj + 14, 1); // Display size is one 32-byte unit.
        link(pobj + 16, display);
        attribute(descriptors, 9, index16 ? 3 : 2, 1, 3, 1, 6, 0);
        if (normals) {
            attribute(descriptors + 24, 10, index16 ? 3 : 2, 0, 3, 14, 6, 32);
            put32(data, descriptors + 48, 255);
        } else put32(data, descriptors + 24, 255);
        data[display] = 0x90;
        put16(data, display + 1, 3);
        std::size_t cursor = display + 3;
        for (std::uint8_t i = 0; i < 3; ++i) {
            if (index16) data[cursor++] = 0;
            data[cursor++] = i;
            if (normals) {
                if (index16) data[cursor++] = 0;
                data[cursor++] = i;
            }
        }
    }

    void link(std::uint32_t slot, std::uint32_t target)
    {
        put32(data, slot, target);
        if (std::find(relocations.begin(), relocations.end(), slot) == relocations.end())
            relocations.push_back(slot);
    }

    void unlink(std::uint32_t slot)
    {
        put32(data, slot, 0);
        std::erase(relocations, slot);
    }

    void attribute(std::uint32_t offset, std::uint32_t attr, std::uint32_t mode,
                   std::uint32_t count, std::uint32_t type, std::uint8_t frac,
                   std::uint16_t stride, std::uint32_t array)
    {
        put32(data, offset, attr);
        put32(data, offset + 4, mode);
        put32(data, offset + 8, count);
        put32(data, offset + 12, type);
        data[offset + 16] = frac;
        put16(data, offset + 18, stride);
        link(offset + 20, array);
    }

    std::shared_ptr<const DatArchive> archive() const
    {
        const std::string name = "fixture_joint";
        const auto public_offset = 0x20 + data.size() + relocations.size() * 4;
        const auto names_offset = public_offset + 8;
        Bytes bytes(names_offset + name.size() + 1, 0);
        put32(bytes, 0, static_cast<std::uint32_t>(bytes.size()));
        put32(bytes, 4, static_cast<std::uint32_t>(data.size()));
        put32(bytes, 8, static_cast<std::uint32_t>(relocations.size()));
        put32(bytes, 12, 1);
        std::copy(data.begin(), data.end(), bytes.begin() + 0x20);
        for (std::size_t i = 0; i < relocations.size(); ++i)
            put32(bytes, 0x20 + data.size() + i * 4, relocations[i]);
        put32(bytes, public_offset, joint);
        std::copy(name.begin(), name.end(), bytes.begin() + static_cast<std::ptrdiff_t>(names_offset));
        return std::make_shared<DatArchive>(bytes);
    }

    RigidModel model() const { return RigidModel(archive(), "fixture_joint"); }
};

void valid_zero_offset_array()
{
    const auto model = Fixture().model();
    check(model.meshes.size() == 1 && model.draw_packets == 1 && model.submitted_vertices == 3,
          "one triangle packet traverses the complete descriptor graph");
    check(model.minimum == std::array<float, 3>{-1.F, -.5F, 0.F} &&
          model.maximum == std::array<float, 3>{1.F, 1.5F, .5F}, "signed fixed-point bounds");
    const auto& mesh = model.meshes[0];
    check(mesh.material->diffuse == std::array<std::uint8_t, 4>{0x12, 0x34, 0x56, 0xff}, "material color");
    check(mesh.attributes.size() == 1 && mesh.attributes[0].byte_size == 18,
          "required indexed array span");
    check(mesh.attributes[0].data == model.archive->data().data(),
          "relocated zero is a valid owned vertex-array base");
    check(mesh.display == model.archive->data().data() + Fixture::display,
          "display list retains an owned original-byte view");
}

void normals_and_index_widths()
{
    for (const bool index16 : {false, true}) {
        const auto model = Fixture(true, index16).model();
        check(model.meshes[0].attributes.size() == 2, "position and normal streams");
        check(model.meshes[0].attributes[1].byte_size == 18, "all indexed normals are bounded");
        check(model.maximum[1] == 1.5F && model.submitted_vertices == 3,
              "vertex packets account for both attribute indices and index widths");
    }
}

void valid_f32_geometry()
{
    Fixture fixture;
    fixture.attribute(Fixture::descriptors, 9, 2, 1, 4, 0, 12, 0);
    const std::array<float, 9> positions = {-2.F, -1.F, 0.F, 2.F, -1.F, 0.F, 0.F, 3.F, 1.F};
    for (std::size_t i = 0; i < positions.size(); ++i) putf32(fixture.data, i * 4, positions[i]);
    const auto model = fixture.model();
    check(model.minimum[0] == -2.F && model.maximum[1] == 3.F &&
          model.meshes[0].attributes[0].byte_size == 36, "binary32 coordinates and stride");
}

void signed_byte_geometry()
{
    for (bool index16 : {false, true}) {
        Fixture fixture(true, index16);
        fixture.attribute(Fixture::descriptors, 9, index16 ? 3 : 2, 1, 1, 4, 3, 0);
        fixture.attribute(Fixture::descriptors + 24, 10, index16 ? 3 : 2, 0, 1, 6, 3, 32);
        const std::array<std::uint8_t, 9> positions = {0x80, 0xf0, 0, 0x7f, 0xf0, 0, 0, 0x30, 0x10};
        std::copy(positions.begin(), positions.end(), fixture.data.begin());
        for (std::size_t i = 0; i < 3; ++i) {
            fixture.data[32 + i * 3] = fixture.data[33 + i * 3] = 0;
            fixture.data[34 + i * 3] = 64;
        }
        const auto model = fixture.model();
        check(model.minimum == std::array<float, 3>{-8.F, -1.F, 0.F} &&
              model.maximum == std::array<float, 3>{7.9375F, 3.F, 1.F},
              "signed8 extremes retain source fractional position scale");
        check(model.meshes[0].attributes[0].byte_size == 9 &&
              model.meshes[0].attributes[1].byte_size == 9,
              "signed8 position and normal arrays retain exact byte spans");
        put16(fixture.data, Fixture::descriptors + 18, 2);
        rejects([&] { (void) fixture.model(); });
    }
}

void display_commands()
{
    for (const auto opcode : {0x08, 0x48, 0x61, 0x91, 0xa8, 0xb8}) {
        Fixture fixture;
        fixture.data[Fixture::display] = static_cast<std::uint8_t>(opcode);
        rejects([&] { (void) fixture.model(); });
    }
    Fixture fixture;
    fixture.data[Fixture::display + 10] = 0x90;
    rejects([&] { (void) fixture.model(); });
    std::fill(fixture.data.begin() + Fixture::display, fixture.data.end(), 0);
    rejects([&] { (void) fixture.model(); });
    put16(fixture.data, Fixture::pobj + 14, 0);
    rejects([&] { (void) fixture.model(); });
}

void surface_counts()
{
    for (const auto& [opcode, count] :
         std::vector<std::pair<std::uint8_t, std::uint16_t>>{{0x80, 3}, {0x80, 5},
             {0x90, 0}, {0x90, 4}, {0x98, 2}, {0xa0, 1}}) {
        Fixture fixture;
        fixture.data[Fixture::display] = opcode;
        put16(fixture.data, Fixture::display + 1, count);
        rejects([&] { (void) fixture.model(); });
    }
    for (const auto opcode : {0x80, 0x90, 0x98, 0xa0}) {
        Fixture fixture;
        fixture.data[Fixture::display] = static_cast<std::uint8_t>(opcode);
        if (opcode == 0x80) put16(fixture.data, Fixture::display + 1, 4);
        check(fixture.model().draw_packets == 1, "each allowed VAT0 surface primitive");
    }
}

void truncated_packets()
{
    Fixture fixture(false, true);
    put16(fixture.data, Fixture::display + 1, 15);
    rejects([&] { (void) fixture.model(); });
    // A valid 28-index quad packet occupies 31 bytes, leaving room for only
    // the next opcode, not its required two-byte count.
    fixture = Fixture();
    fixture.data[Fixture::display] = 0x80;
    put16(fixture.data, Fixture::display + 1, 28);
    for (std::size_t i = 0; i < 28; ++i)
        fixture.data[Fixture::display + 3 + i] = static_cast<std::uint8_t>(i % 3);
    fixture.data[Fixture::display + 31] = 0x90;
    rejects([&] { (void) fixture.model(); });
}

void indexed_array_bounds()
{
    Fixture fixture;
    fixture.data[Fixture::display + 3] = 255;
    rejects([&] { (void) fixture.model(); });
    fixture = Fixture(false, true);
    put16(fixture.data, Fixture::display + 3, 0xffff);
    rejects([&] { (void) fixture.model(); });
    fixture = Fixture(true);
    fixture.data[Fixture::display + 4] = 255;
    rejects([&] { (void) fixture.model(); });
    fixture = Fixture();
    fixture.data[Fixture::display + 3] = 50; // Inside DAT, but in the display-list region.
    rejects([&] { (void) fixture.model(); });
    fixture = Fixture(true);
    fixture.data[Fixture::display + 3] = 6; // Position read would enter the separate normal array.
    rejects([&] { (void) fixture.model(); });
    fixture = Fixture();
    fixture.unlink(Fixture::descriptors + 20);
    rejects([&] { (void) fixture.model(); });
    fixture.link(Fixture::descriptors + 20, 1);
    rejects([&] { (void) fixture.model(); });
}

void cyclic_graphs()
{
    Fixture fixture;
    fixture.link(Fixture::dobj + 4, Fixture::dobj);
    rejects([&] { (void) fixture.model(); });
    fixture = Fixture();
    fixture.link(Fixture::pobj + 4, Fixture::pobj);
    rejects([&] { (void) fixture.model(); });
}

void raw_joint_srt()
{
    Fixture fixture;
    const std::array<float, 3> rotation{.25F, -.5F, .75F};
    const std::array<float, 3> scale{2.F, 3.F, 4.F};
    const std::array<float, 3> translation{-5.F, 6.F, 7.F};
    constexpr std::uint32_t flags = 0x10040088;
    put32(fixture.data, Fixture::joint + 4, flags);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        putf32(fixture.data, Fixture::joint + 20 + axis * 4, rotation[axis]);
        putf32(fixture.data, Fixture::joint + 32 + axis * 4, scale[axis]);
        putf32(fixture.data, Fixture::joint + 44 + axis * 4, translation[axis]);
    }
    const auto model = fixture.model();
    check(model.joints.size() == 1, "one decoded joint");
    const auto& node = model.joints[0];
    check(node.descriptor_offset == Fixture::joint &&
          node.parent == melee_web::RigidJoint::no_parent && node.flags == flags,
          "root identity and original flags survive typed decoding");
    check(node.rotation == rotation && node.scale == scale && node.translation == translation,
          "Euler SRT is preserved for original HSD transform evaluation");
    check(model.meshes[0].joint_index == 0 &&
          model.meshes[0].minimum == std::array<float, 3>{-1.F, -.5F, 0.F} &&
          model.meshes[0].maximum == std::array<float, 3>{1.F, 1.5F, .5F},
          "mesh bounds stay local; the decoder must not apply SRT a second time");
}

void add_joint(Fixture& fixture, std::uint32_t offset)
{
    fixture.data.resize(std::max(fixture.data.size(), std::size_t(offset) + 64), 0);
    for (std::size_t axis = 0; axis < 3; ++axis)
        putf32(fixture.data, offset + 32 + axis * 4, 1.F);
}

void joint_hierarchy()
{
    Fixture fixture;
    constexpr std::uint32_t child = 320, sibling = 384, grandchild = 448, root_sibling = 512;
    for (const auto offset : {child, sibling, grandchild, root_sibling}) {
        add_joint(fixture, offset);
        // These are separate joint instances of the same immutable geometry.
        fixture.link(offset + 16, Fixture::dobj);
    }
    fixture.link(Fixture::joint + 8, child);
    fixture.link(Fixture::joint + 12, root_sibling);
    fixture.link(child + 8, grandchild);
    fixture.link(child + 12, sibling);
    putf32(fixture.data, child + 44, 3.F);
    putf32(fixture.data, sibling + 44, -3.F);
    const auto model = fixture.model();
    check(model.joints.size() == 5 && model.meshes.size() == 5 &&
          model.draw_packets == 5 && model.submitted_vertices == 15,
          "five distinct joints can share one DObj/PObj/vertex/display data graph");
    const auto index_of = [&](std::uint32_t offset) {
        const auto it = std::find_if(model.joints.begin(), model.joints.end(),
            [&](const auto& node) { return node.descriptor_offset == offset; });
        check(it != model.joints.end(), "expected joint descriptor retained");
        return static_cast<std::uint32_t>(it - model.joints.begin());
    };
    const auto root_index = index_of(Fixture::joint);
    const auto child_index = index_of(child);
    const auto sibling_index = index_of(sibling);
    const auto grandchild_index = index_of(grandchild);
    check(model.joints[root_index].parent == melee_web::RigidJoint::no_parent &&
          model.joints[index_of(root_sibling)].parent == melee_web::RigidJoint::no_parent,
          "root siblings have no parent");
    check(model.joints[child_index].parent == root_index &&
          model.joints[sibling_index].parent == root_index &&
          model.joints[grandchild_index].parent == child_index,
          "a sibling inherits the shared parent, not its preceding sibling");
    check(root_index < child_index && root_index < sibling_index && child_index < grandchild_index,
          "parents precede children for original HSD matrix evaluation");
    check(model.joints[child_index].translation[0] == 3.F &&
          model.joints[sibling_index].translation[0] == -3.F,
          "shared geometry retains each owning joint's independent transform");
    std::vector<bool> owns_geometry(model.joints.size(), false);
    for (const auto& mesh : model.meshes) {
        check(mesh.joint_index < model.joints.size() && !owns_geometry[mesh.joint_index],
              "each decoded mesh records exactly its owning joint");
        owns_geometry[mesh.joint_index] = true;
        check(mesh.display == model.meshes[0].display &&
              mesh.material == model.meshes[0].material &&
              mesh.attributes[0].data == model.meshes[0].attributes[0].data &&
              mesh.minimum == model.meshes[0].minimum && mesh.maximum == model.meshes[0].maximum,
              "repeated geometry keeps immutable shared bytes and local bounds");
    }
}

void invalid_joint_graphs()
{
    for (const auto offset : {8U, 12U, 60U}) {
        Fixture fixture;
        fixture.link(Fixture::joint + offset, Fixture::joint);
        rejects([&] { (void) fixture.model(); });
    }
    // Two parent chains may share geometry, but sharing a joint descriptor is
    // ambiguous parentage and requires HSD instance semantics we do not support.
    Fixture shared;
    add_joint(shared, 320);
    add_joint(shared, 384);
    shared.link(Fixture::joint + 8, 320);
    shared.link(Fixture::joint + 12, 384);
    shared.link(384 + 8, 320);
    rejects([&] { (void) shared.model(); });
    for (const auto offset : {20U, 32U, 44U}) {
        for (const auto bits : {0x7f800000U, 0xff800000U, 0x7fc00001U}) {
            Fixture fixture;
            add_joint(fixture, 320);
            fixture.link(Fixture::joint + 8, 320);
            put32(fixture.data, 320 + offset, bits);
            rejects([&] { (void) fixture.model(); });
        }
    }
    for (const auto flag : {1U << 5, 1U << 9, 1U << 12, 1U << 17, 1U << 23}) {
        Fixture fixture;
        put32(fixture.data, Fixture::joint + 4, flag);
        rejects([&] { (void) fixture.model(); });
    }
    Fixture custom_class;
    custom_class.link(Fixture::joint, Fixture::material);
    rejects([&] { (void) custom_class.model(); });
}

void indexed_uv_geometry()
{
    for (const auto type : {0U, 3U, 4U}) {
        Fixture fixture;
        constexpr std::uint32_t uv_array = 32;
        const bool floats = type == 4, unsigned_bytes = type == 0;
        const auto stride = std::uint16_t(unsigned_bytes ? 2 : floats ? 12 : 6);
        fixture.attribute(Fixture::descriptors + 24, 13, 3, 1,
                          type, unsigned_bytes ? 7 : floats ? 0 : 2, stride, uv_array);
        put32(fixture.data, Fixture::descriptors + 48, 255);
        const std::array<float, 6> uv = {-.5F, .25F, 1.5F, 2.F, 0.F, -1.F};
        for (std::size_t i = 0; i < uv.size(); ++i) {
            const auto offset = uv_array + (i / 2) * stride + (i % 2) * (unsigned_bytes ? 1 : floats ? 4 : 2);
            if (unsigned_bytes) {
                const std::array<std::uint8_t, 6> encoded{0, 32, 64, 128, 16, 112};
                fixture.data[offset] = encoded[i];
            } else if (floats) putf32(fixture.data, offset, uv[i]);
            else put16(fixture.data, offset, static_cast<std::uint16_t>(static_cast<std::int16_t>(uv[i] * 4)));
        }
        std::fill(fixture.data.begin() + Fixture::display + 3, fixture.data.end(), 0);
        const std::array<std::uint16_t, 3> uv_indices{2, 0, 1};
        for (std::size_t vertex = 0; vertex < 3; ++vertex) {
            fixture.data[Fixture::display + 3 + vertex * 3] = static_cast<std::uint8_t>(vertex);
            put16(fixture.data, Fixture::display + 4 + vertex * 3, uv_indices[vertex]);
        }
        const auto model = fixture.model();
        const auto& mesh = model.meshes[0];
        check(mesh.material->textures.empty() && mesh.attributes.size() == 2 && mesh.attributes[1].attr == 13,
              "an untextured material may retain an unused TEX0 vertex stream");
        check(model.draw_packets == 1 && model.submitted_vertices == 3 &&
              mesh.attributes[0].byte_size == 18 &&
              mesh.attributes[1].byte_size == (unsigned_bytes ? 6U : floats ? 32U : 16U),
              "mixed POS8/UV16 indices use ST width and preserve padded array strides");
        if (unsigned_bytes)
            check(mesh.attributes[1].comp_type == 0 && mesh.attributes[1].frac == 7 &&
                  mesh.attributes[1].stride == 2,
                  "U8 frac7 ST coordinates retain their original compact format");
        check(mesh.attributes[1].data == model.archive->data().data() + uv_array &&
              mesh.minimum == std::array<float, 3>{-1.F, -.5F, 0.F} &&
              mesh.maximum == std::array<float, 3>{1.F, 1.5F, .5F},
              "UV bytes remain big-endian and do not alter position bounds");
        put16(fixture.data, Fixture::display + 4, 16);
        rejects([&] { (void) fixture.model(); }); // UV index enters the joint descriptor.
        put16(fixture.data, Fixture::display + 4, 2);
        if (floats) {
            put32(fixture.data, uv_array, 0x7fc00001U);
            rejects([&] { (void) fixture.model(); });
        }
    }
    Fixture fixture;
    fixture.attribute(Fixture::descriptors + 24, 13, 2, 0, 3, 2, 4, 32);
    put32(fixture.data, Fixture::descriptors + 48, 255);
    rejects([&] { (void) fixture.model(); }); // One-component texture coordinates are unsupported.
    fixture.attribute(Fixture::descriptors + 24, 13, 2, 1, 3, 2, 3, 32);
    rejects([&] { (void) fixture.model(); }); // A stride cannot truncate ST components.
}

void materials_and_polygon_modes()
{
    for (const auto offset : {0U, 8U, 16U, 20U}) {
        Fixture fixture;
        fixture.link(Fixture::mobj + offset, Fixture::material);
        rejects([&] { (void) fixture.model(); });
    }
    for (const auto render_mode : {2U, 0x2004U, 0x80000004U}) {
        Fixture fixture;
        put32(fixture.data, Fixture::mobj + 4, render_mode);
        rejects([&] { (void) fixture.model(); });
    }
    Fixture fixture;
    putf32(fixture.data, Fixture::material + 12, .5F);
    rejects([&] { (void) fixture.model(); });
    for (const auto flags : {8U, 0x1000U, 0x2000U}) {
        fixture = Fixture();
        put16(fixture.data, Fixture::pobj + 12, static_cast<std::uint16_t>(flags));
        rejects([&] { (void) fixture.model(); });
    }
    fixture = Fixture();
    fixture.link(Fixture::pobj + 20, Fixture::joint);
    rejects([&] { (void) fixture.model(); });
}

Fixture shape_fixture()
{
    Fixture fixture;
    constexpr std::uint32_t shape = 320, index_table = 352, indices0 = 360,
        indices1 = 364;
    fixture.data.resize(400, 0);
    put16(fixture.data, Fixture::pobj + 12, 0x1000);
    fixture.link(Fixture::pobj + 20, shape);
    put16(fixture.data, shape, 1);       // SHAPESET_AVERAGE.
    put16(fixture.data, shape + 2, 2);   // Two source shape positions.
    put32(fixture.data, shape + 4, 3);   // Three blended position entries.
    fixture.link(shape + 8, Fixture::descriptors);
    fixture.link(shape + 12, index_table);
    fixture.link(index_table, indices0);
    fixture.link(index_table + 4, indices1);
    for (std::uint32_t i = 0; i < 3; ++i) {
        fixture.data[indices0 + i] = static_cast<std::uint8_t>(i);
        fixture.data[indices1 + i] = static_cast<std::uint8_t>(i);
    }
    return fixture;
}

void shape_geometry_and_logical_bounds()
{
    auto fixture = shape_fixture();
    const auto model = RigidModel(fixture.archive(), Fixture::joint, "shape",
                                  melee_web::ModelRenderPass::All,
                                  melee_web::DatMaterialPolicy::NativeDescriptors);
    const auto& mesh = model.meshes[0];
    check(mesh.shape && mesh.shape->flags == 1 && mesh.shape->shape_count == 2 &&
          mesh.shape->vertex_index_count == 3 && mesh.shape->vertex_index_lists.size() == 2,
          "average shape metadata retains its source blend set and index lists");

    fixture = shape_fixture();
    fixture.data[Fixture::display + 3] = 3;
    rejects([&] { (void) RigidModel(fixture.archive(), Fixture::joint, "shape",
                                   melee_web::ModelRenderPass::All,
                                   melee_web::DatMaterialPolicy::NativeDescriptors); });

    fixture = shape_fixture();
    put16(fixture.data, 320, 4); // Neither average nor additive.
    rejects([&] { (void) RigidModel(fixture.archive(), Fixture::joint, "shape",
                                   melee_web::ModelRenderPass::All,
                                   melee_web::DatMaterialPolicy::NativeDescriptors); });
}

void attach_texture(Fixture& fixture, bool reflection, std::uint32_t source)
{
    constexpr std::uint32_t texture = 320, image = 416, pixels = 448;
    fixture.data.resize(480, 0);
    fixture.link(Fixture::mobj + 8, texture);
    put32(fixture.data, Fixture::mobj + 4, reflection ? 0x1c : 0x11);
    put32(fixture.data, texture + 12, source);
    for (std::uint32_t axis = 0; axis < 3; ++axis)
        putf32(fixture.data, texture + 28 + 4 * axis, 1.F);
    fixture.data[texture + 60] = fixture.data[texture + 61] = 1;
    put32(fixture.data, texture + 64, reflection ? 0x30081 : 0x30010);
    putf32(fixture.data, texture + 68, reflection ? .2F : 1.F);
    put32(fixture.data, texture + 72, 1);
    fixture.link(texture + 76, image);
    fixture.link(image, pixels);
    put16(fixture.data, image + 4, 1);
    put16(fixture.data, image + 6, 1); // A complete 1x1 I4 image occupies one32-byte tile.
}

void material_vertex_dependencies()
{
    Fixture constant;
    put32(constant.data, Fixture::mobj + 4, 5);
    check(constant.model().meshes[0].attributes.size() == 1,
          "original channel mode5 uses constant color and does not need normals");
    for (const auto lighting : {4U, 8U, 0xcU}) {
        Fixture invalid;
        put32(invalid.data, Fixture::mobj + 4, lighting);
        rejects([&] { (void) invalid.model(); });
    }
    Fixture fixture(true);
    attach_texture(fixture, true, 5);
    const auto reflection = fixture.model();
    check(reflection.meshes[0].attributes.size() == 2 &&
          reflection.meshes[0].material->textures[0].source == 5 &&
          reflection.meshes[0].material->textures[0].blending == .2F,
          "reflection uses normals even when the preserved source enum namesTEX1");
    fixture = Fixture();
    attach_texture(fixture, true, 5);
    rejects([&] { (void) fixture.model(); });
    fixture = Fixture();
    attach_texture(fixture, false, 4);
    rejects([&] { (void) fixture.model(); });

    fixture = Fixture();
    fixture.attribute(Fixture::descriptors + 24, 20, 2, 1, 0, 7, 2, 32);
    put32(fixture.data, Fixture::descriptors + 48, 255);
    std::fill(fixture.data.begin() + Fixture::display + 3, fixture.data.begin() + Fixture::display + 32, 0);
    for (std::uint8_t index = 0; index < 3; ++index) {
        fixture.data[Fixture::display + 3 + index * 2] = index;
        fixture.data[Fixture::display + 4 + index * 2] = index;
    }
    attach_texture(fixture, false, 11);
    const auto uv7 = fixture.model();
    check(uv7.meshes[0].attributes[1].attr == 20 &&
          uv7.meshes[0].material->textures[0].source == 11,
          "UV texture sourceTEX7 requires and preserves the matching vertex attribute");
    put32(fixture.data, Fixture::descriptors + 24, 13);
    rejects([&] { (void) fixture.model(); });
}

void descriptor_formats()
{
    for (const auto& [offset, value] : std::vector<std::pair<std::uint32_t, std::uint32_t>>{
             {0, 10}, {0, 11}, {0, 255}, {4, 1}, {8, 0}, {12, 0}, {12, 5}}) {
        Fixture fixture;
        put32(fixture.data, Fixture::descriptors + offset, value);
        rejects([&] { (void) fixture.model(); });
    }
    for (const auto stride : {0U, 5U, 256U}) {
        Fixture fixture;
        put16(fixture.data, Fixture::descriptors + 18, static_cast<std::uint16_t>(stride));
        rejects([&] { (void) fixture.model(); });
    }
    Fixture fixture;
    fixture.data[Fixture::descriptors + 16] = 32;
    rejects([&] { (void) fixture.model(); });
    fixture = Fixture();
    put32(fixture.data, Fixture::descriptors + 12, 4); // F32 with nonzero frac.
    rejects([&] { (void) fixture.model(); });
    fixture = Fixture(true);
    put32(fixture.data, Fixture::descriptors + 48, 10); // Missing sentinel.
    rejects([&] { (void) fixture.model(); });
}

void finite_geometry()
{
    for (const auto bits : {0x7f800000U, 0xff800000U, 0x7fc00001U, 0x4b000000U}) {
        Fixture fixture;
        fixture.attribute(Fixture::descriptors, 9, 2, 1, 4, 0, 12, 0);
        put32(fixture.data, 0, bits);
        rejects([&] { (void) fixture.model(); });
        fixture = Fixture(true);
        fixture.attribute(Fixture::descriptors + 24, 10, 2, 0, 4, 0, 12, 24);
        put32(fixture.data, 24, bits);
        rejects([&] { (void) fixture.model(); });
    }
}

// The skin specimen has a skeleton-root mesh and two later bones, so envelope
// references must resolve after traversal. Two palettes exercise both original
// HSD branches: one rigid weight1, one ordered .25/.75 weighted blend.
struct SkinFixture : Fixture {
    static constexpr uint32_t attrs = 320, commands = 416, bone0 = 480,
        bone1 = 544, inverse0 = 608, inverse1 = 656,
        envelope0 = 704, envelope1 = 720, table = 744;
    SkinFixture() : Fixture(true) {
        data.resize(1024, 0);
        put32(data, joint + 4, 0x86); // Skeleton root + envelope model + lighting.
        add_joint(*this, bone0); add_joint(*this, bone1);
        link(joint + 8, bone0); link(bone0 + 12, bone1);
        put32(data, bone0 + 4, 9); put32(data, bone1 + 4, 9);
        link(bone0 + 56, inverse0); link(bone1 + 56, inverse1);
        for (const auto matrix : {inverse0, inverse1})
            for (uint32_t axis = 0; axis < 3; ++axis) putf32(data, matrix + axis * 20, 1.F);
        putf32(data, inverse1 + 12, -3.F);
        link(pobj + 8, attrs); link(pobj + 16, commands); link(pobj + 20, table);
        put16(data, pobj + 12, 0xa001);
        attribute(attrs, 0, 1, 0, 4, 0, 0, 0); unlink(attrs + 20);
        attribute(attrs + 24, 9, 2, 1, 3, 1, 6, 0);
        attribute(attrs + 48, 10, 2, 0, 1, 6, 3, 32);
        put32(data, attrs + 72, 255);
        std::fill(data.begin() + 32, data.begin() + 41, 0);
        for (uint32_t i = 0; i < 3; ++i) data[32 + i * 3 + 2] = 64;
        link(table, envelope0); link(table + 4, envelope1);
        link(envelope0, bone0); putf32(data, envelope0 + 4, 1.F);
        link(envelope1, bone0); putf32(data, envelope1 + 4, .25F);
        link(envelope1 + 8, bone1); putf32(data, envelope1 + 12, .75F);
        data[commands] = 0x90; put16(data, commands + 1, 3);
        for (uint8_t i = 0; i < 3; ++i) {
            data[commands + 3 + i * 3] = i ? 3 : 0;
            data[commands + 4 + i * 3] = i;
            data[commands + 5 + i * 3] = i;
        }
    }
};

void skin_metadata_and_bounds()
{
    const auto model = SkinFixture().model();
    check(model.joints.size() == 3 && model.joints[1].parent == 0 && model.joints[2].parent == 0,
          "skeletal flags and forward envelope references retain preorder parentage");
    const auto& mesh = model.meshes[0];
    check(mesh.descriptor_offset == Fixture::pobj && mesh.flags == 0xa001 &&
          mesh.envelopes.size() == 2 && mesh.envelopes[1].influences.size() == 2,
          "original envelope palette order and source polygon flags");
    check(mesh.envelopes[0].influences[0].joint == 1 &&
          mesh.envelopes[1].influences[0].joint == 1 &&
          mesh.envelopes[1].influences[1].joint == 2 &&
          mesh.envelopes[1].influences[0].weight == .25F &&
          mesh.envelopes[1].influences[1].weight == .75F,
          "joint offsets resolve to indices without reordering or normalizing weights");
    check(model.joints[2].inverse_bind && (*model.joints[2].inverse_bind)[3] == -3.F &&
          (*model.joints[2].inverse_bind)[0] == 1.F && (*model.joints[2].inverse_bind)[5] == 1.F,
          "row-major inverse bind floats remain original values");
    check(mesh.attributes[0].attr == 0 && mesh.attributes[0].attr_type == 1 &&
          mesh.attributes[0].data == nullptr && mesh.attributes[0].byte_size == 0 &&
          mesh.attributes[2].comp_type == 1 && mesh.attributes[2].byte_size == 9,
          "direct PN matrix bytes and compact signed-byte normal arrays");
    check(mesh.palette_used_mask == 3 &&
          mesh.palette_minimum[0] == std::array<float, 3>{-1.F, -.5F, 0.F} &&
          mesh.palette_maximum[0] == mesh.palette_minimum[0] &&
          mesh.palette_minimum[1] == std::array<float, 3>{0.F, -.5F, 0.F} &&
          mesh.palette_maximum[1] == std::array<float, 3>{1.F, 1.5F, .5F} &&
          mesh.palette_minimum[2] == std::array<float, 3>{},
          "bounds use only positions submitted through each actual palette slot");
}

void inverse_bind_requirements()
{
    SkinFixture fixture;
    fixture.unlink(SkinFixture::bone0 + 56);
    rejects([&] { (void) fixture.model(); }); // Weighted slot needs both inverse binds.
    fixture.unlink(SkinFixture::table + 4);
    for (uint32_t i = 0; i < 3; ++i) fixture.data[SkinFixture::commands + 3 + i * 3] = 0;
    check(fixture.model().meshes[0].envelopes.size() == 1,
          "skeleton-root weight1 branch uses world matrix without requiring inverse bind");
    put32(fixture.data, Fixture::joint + 4, 0x81); // A skeleton bone is not a skeleton root.
    rejects([&] { (void) fixture.model(); });
    fixture = SkinFixture();
    for (const auto bad : {0x7fc00000U, 0x7f800000U, 0x4b000000U}) {
        put32(fixture.data, SkinFixture::inverse0, bad);
        rejects([&] { (void) fixture.model(); });
    }
    fixture = SkinFixture();
    fixture.link(SkinFixture::bone0 + 56, 1000); // Only24bytes remain in archive.
    rejects([&] { (void) fixture.model(); });
    fixture = SkinFixture();
    fixture.link(SkinFixture::bone0 + 56, SkinFixture::inverse0 + 2);
    rejects([&] { (void) fixture.model(); });
    fixture = SkinFixture();
    fixture.link(1000, SkinFixture::inverse0 + 32); // Referenced region bisects matrix.
    rejects([&] { (void) fixture.model(); });
}

void envelope_reference_validation()
{
    for (const auto weight : {-1.F, 1.01F, INFINITY, NAN}) {
        SkinFixture fixture;
        putf32(fixture.data, SkinFixture::envelope1 + 4, weight);
        rejects([&] { (void) fixture.model(); });
    }
    SkinFixture fixture;
    putf32(fixture.data, SkinFixture::envelope1 + 4, 0.F);
    putf32(fixture.data, SkinFixture::envelope1 + 12, 0.F);
    rejects([&] { (void) fixture.model(); });
    fixture = SkinFixture();
    // Positive nonunit totals keep their original semantics; do not normalize.
    putf32(fixture.data, SkinFixture::envelope1 + 12, .5F);
    check(fixture.model().meshes[0].envelopes[1].influences[1].weight == .5F,
          "parser does not invent a normalization requirement");
    fixture = SkinFixture();
    add_joint(fixture, 800);
    fixture.link(SkinFixture::envelope0, 800); // Wellformed joint outside chosen graph.
    rejects([&] { (void) fixture.model(); });
    fixture = SkinFixture();
    fixture.unlink(SkinFixture::envelope0);
    rejects([&] { (void) fixture.model(); });
    fixture = SkinFixture();
    fixture.unlink(SkinFixture::table);
    rejects([&] { (void) fixture.model(); });
    fixture = SkinFixture();
    fixture.link(SkinFixture::envelope1 + 16, SkinFixture::bone0);
    putf32(fixture.data, SkinFixture::envelope1 + 20, .5F);
    rejects([&] { (void) fixture.model(); }); // Influence list crosses next table region.
    fixture = SkinFixture();
    fixture.link(1000, SkinFixture::table + 8);
    rejects([&] { (void) fixture.model(); }); // Table lacks terminator inside referenced span.
    fixture = SkinFixture();
    put32(fixture.data, Fixture::joint + 4, 0x80);
    rejects([&] { (void) fixture.model(); }); // No skeleton ancestor for mesh transform.
}

void envelope_resource_limits()
{
    SkinFixture fixture;
    constexpr uint32_t table = 1024, influences = 1080;
    fixture.data.resize(1400, 0);
    fixture.link(Fixture::pobj + 20, table);
    for (uint32_t i = 0; i < 11; ++i) fixture.link(table + i * 4, SkinFixture::envelope0);
    rejects([&] { (void) fixture.model(); }); //11palette entries.
    fixture.unlink(table + 10 * 4);
    check(fixture.model().meshes[0].envelopes.size() == 10, "ten palette slots are supported");
    fixture.link(table, influences);
    for (uint32_t i = 0; i < 32; ++i) {
        fixture.link(influences + i * 8, SkinFixture::bone0);
        putf32(fixture.data, influences + i * 8 + 4, 1.F / 32);
    }
    rejects([&] { (void) fixture.model(); });
    fixture.unlink(influences + 31 * 8);
    check(fixture.model().meshes[0].envelopes[0].influences.size() == 31,
          "original HSD performance counter supports at most31 influences");
}

void matrix_index_validation()
{
    for (const auto value : {1U, 2U, 6U, 30U, 255U}) {
        SkinFixture fixture;
        fixture.data[SkinFixture::commands + 3] = static_cast<uint8_t>(value);
        rejects([&] { (void) fixture.model(); });
    }
    SkinFixture fixture;
    put32(fixture.data, SkinFixture::attrs + 4, 2);
    rejects([&] { (void) fixture.model(); }); // Matrix indices are immediate bytes, not arrays.
    fixture = SkinFixture();
    fixture.link(SkinFixture::attrs + 20, 0);
    rejects([&] { (void) fixture.model(); });
    fixture = SkinFixture();
    put16(fixture.data, SkinFixture::attrs + 18, 1);
    rejects([&] { (void) fixture.model(); }); // Direct matrix bytes have no array stride.
    fixture = SkinFixture();
    fixture.link(Fixture::pobj + 8, SkinFixture::attrs + 24);
    rejects([&] { (void) fixture.model(); }); // Envelope with no PN index stream.
    fixture = SkinFixture();
    constexpr uint32_t attrs = 800;
    fixture.link(Fixture::pobj + 8, attrs);
    fixture.attribute(attrs, 0, 1, 0, 4, 0, 0, 0); fixture.unlink(attrs + 20);
    fixture.attribute(attrs + 24, 1, 1, 0, 4, 0, 0, 0); fixture.unlink(attrs + 44);
    fixture.attribute(attrs + 48, 9, 2, 1, 3, 1, 6, 0);
    fixture.attribute(attrs + 72, 10, 2, 0, 1, 6, 3, 32);
    put32(fixture.data, attrs + 96, 255);
    std::fill(fixture.data.begin() + SkinFixture::commands + 3,
              fixture.data.begin() + SkinFixture::commands + 32, 0);
    for (uint8_t i = 0; i < 3; ++i) {
        const auto cursor = SkinFixture::commands + 3 + 4 * i;
        fixture.data[cursor] = i ? 3 : 0;
        fixture.data[cursor + 1] = 30;
        fixture.data[cursor + 2] = fixture.data[cursor + 3] = i;
    }
    check(fixture.model().meshes[0].attributes[1].attr == 1,
          "direct texture matrix stream participates in the original packet layout");
    for (const auto value : {0U, 31U, 36U, 60U, 255U}) {
        fixture.data[SkinFixture::commands + 4] = static_cast<uint8_t>(value);
        rejects([&] { (void) fixture.model(); });
    }
    fixture.data[SkinFixture::commands + 4] = 30;
    fixture.data[SkinFixture::commands] = 0x20; // Indexed XF matrix-load command not needed by this path.
    rejects([&] { (void) fixture.model(); });
}

void envelope_lighting_contract()
{
    for (const auto mode : {4U, 8U, 0xcU}) {
        SkinFixture fixture;
        put32(fixture.data, Fixture::mobj + 4, mode);
        put32(fixture.data, Fixture::joint + 4, 6); // Skeleton root without LIGHTING.
        rejects([&] { (void) fixture.model(); });
        put32(fixture.data, Fixture::joint + 4, 0x86);
        check(fixture.model().joints[0].flags == 0x86,
              "lit envelope owner retains the source lighting flag for normal palette uploads");
    }
    for (const auto mode : {1U, 5U}) {
        SkinFixture fixture;
        put32(fixture.data, Fixture::mobj + 4, mode);
        put32(fixture.data, Fixture::joint + 4, 6);
        check(fixture.model().joints[0].flags == 6,
              "original constant channel modes do not require unused normal palette uploads");
    }
}

void dobj_preorder_mapping()
{
    Fixture fixture;
    fixture.data.resize(360);
    constexpr uint32_t second_dobj = 320, second_pobj = 336;
    std::copy_n(fixture.data.begin() + Fixture::pobj, 24, fixture.data.begin() + second_pobj);
    fixture.link(second_pobj + 8, Fixture::descriptors);
    fixture.link(second_pobj + 16, Fixture::display);
    fixture.link(Fixture::pobj + 4, second_pobj);
    fixture.link(Fixture::dobj + 4, second_dobj);
    fixture.link(second_dobj + 8, Fixture::mobj);
    fixture.link(second_dobj + 12, second_pobj);
    const auto model = fixture.model();
    check(model.dobj_count == 2 && model.meshes.size() == 3 &&
          model.meshes[0].dobj_index == 0 && model.meshes[1].dobj_index == 0 &&
          model.meshes[2].dobj_index == 1,
          "visibility indexes DObj occurrences, not individual or shared PObj descriptors");
}

void active_texture_matrix_contract()
{
    SkinFixture fixture;
    fixture.data.resize(1664);
    constexpr uint32_t attrs = 1024, texture = 1280, image = 1376,
        pixels = 1408, uv = 1440, reflected = 1536;
    fixture.link(Fixture::pobj + 8, attrs);
    fixture.attribute(attrs, 0, 1, 0, 4, 0, 0, 0); fixture.unlink(attrs + 20);
    fixture.attribute(attrs + 24, 1, 1, 0, 4, 0, 0, 0); fixture.unlink(attrs + 44);
    fixture.attribute(attrs + 48, 9, 2, 1, 3, 1, 6, 0);
    fixture.attribute(attrs + 72, 10, 2, 0, 1, 6, 3, 32);
    fixture.attribute(attrs + 96, 20, 2, 1, 0, 7, 2, uv);
    put32(fixture.data, attrs + 120, 255);
    fixture.link(Fixture::mobj + 8, texture);
    put32(fixture.data, Fixture::mobj + 4, 0x14);
    put32(fixture.data, texture + 8, 7); // Resource assignment replaces this source map ID.
    put32(fixture.data, texture + 12, 11); // TEX7 source still gets TexGen0 in a UV-only chain.
    for (uint32_t axis = 0; axis < 3; ++axis) putf32(fixture.data, texture + 28 + axis * 4, 1.F);
    fixture.data[texture + 60] = fixture.data[texture + 61] = 1;
    put32(fixture.data, texture + 64, 0x30010);
    putf32(fixture.data, texture + 68, 1.F);
    fixture.link(texture + 76, image); fixture.link(image, pixels);
    put16(fixture.data, image + 4, 1); put16(fixture.data, image + 6, 1);
    std::fill(fixture.data.begin() + SkinFixture::commands + 3,
              fixture.data.begin() + SkinFixture::commands + 32, 0);
    for (uint8_t i = 0; i < 3; ++i) {
        const auto cursor = SkinFixture::commands + 3 + i * 5;
        fixture.data[cursor] = i ? 3 : 0;
        fixture.data[cursor + 1] = 30;
        fixture.data[cursor + 2] = fixture.data[cursor + 3] = fixture.data[cursor + 4] = i;
    }
    rejects([&] { (void) fixture.model(); }); // Active TexGen0 would consume an unloaded matrix.
    put32(fixture.data, attrs + 24, 2);
    check(fixture.model().meshes[0].attributes[1].attr == 2,
          "unused TexGen1 matrix stream is harmless even though source coordinates are TEX7");
    // Reflection is assigned TexGen0 before the earlier UV node, and original
    // HSD uploads the entire texture-normal palette for it.
    fixture.link(texture + 4, reflected);
    std::copy_n(fixture.data.begin() + texture, 92, fixture.data.begin() + reflected);
    put32(fixture.data, reflected + 4, 0);
    fixture.link(reflected + 76, image);
    put32(fixture.data, reflected + 12, 5);
    put32(fixture.data, reflected + 64, 0x30081);
    put32(fixture.data, attrs + 24, 1);
    check(fixture.model().meshes[0].material->textures.size() == 2,
          "reflection-first generator assignment supports the loaded matrix palette");
}

void direct_rgba8_geometry()
{
    Fixture fixture;
    put32(fixture.data, Fixture::mobj + 4, 2);
    fixture.attribute(Fixture::descriptors + 24, 11, 1, 1, 5, 0, 4, 0);
    fixture.unlink(Fixture::descriptors + 44);
    put32(fixture.data, Fixture::descriptors + 48, 255);
    for (uint8_t i = 0; i < 3; ++i) {
        const auto cursor = Fixture::display + 3 + i * 5;
        fixture.data[cursor] = i;
        fixture.data[cursor + 1] = 10 + i;
        fixture.data[cursor + 2] = 20 + i;
        fixture.data[cursor + 3] = 30 + i;
        fixture.data[cursor + 4] = 40 + i;
    }
    const auto model = fixture.model();
    const auto& mesh = model.meshes[0];
    check(mesh.attributes.size() == 2 && mesh.attributes[1].attr == 11 &&
          mesh.attributes[1].attr_type == 1 && mesh.attributes[1].data == nullptr &&
          mesh.attributes[1].byte_size == 0 && mesh.material->render_mode == 2,
          "direct RGBA8 stays in the display list with no fabricated vertex array");
    check(mesh.minimum == std::array<float, 3>{-1.F, -.5F, 0.F} &&
          mesh.maximum == std::array<float, 3>{1.F, 1.5F, .5F} && model.submitted_vertices == 3,
          "packet scanner advances over RGBA bytes without corrupting subsequent position indices");
    for (uint8_t i = 0; i < 3; ++i)
        check(static_cast<const uint8_t*>(mesh.display)[3 + i * 5 + 4] == 40 + i, "original alpha bytes survive");
    const auto valid = fixture;
    put16(fixture.data, Fixture::display + 1, 6);
    rejects([&] { (void) fixture.model(); }); // 6*(index+RGBA) exceeds the32-byte display span.
    fixture = valid; put32(fixture.data, Fixture::descriptors + 24, 12);
    rejects([&] { (void) fixture.model(); }); // RENDER_VERTEX needs CLR0, not only CLR1.
    fixture = valid; put32(fixture.data, Fixture::descriptors + 36, 4);
    rejects([&] { (void) fixture.model(); }); // Packed RGBA6 is not this supported packet format.
    fixture = valid; fixture.link(Fixture::descriptors + 44, 0);
    rejects([&] { (void) fixture.model(); }); // DIRECT has no external array even at relocated zero.
}

void direct_rgba4_geometry()
{
    Fixture fixture;
    put32(fixture.data, Fixture::mobj + 4, 2);
    fixture.attribute(Fixture::descriptors + 24, 11, 1, 1, 3, 0, 4, 0);
    fixture.unlink(Fixture::descriptors + 44);
    put32(fixture.data, Fixture::descriptors + 48, 255);
    for (uint8_t i = 0; i < 3; ++i) {
        const auto cursor = Fixture::display + 3 + i * 3;
        fixture.data[cursor] = i;
        fixture.data[cursor + 1] = uint8_t(0x10 + i);
        fixture.data[cursor + 2] = uint8_t(0x20 + i);
    }
    const auto model = RigidModel(fixture.archive(), Fixture::joint, "rgba4",
                                  melee_web::ModelRenderPass::All,
                                  melee_web::DatMaterialPolicy::NativeDescriptors);
    const auto& mesh = model.meshes[0];
    check(mesh.attributes.size() == 2 && mesh.attributes[1].attr == 11 &&
          mesh.attributes[1].attr_type == 1 && mesh.attributes[1].comp_type == 3 &&
          mesh.attributes[1].data == nullptr && mesh.attributes[1].byte_size == 0 &&
          model.submitted_vertices == 3,
          "native direct RGBA4 remains a two-byte display-list color");
    for (uint8_t i = 0; i < 3; ++i)
        check(static_cast<const uint8_t*>(mesh.display)[3 + i * 3 + 2] == uint8_t(0x20 + i),
              "packed RGBA4 bytes survive in the original display list");
    rejects([&] { (void) fixture.model(); });
}

void explicit_opaque_pass()
{
    using melee_web::ModelRenderPass;
    Fixture fixture;
    constexpr uint32_t billboard = 320, empty = 384, retained = 448,
        xlu_dobj = 512, xlu_mobj = 528, edge_dobj = 552, edge_mobj = 568;
    fixture.data.resize(608);
    for (const auto offset : {billboard, empty, retained}) add_joint(fixture, offset);
    fixture.link(Fixture::joint + 8, billboard);
    fixture.link(billboard + 12, empty); fixture.link(empty + 12, retained);
    fixture.link(retained + 16, Fixture::dobj);
    put32(fixture.data, billboard + 4, 0x200); // Unsupported camera-dependent billboard.
    fixture.link(billboard + 16, xlu_dobj);
    fixture.link(xlu_dobj + 8, xlu_mobj); fixture.link(xlu_dobj + 12, Fixture::pobj);
    put32(fixture.data, xlu_mobj + 4, 0x60000001);
    fixture.link(Fixture::dobj + 4, edge_dobj);
    fixture.link(edge_dobj + 8, edge_mobj); fixture.link(edge_dobj + 12, Fixture::pobj);
    put32(fixture.data, edge_mobj + 4, 0x40000001);
    // Omitted materials intentionally have no supported color payload. Selection
    // uses original flags before hydration; strict full loading still rejects.
    rejects([&] { (void) fixture.model(); });
    const auto archive = fixture.archive();
    const RigidModel model(archive, Fixture::joint, "stage entry 3", ModelRenderPass::Opaque);
    check(model.root_offset == Fixture::joint && model.symbol == "stage entry 3" &&
          model.render_pass == ModelRenderPass::Opaque,
          "explicit source root and pass survive independently of public symbol names");
    check(model.meshes.size() == 2 && model.dobj_count == 5 && model.omitted_dobjs == 3 &&
          model.omitted_translucent_meshes == 1 && model.omitted_texture_edge_meshes == 2 &&
          model.omitted_meshes() == 3, "omission counts include original shared DObj occurrences");
    check(model.joints.size() == 2 && model.omitted_joints == 2 &&
          model.joints[1].descriptor_offset == retained && model.joints[1].parent == 0 &&
          model.meshes[0].dobj_index == 0 && model.meshes[1].dobj_index == 3 &&
          model.meshes[1].joint_index == 1,
          "pruning remaps transforms while preserving source identity and flat DObj indices");
    put32(fixture.data, Fixture::joint + 4, 0x200);
    rejects([&] { (void) RigidModel(fixture.archive(), Fixture::joint, "stage", ModelRenderPass::Opaque); });
    put32(fixture.data, Fixture::joint + 4, 0);
    put32(fixture.data, xlu_mobj + 4, 0x20000001);
    rejects([&] { (void) RigidModel(fixture.archive(), Fixture::joint, "stage", ModelRenderPass::Opaque); });
    rejects([&] { (void) RigidModel(archive, Fixture::joint + 1, "unaligned", ModelRenderPass::Opaque); });
}

void opaque_envelope_dependency_closure()
{
    using melee_web::ModelRenderPass;
    SkinFixture fixture;
    constexpr uint32_t unused = 800;
    add_joint(fixture, unused);
    put32(fixture.data, unused + 4, 0x200);
    fixture.link(Fixture::joint + 8, unused);
    fixture.link(unused + 12, SkinFixture::bone0);
    const RigidModel model(fixture.archive(), Fixture::joint, "skin", ModelRenderPass::Opaque);
    check(model.joints.size() == 3 && model.omitted_joints == 1 &&
          model.joints[1].descriptor_offset == SkinFixture::bone0 &&
          model.joints[2].descriptor_offset == SkinFixture::bone1 &&
          model.joints[1].parent == 0 && model.joints[2].parent == 0 &&
          model.meshes[0].envelopes[1].influences[1].joint == 2,
          "envelope-only bones and their skeleton ancestors survive unrelated billboard pruning");
    put32(fixture.data, SkinFixture::bone0 + 4, 9 | 0x200);
    rejects([&] { (void) RigidModel(fixture.archive(), Fixture::joint, "skin", ModelRenderPass::Opaque); });
}

void missing_model_content()
{
    rejects([] { (void) RigidModel(nullptr, "fixture_joint"); });
    rejects([] { (void) RigidModel(Fixture().archive(), "missing_symbol"); });
    Fixture fixture;
    fixture.unlink(Fixture::joint + 16);
    rejects([&] { (void) fixture.model(); });
    fixture = Fixture();
    fixture.unlink(Fixture::dobj + 12);
    rejects([&] { (void) fixture.model(); });
    fixture = Fixture();
    fixture.unlink(Fixture::mobj + 12);
    rejects([&] { (void) fixture.model(); });
}

} // namespace

int main(int argc, char** argv)
{
    const std::map<std::string, std::function<void()>> cases = {
        {"valid_zero_offset_array", valid_zero_offset_array},
        {"normals_and_index_widths", normals_and_index_widths},
        {"valid_f32_geometry", valid_f32_geometry}, {"display_commands", display_commands},
        {"signed_byte_geometry", signed_byte_geometry},
        {"surface_counts", surface_counts}, {"truncated_packets", truncated_packets},
        {"indexed_array_bounds", indexed_array_bounds}, {"cyclic_graphs", cyclic_graphs},
        {"raw_joint_srt", raw_joint_srt}, {"joint_hierarchy", joint_hierarchy},
        {"invalid_joint_graphs", invalid_joint_graphs},
        {"indexed_uv_geometry", indexed_uv_geometry},
        {"materials_and_polygon_modes", materials_and_polygon_modes},
        {"shape_geometry_and_logical_bounds", shape_geometry_and_logical_bounds},
        {"material_vertex_dependencies", material_vertex_dependencies},
        {"descriptor_formats", descriptor_formats}, {"finite_geometry", finite_geometry},
        {"missing_model_content", missing_model_content},
        {"direct_rgba8_geometry", direct_rgba8_geometry},
        {"direct_rgba4_geometry", direct_rgba4_geometry},
        {"explicit_opaque_pass", explicit_opaque_pass},
        {"opaque_envelope_dependency_closure", opaque_envelope_dependency_closure},
        {"skin_metadata_and_bounds", skin_metadata_and_bounds},
        {"inverse_bind_requirements", inverse_bind_requirements},
        {"envelope_reference_validation", envelope_reference_validation},
        {"envelope_resource_limits", envelope_resource_limits},
        {"matrix_index_validation", matrix_index_validation},
        {"envelope_lighting_contract", envelope_lighting_contract},
        {"dobj_preorder_mapping", dobj_preorder_mapping},
        {"active_texture_matrix_contract", active_texture_matrix_contract},
    };
    if (argc != 2 || !cases.contains(argv[1])) {
        std::cerr << "Usage: rigid_model_test <known case name>\n";
        return 2;
    }
    try { cases.at(argv[1])(); }
    catch (const std::exception& error) {
        std::cerr << argv[1] << ": " << error.what() << '\n';
        return 1;
    }
    return 0;
}
