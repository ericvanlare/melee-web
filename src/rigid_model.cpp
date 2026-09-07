#include "rigid_model.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace melee_web {
namespace {
constexpr uint32_t va_pos = 9, va_nrm = 10, va_tex0 = 13, va_tex7 = 20, va_null = 255;
constexpr uint32_t index8 = 2, index16 = 3, type_s16 = 3, type_f32 = 4;
constexpr size_t max_joints = 4096, max_meshes = 4096, max_packets = 65536, max_vertices = 1000000;

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
    if (type < type_f32) {
        const auto raw = type < 2 ? uint32_t(a.range(base, 1)[0]) : uint32_t(a.be16(base));
        const int value = type == 1 && raw >= 0x80 ? int(raw) - 256 :
                          type == type_s16 && raw >= 0x8000 ? int(raw) - 65536 : int(raw);
        result = std::ldexp(float(value), -int(frac));
    } else {
        result = a.f32(base);
    }
    if (!std::isfinite(result) || std::abs(result) > 1000000.f)
        reject("Nonfinite or out-of-range geometry coordinate");
    return result;
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
    std::array<uint32_t, 10> array_offsets{}, maximum_index{}, widths{}, components{};
    mesh.minimum = {INFINITY, INFINITY, INFINITY};
    mesh.maximum = {-INFINITY, -INFINITY, -INFINITY};
    bool found_end = false;
    for (uint32_t i = 0; i < 11; ++i) {
        const auto d = descriptors + i * 24;
        (void) a.range(d, 24);
        const auto attr = a.be32(d);
        if (attr == va_null) { found_end = true; break; }
        // Fixed ordering also establishes the byte layout of each vertex packet.
        const bool uv = attr >= va_tex0 && attr <= va_tex7;
        if ((i == 0 && attr != va_pos) || i == 10 ||
            (attr != va_pos && attr != va_nrm && !uv) ||
            (i && attr <= mesh.attributes.back().attr))
            reject("Only ordered POS, optional NRM and TEX0 through TEX7 descriptors are supported");
        const auto mode = a.be32(d + 4), count = a.be32(d + 8), type = a.be32(d + 12);
        const auto frac = a.range(d + 16, 1)[0];
        const auto stride = a.be16(d + 18);
        if (mode != index8 && mode != index16) reject("Only indexed vertex attributes are supported");
        if (count != (attr == va_nrm ? 0u : 1u)) reject("Only XYZ position/normal and ST texture coordinates are supported");
        if (type > type_f32 || (!uv && type != type_s16 && type != type_f32))
            reject("Unsupported vertex component format");
        if (frac > 31 || (type == type_f32 && frac != 0)) reject("Unsupported vertex fractional scale");
        components[i] = uv ? 2 : 3;
        const uint32_t component_size = type < 2 ? 1 : type < 4 ? 2 : 4;
        const uint32_t width = components[i] * component_size;
        widths[i] = width;
        if (stride < width || stride > 255) reject("Vertex stride is out of range");
        auto array = a.pointer(d + 20, width);
        if (!array) reject("Vertex array pointer is null");
        const uint32_t alignment = component_size;
        if (*array % alignment) reject("Vertex array alignment is invalid");
        array_offsets[i] = *array;
        mesh.attributes.push_back({attr, mode, count, type, frac, stride,
                                   a.range(*array, width).data(), 0});
    }
    if (!found_end || mesh.attributes.empty()) reject("Unterminated or missing vertex descriptors");
    const auto has_attribute = [&](uint32_t attr) {
        return std::any_of(mesh.attributes.begin(), mesh.attributes.end(),
                           [&](const auto& descriptor) { return descriptor.attr == attr; });
    };
    if ((((mesh.material->render_mode & 7U) == 4U) || (mesh.material->render_mode & 8U)) &&
        !has_attribute(va_nrm))
        reject("Diffuse or specular lighting requires normal coordinates");
    for (const auto& texture : mesh.material->textures) {
        if ((texture.source_flags & 15U) == 1) {
            if (!has_attribute(va_nrm)) reject("Reflection texture requires normal coordinates");
        } else if (!has_attribute(va_tex0 + texture.source - 4)) {
            reject("Texture requires a missing UV vertex source");
        }
    }

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
                const uint32_t width = widths[i];
                const uint64_t location = uint64_t(array_offsets[i]) + uint64_t(index) * attr.stride;
                if (location > std::numeric_limits<uint32_t>::max()) reject("Vertex index address overflow");
                if (location + width > a.next_target_offset(array_offsets[i]))
                    reject("Vertex index crosses another referenced data region");
                (void) a.range(uint32_t(location), width);
                for (size_t axis = 0; axis < components[i]; ++axis) {
                    const auto value = component(a, uint32_t(location) + uint32_t(axis) * (width / components[i]),
                                                 attr.comp_type, attr.frac);
                    if (attr.attr == va_pos) {
                        mesh.minimum[axis] = std::min(mesh.minimum[axis], value);
                        mesh.maximum[axis] = std::max(mesh.maximum[axis], value);
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
        attr.byte_size = maximum_index[i] * uint32_t(attr.stride) + widths[i];
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
    struct PendingJoint { uint32_t offset, parent; };
    std::vector<PendingJoint> pending{{root->data_offset, RigidJoint::no_parent}};
    std::set<uint32_t> visited_joints;
    std::map<uint32_t, std::shared_ptr<const DatMaterial>> material_cache;
    size_t texture_bytes = 0;
    while (!pending.empty()) {
        const auto [joint, parent] = pending.back();
        pending.pop_back();
        if (joint % 4 || !visited_joints.insert(joint).second || joints.size() >= max_joints)
            reject("Cyclic, shared, unaligned or oversized joint graph");
        (void) a.range(joint, 64);
        absent(a, joint, "Custom joint classes are unsupported");
        RigidJoint node;
        node.descriptor_offset = joint;
        node.parent = parent;
        node.flags = a.be32(joint + 4);
        // Ordinary Euler transforms and render metadata. Original HSD transform
        // code performs scale inheritance; special matrix/IK modes need more HSD.
        if (node.flags & ~0x701D01D8u) reject("Joint flags require unsupported HSD behavior");
        for (uint32_t axis = 0; axis < 3; ++axis) {
            node.rotation[axis] = a.f32(joint + 20 + 4 * axis);
            node.scale[axis] = a.f32(joint + 32 + 4 * axis);
            node.translation[axis] = a.f32(joint + 44 + 4 * axis);
            if (!std::isfinite(node.rotation[axis]) || !std::isfinite(node.scale[axis]) ||
                !std::isfinite(node.translation[axis])) reject("Joint SRT values must be finite");
        }
        absent(a, joint + 56, "Joint inverse matrices are unsupported");
        absent(a, joint + 60, "Joint references are unsupported");
        const auto joint_index = uint32_t(joints.size());
        joints.push_back(node);
        // Siblings inherit this node's parent, not this node. Push child last so
        // the iterative traversal visits parents before descendants without recursion.
        if (auto next = a.pointer(joint + 12, 64)) pending.push_back({*next, parent});
        if (auto child = a.pointer(joint + 8, 64)) pending.push_back({*child, joint_index});
        std::set<uint32_t> objects;
        auto dobj = a.pointer(joint + 16, 16);
        while (dobj) {
            if (*dobj % 4 || !objects.insert(*dobj).second || objects.size() > max_meshes)
                reject("Cyclic, unaligned or oversized display-object chain");
            absent(a, *dobj, "Custom display-object classes are unsupported");
            const auto mat = required(a, *dobj + 8, 24);
            auto cached = material_cache.find(mat);
            if (cached == material_cache.end()) {
                auto decoded = std::make_shared<DatMaterial>(read_dat_material(a, mat));
                for (const auto& texture : decoded->textures) {
                    const auto bytes = texture.image.bytes.size() +
                        (texture.palette ? texture.palette->bytes.size() : 0);
                    if (bytes > 64 * 1024 * 1024 - texture_bytes)
                        reject("Model exceeds texture validation byte budget");
                    texture_bytes += bytes;
                }
                cached = material_cache.emplace(mat, std::move(decoded)).first;
            }
            std::set<uint32_t> polygons;
            auto pobj = a.pointer(*dobj + 12, 24);
            while (pobj) {
                if (*pobj % 4 || !polygons.insert(*pobj).second || meshes.size() >= max_meshes)
                    reject("Cyclic, unaligned or oversized polygon chain");
                RigidMesh mesh;
                mesh.joint_index = joint_index;
                mesh.material = cached->second;
                geometry(a, *pobj, mesh, *this);
                meshes.push_back(std::move(mesh));
                pobj = a.pointer(*pobj + 4, 24);
            }
            dobj = a.pointer(*dobj + 4, 16);
        }
    }
    if (meshes.empty() || !draw_packets)
        reject("Model has no draw primitives");
}
}
