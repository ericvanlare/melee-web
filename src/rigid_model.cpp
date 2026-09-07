#include "rigid_model.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace melee_web {
namespace {
constexpr uint32_t va_pos = 9, va_nrm = 10, va_null = 255;
constexpr uint32_t index8 = 2, index16 = 3, type_s16 = 3, type_f32 = 4;
constexpr size_t max_meshes = 256, max_packets = 65536, max_vertices = 1000000;

[[noreturn]] void reject(const char* reason) { throw DatError(reason); }

uint32_t required(const DatArchive& a, uint32_t slot, size_t length) {
    auto value = a.pointer(slot, length);
    if (!value) reject("Required model pointer is null");
    if (*value % 4) reject("Descriptor pointer is not aligned to four bytes");
    return *value;
}

void absent(const DatArchive& a, uint32_t slot, const char* message) {
    if (a.pointer(slot)) reject(message);
}

float component(const DatArchive& a, uint32_t base, uint32_t type, uint8_t frac) {
    float result;
    if (type == type_s16) {
        const auto raw = a.be16(base);
        const int value = raw < 0x8000 ? int(raw) : int(raw) - 65536;
        result = std::ldexp(float(value), -int(frac));
    } else {
        result = a.f32(base);
    }
    if (!std::isfinite(result) || std::abs(result) > 1000000.f)
        reject("Nonfinite or out-of-range geometry coordinate");
    return result;
}

void material(const DatArchive& a, uint32_t offset, RigidMesh& mesh) {
    (void) a.range(offset, 24);
    absent(a, offset, "Custom material classes are unsupported");
    if (a.be32(offset + 4) != 4) reject("Only opaque diffuse materials are supported");
    absent(a, offset + 8, "Textured models are unsupported");
    const auto mat = required(a, offset + 12, 20);
    absent(a, offset + 16, "Custom material rendering is unsupported");
    absent(a, offset + 20, "Custom pixel-engine state is unsupported");
    if (a.f32(mat + 12) != 1.f) reject("Transparent materials are unsupported");
    auto color = a.range(mat + 4, 4);
    std::copy(color.begin(), color.end(), mesh.diffuse.begin());
    mesh.diffuse[3] = 255;
}

void geometry(const DatArchive& a, uint32_t offset, RigidMesh& mesh, RigidModel& model) {
    (void) a.range(offset, 24);
    absent(a, offset, "Custom polygon classes are unsupported");
    mesh.flags = a.be16(offset + 12);
    if (mesh.flags & ~uint16_t(0xC000)) reject("Skinned or shape-animated polygons are unsupported");
    absent(a, offset + 20, "Referenced joints, envelopes and shape animation are unsupported");
    const auto units = a.be16(offset + 14);
    if (!units) reject("Polygon display list is empty");
    mesh.display_bytes = uint32_t(units) * 32;
    size_t total_display_bytes = mesh.display_bytes;
    for (const auto& previous : model.meshes) total_display_bytes += previous.display_bytes;
    if (total_display_bytes > 64 * 1024 * 1024) reject("Model exceeds display-list scan budget");
    const auto display = required(a, offset + 16, mesh.display_bytes);
    if (mesh.display_bytes > a.next_target_offset(display) - display)
        reject("Display list crosses another referenced data region");
    mesh.display = a.range(display, mesh.display_bytes).data();
    const auto descriptors = required(a, offset + 8, 24);
    std::array<uint32_t, 2> array_offsets{}, maximum_index{};
    bool found_end = false;
    for (uint32_t i = 0; i < 3; ++i) {
        const auto d = descriptors + i * 24;
        (void) a.range(d, 24);
        const auto attr = a.be32(d);
        if (attr == va_null) { found_end = true; break; }
        // Fixed ordering also establishes the byte layout of each vertex packet.
        if ((i == 0 && attr != va_pos) || (i == 1 && attr != va_nrm) || i == 2)
            reject("Only ordered POS and optional NRM descriptors are supported");
        const auto mode = a.be32(d + 4), count = a.be32(d + 8), type = a.be32(d + 12);
        const auto frac = a.range(d + 16, 1)[0];
        const auto stride = a.be16(d + 18);
        if (mode != index8 && mode != index16) reject("Only indexed vertex attributes are supported");
        if (count != (attr == va_pos ? 1u : 0u)) reject("Only XYZ position and normal attributes are supported");
        if (type != type_s16 && type != type_f32) reject("Only S16 and F32 vertex components are supported");
        if (frac > 31 || (type == type_f32 && frac != 0)) reject("Unsupported vertex fractional scale");
        const uint32_t width = type == type_s16 ? 6 : 12;
        if (stride < width || stride > 255) reject("Vertex stride is out of range");
        auto array = a.pointer(d + 20, width);
        if (!array) reject("Vertex array pointer is null");
        const uint32_t alignment = type == type_s16 ? 2 : 4;
        if (*array % alignment) reject("Vertex array alignment is invalid");
        array_offsets[i] = *array;
        mesh.attributes.push_back({attr, mode, count, type, frac, stride,
                                   a.range(*array, width).data(), 0});
    }
    if (!found_end || mesh.attributes.empty()) reject("Unterminated or missing vertex descriptors");

    size_t cursor = 0;
    const auto packets_before = model.draw_packets;
    const auto bytes = a.range(display, mesh.display_bytes);
    while (cursor < bytes.size()) {
        const auto opcode = bytes[cursor++];
        if (opcode == 0) {
            // Imported primitive lists may only end in zero alignment padding.
            if (!std::all_of(bytes.begin() + cursor, bytes.end(), [](uint8_t b) { return b == 0; }))
                reject("Nonzero commands after display-list padding");
            break;
        }
        if (opcode != 0x80 && opcode != 0x90 && opcode != 0x98 && opcode != 0xA0)
            reject("Unsupported display command or vertex format; only VAT0 surface primitives are accepted");
        if (bytes.size() - cursor < 2) reject("Truncated display primitive header");
        const auto count = uint32_t(bytes[cursor]) * 256 + bytes[cursor + 1];
        cursor += 2;
        if ((opcode == 0x80 && (count < 4 || count % 4)) ||
            (opcode == 0x90 && (count < 3 || count % 3)) ||
            ((opcode == 0x98 || opcode == 0xA0) && count < 3))
            reject("Invalid vertex count for surface primitive");
        if (++model.draw_packets > max_packets || count > max_vertices - model.submitted_vertices)
            reject("Model exceeds primitive or vertex budget");
        model.submitted_vertices += count;
        for (uint32_t vertex = 0; vertex < count; ++vertex) {
            for (size_t i = 0; i < mesh.attributes.size(); ++i) {
                const auto& attr = mesh.attributes[i];
                const size_t encoded = attr.attr_type == index8 ? 1 : 2;
                if (bytes.size() - cursor < encoded) reject("Truncated indexed vertex packet");
                uint32_t index = bytes[cursor++];
                if (encoded == 2) index = index * 256 + bytes[cursor++];
                maximum_index[i] = std::max(maximum_index[i], index);
                const uint32_t width = attr.comp_type == type_s16 ? 6 : 12;
                const uint64_t location = uint64_t(array_offsets[i]) + uint64_t(index) * attr.stride;
                if (location > std::numeric_limits<uint32_t>::max()) reject("Vertex index address overflow");
                if (location + width > a.next_target_offset(array_offsets[i]))
                    reject("Vertex index crosses another referenced data region");
                (void) a.range(uint32_t(location), width);
                for (size_t axis = 0; axis < 3; ++axis) {
                    const auto value = component(a, uint32_t(location) + uint32_t(axis) * (width / 3),
                                                 attr.comp_type, attr.frac);
                    if (attr.attr == va_pos) {
                        model.minimum[axis] = std::min(model.minimum[axis], value);
                        model.maximum[axis] = std::max(model.maximum[axis], value);
                    }
                }
            }
        }
    }
    if (model.draw_packets == packets_before) reject("Polygon has no draw primitives");
    for (size_t i = 0; i < mesh.attributes.size(); ++i) {
        auto& attr = mesh.attributes[i];
        attr.byte_size = maximum_index[i] * uint32_t(attr.stride) + (attr.comp_type == type_s16 ? 6 : 12);
        (void) a.range(array_offsets[i], attr.byte_size);
    }
}
}

