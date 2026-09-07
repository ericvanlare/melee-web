#include "rigid_model.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
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
        put32(data, mobj + 4, 4); // RENDER_DIFFUSE.
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
    check(mesh.diffuse == std::array<std::uint8_t, 4>{0x12, 0x34, 0x56, 0xff}, "material color");
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
              mesh.attributes[0].data == model.meshes[0].attributes[0].data &&
              mesh.minimum == model.meshes[0].minimum && mesh.maximum == model.meshes[0].maximum,
              "repeated geometry keeps immutable shared bytes and local bounds");
    }
}

void invalid_joint_graphs()
{
    for (const auto offset : {8U, 12U, 56U, 60U}) {
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
        check(!mesh.texture && mesh.attributes.size() == 2 && mesh.attributes[1].attr == 13,
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
    for (const auto render_mode : {0U, 5U, 0x80000004U}) {
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

void descriptor_formats()
{
    for (const auto& [offset, value] : std::vector<std::pair<std::uint32_t, std::uint32_t>>{
             {0, 10}, {0, 11}, {0, 255}, {4, 1}, {8, 0}, {12, 1}, {12, 5}}) {
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
        {"surface_counts", surface_counts}, {"truncated_packets", truncated_packets},
        {"indexed_array_bounds", indexed_array_bounds}, {"cyclic_graphs", cyclic_graphs},
        {"raw_joint_srt", raw_joint_srt}, {"joint_hierarchy", joint_hierarchy},
        {"invalid_joint_graphs", invalid_joint_graphs},
        {"indexed_uv_geometry", indexed_uv_geometry},
        {"materials_and_polygon_modes", materials_and_polygon_modes},
        {"descriptor_formats", descriptor_formats}, {"finite_geometry", finite_geometry},
        {"missing_model_content", missing_model_content},
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