RigidModel::RigidModel(std::shared_ptr<const DatArchive> source, const std::string& root_name)
    : archive(std::move(source)),
      minimum{INFINITY, INFINITY, INFINITY}, maximum{-INFINITY, -INFINITY, -INFINITY}, symbol(root_name) {
    if (!archive) reject("Archive is missing");
    const auto& a = *archive;
    const auto& roots = a.public_symbols();
    const auto root = std::find_if(roots.begin(), roots.end(), [&](const auto& s) { return s.name == symbol; });
    if (root == roots.end()) reject("Public model symbol is missing");
    const auto joint = root->data_offset;
    if (joint % 4) reject("Joint root is unaligned");
    (void) a.range(joint, 64);
    absent(a, joint, "Custom joint classes are unsupported");
    // Root visibility/lighting metadata seen in rigid assets. Matrix-dependent
    // and particle/spline/instance flags require the full HSD joint path.
    if (a.be32(joint + 4) & ~0x70040088u) reject("Joint flags require unsupported HSD behavior");
    absent(a, joint + 8, "Joint hierarchies are not supported by this target");
    absent(a, joint + 12, "Sibling joints are not supported by this target");
    for (uint32_t i = 0; i < 9; ++i) {
        const auto expected = i >= 3 && i < 6 ? 1.f : 0.f;
        if (a.f32(joint + 20 + 4 * i) != expected) reject("Only identity joint transforms are supported");
    }
    absent(a, joint + 56, "Joint inverse matrices are unsupported");
    absent(a, joint + 60, "Joint references are unsupported");
    std::set<uint32_t> objects, polygons;
    auto dobj = a.pointer(joint + 16, 16);
    while (dobj) {
        if (*dobj % 4 || !objects.insert(*dobj).second || objects.size() > max_meshes)
            reject("Cyclic, unaligned or oversized display-object chain");
        absent(a, *dobj, "Custom display-object classes are unsupported");
        const auto mat = required(a, *dobj + 8, 24);
        auto pobj = a.pointer(*dobj + 12, 24);
        while (pobj) {
            if (*pobj % 4 || !polygons.insert(*pobj).second || meshes.size() >= max_meshes)
                reject("Cyclic, shared, unaligned or oversized polygon chain");
            RigidMesh mesh;
            material(a, mat, mesh);
            geometry(a, *pobj, mesh, *this);
            meshes.push_back(std::move(mesh));
            pobj = a.pointer(*pobj + 4, 24);
        }
        dobj = a.pointer(*dobj + 4, 16);
    }
    if (meshes.empty() || !draw_packets || maximum[0] == minimum[0] || maximum[1] == minimum[1])
        reject("Model has no drawable surface extent");
}
}
