#include "rigid_model.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace melee_web {
namespace {
constexpr uint32_t va_pos = 9, va_nrm = 10, va_clr0 = 11, va_clr1 = 12,
                   va_tex0 = 13, va_tex7 = 20, va_nbt = 25, va_null = 255;
constexpr uint32_t direct = 1, index8 = 2, index16 = 3, type_s16 = 3, type_f32 = 4;
constexpr size_t max_joints = MELEE_WEB_SKIN_MAX_JOINTS, max_meshes = 4096, max_packets = 65536, max_vertices = 1000000;

[[noreturn]] void reject(const char* reason) { throw DatError(reason); }

// GX_VA_NBT is an alias descriptor after TEX7 in the enum, but the original
// PObj packet order treats its normal payload at the NRM position. Preserve
// attr=GX_VA_NBT in the published descriptor while using this order key only
// for validation and packet layout checks.
constexpr uint32_t attribute_order(uint32_t attr) noexcept {
    return attr == va_nbt ? va_nrm : attr;
}

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

void envelopes(const DatArchive& a, uint32_t table, RigidMesh& mesh) {
    const auto table_end = a.next_target_offset(table);
    for (uint32_t slot = 0; slot <= MELEE_WEB_POBJ_MAX_PALETTE; ++slot) {
        const auto pointer_slot = table + slot * 4;
        if (pointer_slot > table_end || table_end - pointer_slot < 4)
            reject("Unterminated envelope palette table");
        const auto entry = a.pointer(pointer_slot, 8);
        if (!entry) {
            if (mesh.envelopes.empty()) reject("Envelope palette is empty");
            return;
        }
        if (slot == MELEE_WEB_POBJ_MAX_PALETTE) reject("Envelope palette exceeds ten matrix slots");
        if (*entry % 4) reject("Envelope descriptor is unaligned");
        RigidEnvelope envelope;
        envelope.descriptor_offset = *entry;
        const auto end = a.next_target_offset(*entry);
        bool terminated = false;
        float total = 0;
        for (uint32_t influence = 0; influence <= MELEE_WEB_POBJ_MAX_INFLUENCES; ++influence) {
            const auto d = *entry + influence * 8;
            if (d > end || end - d < 8) reject("Unterminated envelope influence array");
            const auto joint = a.pointer(d, 64);
            if (!joint) { terminated = true; break; }
            if (influence == MELEE_WEB_POBJ_MAX_INFLUENCES)
                reject("Envelope exceeds thirty-one joint influences");
            if (*joint % 4) reject("Envelope joint reference is unaligned");
            const auto weight = a.f32(d + 4);
            if (!std::isfinite(weight) || weight < 0 || weight > 1)
                reject("Envelope weight must be finite and between zero and one");
            total += weight;
            // Resolve descriptor offsets after the complete joint traversal.
            envelope.influences.push_back({*joint, weight});
        }
        if (!terminated || envelope.influences.empty() || total <= 0)
            reject("Envelope must contain a terminated, nonzero weighted influence list");
        mesh.envelopes.push_back(std::move(envelope));
    }
}

void geometry(const DatArchive& a, uint32_t offset, RigidMesh& mesh, RigidModel& model, DatMaterialPolicy policy) {
    (void) a.range(offset, 24);
    absent(a, offset, "Custom polygon classes are unsupported");
    mesh.descriptor_offset = offset;
    mesh.flags = a.be16(offset + 12);
    const bool shape_animation = (mesh.flags & 0x3000U) == 0x1000U;
    // Bits zero and two are retained source metadata; HSD's primitive and
    // setup paths ignore them. Dream Land uses bit two on back-face-culled
    // foliage. The type field selects rigid skin (0), shape animation
    // (0x1000), or envelope (0x2000).
    if (mesh.flags & ~uint16_t(0xF005)) reject("Unsupported polygon flags or shape animation");
    if ((mesh.flags & 0x3000) == 0x2000)
        envelopes(a, required(a, offset + 20, 4), mesh);
    else if (!shape_animation) {
        const auto shared = a.pointer(offset + 20, 64);
        if (shared) {
            if (policy != DatMaterialPolicy::NativeDescriptors)
                reject("Shared-joint skinning requires the native HSD path");
            mesh.shared_joint_offset = *shared;
        }
    }
    const auto shape_set_offset = shape_animation ? required(a, offset + 20, 28) : 0;
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
    // Shared shape sets point back into the descriptor list (the NRM pointer
    // commonly targets the second descriptor), so relocation boundaries can
    // occur inside this fixed-size 24-byte record array. The null attr is the
    // source terminator; range() still enforces the archive data bound and
    // the attribute count cap below limits the scan.
    const auto descriptors_end = uint32_t(a.data().size());
    std::array<uint32_t, MELEE_WEB_POBJ_MAX_ATTRIBUTES> array_offsets{}, maximum_index{}, widths{}, components{};
    mesh.minimum = {INFINITY, INFINITY, INFINITY};
    mesh.maximum = {-INFINITY, -INFINITY, -INFINITY};
    bool found_end = false;
    for (uint32_t i = 0; i <= MELEE_WEB_POBJ_MAX_ATTRIBUTES; ++i) {
        const auto d = descriptors + i * 24;
        if (d > descriptors_end || descriptors_end - d < 24)
            reject("Vertex descriptors cross another referenced data region");
        (void) a.range(d, 24);
        const auto attr = a.be32(d);
        if (attr == va_null) { found_end = true; break; }
        // Fixed ordering also establishes the byte layout of each vertex packet.
        const bool matrix = attr <= 8;
        const bool color = attr == va_clr0 || attr == va_clr1;
        const bool uv = attr >= va_tex0 && attr <= va_tex7;
        const bool nbt = attr == va_nbt;
        if (nbt && policy != DatMaterialPolicy::NativeDescriptors)
            reject("GX_VA_NBT requires the original native PObj path");
        if (i == MELEE_WEB_POBJ_MAX_ATTRIBUTES ||
            (!matrix && attr != va_pos && attr != va_nrm && !nbt && !color && !uv) ||
            (i && attribute_order(attr) <= attribute_order(mesh.attributes.back().attr)))
            reject("Only ordered matrix indices, POS, NRM/NBT, CLR and TEX0 through TEX7 descriptors are supported");
        const auto mode = a.be32(d + 4), count = a.be32(d + 8), type = a.be32(d + 12);
        const auto frac = a.range(d + 16, 1)[0];
        const auto stride = a.be16(d + 18);
        if (matrix) {
            if ((mesh.envelopes.empty() && !mesh.shared_joint_offset) || mode != direct || stride != 0)
                reject("Direct matrix indices require an envelope/shared-joint source and zero array stride");
            absent(a, d + 20, "Direct matrix indices cannot reference a vertex array");
            mesh.attributes.push_back({attr, mode, count, type, frac, stride, nullptr, 0});
            continue;
        }
        if (color) {
            // The original native PObj path consumes all six GX packed color
            // encodings. RGB565/RGB8/RGBX8 carry GX_CLR_RGB while
            // RGBA4/RGBA6/RGBA8 carry GX_CLR_RGBA. Direct values live in the
            // display list; indexed values use a packed color array. Viewer
            // imports retain the narrower direct RGBA8 contract.
            static constexpr uint32_t packed_widths[] = {2, 3, 4, 2, 3, 4};
            if (type >= std::size(packed_widths) || count > 1 || frac != 0)
                reject("Unsupported packed color component descriptor");
            const uint32_t packed_width = packed_widths[type];
            if (mode == direct) {
                const bool native_packed = policy == DatMaterialPolicy::NativeDescriptors &&
                    count == (type >= 3 ? 1u : 0u);
                const bool viewer_rgba8 = type == 5 && count == 1;
                if ((!native_packed && !viewer_rgba8) ||
                    (stride != 0 && stride != packed_width && stride != 4))
                    reject("Unsupported direct packed color descriptor");
                absent(a, d + 20, "Direct vertex colors cannot reference a vertex array");
                widths[i] = packed_width;
                mesh.attributes.push_back({attr, mode, count, type, frac, stride, nullptr, 0});
                continue;
            }
            if (policy != DatMaterialPolicy::NativeDescriptors ||
                (mode != index8 && mode != index16))
                reject("Indexed packed colors require the original native PObj path");
            components[i] = 0; // Packed colors are not scalar geometry bounds.
            widths[i] = packed_width;
            if (stride < packed_width || stride > 255)
                reject("Indexed packed color stride is out of range");
            auto array = a.pointer(d + 20, packed_width);
            if (!array) reject("Indexed packed color array pointer is null");
            array_offsets[i] = *array;
            mesh.attributes.push_back({attr, mode, count, type, frac, stride,
                                       a.range(*array, packed_width).data(), 0});
            continue;
        }
        if (mode != index8 && mode != index16) reject("Only indexed vertex attributes are supported");
        if (count != (attr == va_nrm ? 0u : 1u)) reject("Only XYZ position/normal and ST texture coordinates are supported");
        if (type > type_f32 || (!uv && type != type_s16 && type != type_f32 && type != 1))
            reject("Unsupported vertex component format");
        if (nbt && (count != 1 || (type != type_s16 && type != type_f32)))
            reject("Only interleaved GX_VA_NBT count-one S16/F32 arrays are supported");
        if (frac > 31 || (type == type_f32 && frac != 0)) reject("Unsupported vertex fractional scale");
        components[i] = uv ? 2 : nbt ? 9 : 3;
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
    if (!has_attribute(va_pos)) reject("Position attribute is missing");
    const bool has_normal = has_attribute(va_nrm) || has_attribute(va_nbt);
    if ((mesh.material->render_mode & 2U) && !has_attribute(va_clr0))
        reject("Vertex-color material requires CLR0 geometry");
    if (!mesh.envelopes.empty() && !has_attribute(0))
        reject("Envelope geometry requires a position matrix index");
    if (shape_animation) {
        if (policy != DatMaterialPolicy::NativeDescriptors)
            reject("Shape animation requires the original native PObj path");
        RigidShape shape;
        shape.flags = a.be16(shape_set_offset);
        shape.shape_count = a.be16(shape_set_offset + 2);
        const auto shape_lists = (shape.flags & 2U) ? uint32_t(shape.shape_count) + 1U : shape.shape_count;
        if (!shape.shape_count || !a.be32(shape_set_offset + 4) ||
            ((shape.flags & 3U) != 1U && (shape.flags & 3U) != 2U) ||
            shape_lists > 4096 || (shape.flags & ~uint16_t(7)) ||
            a.be32(shape_set_offset + 4) > 2000U || a.be32(shape_set_offset + 16) > 2000U)
            reject("Shape set count or flags exceed the original morph buffer");
        shape.vertex_index_count = a.be32(shape_set_offset + 4);
        shape.normal_index_count = a.be32(shape_set_offset + 16);
        const auto vertex_desc = required(a, shape_set_offset + 8, 24);
        const auto normal_desc = a.pointer(shape_set_offset + 20, 24);
        const auto descriptor_index = [&](uint32_t target, bool normal) {
            if (target < descriptors || (target - descriptors) % 24)
                reject("Shape set attribute descriptor is outside the PObj list");
            const auto index = (target - descriptors) / 24;
            if (index >= mesh.attributes.size() ||
                (normal ? mesh.attributes[index].attr != va_nrm
                        : mesh.attributes[index].attr != va_pos))
                reject("Shape set attribute descriptor does not name POS/NRM data");
            return index;
        };
        shape.vertex_attribute = descriptor_index(vertex_desc, false);
        if (mesh.attributes[shape.vertex_attribute].attr_type != index8 &&
            mesh.attributes[shape.vertex_attribute].attr_type != index16)
            reject("Shape vertex data must use indexed POS storage");
        if (normal_desc) {
            shape.normal_attribute = descriptor_index(*normal_desc, true);
            if (mesh.attributes[shape.normal_attribute].attr_type != index8 &&
                mesh.attributes[shape.normal_attribute].attr_type != index16)
                reject("Shape normal data must use indexed NRM storage");
        } else if (shape.normal_index_count) {
            reject("Shape normal indices require a normal descriptor");
        }
        const auto validate_shape_indices = [&](uint32_t attribute, uint32_t table,
                                                 uint32_t index_count,
                                                 std::vector<const uint8_t*>& output) {
            const auto& descriptor = mesh.attributes[attribute];
            const auto bytes_per_index = descriptor.attr_type == index16 ? 2U : 1U;
            const auto table_end = a.next_target_offset(table);
            if (shape_lists > (table_end - table) / 4U)
                reject("Shape index pointer table is truncated");
            output.reserve(shape_lists);
            for (uint32_t shape_id = 0; shape_id < shape_lists; ++shape_id) {
                const auto indices = a.pointer(table + 4U * shape_id,
                                               index_count * bytes_per_index);
                if (!indices || index_count * bytes_per_index >
                                    a.next_target_offset(*indices) - *indices)
                    reject("Shape index list is truncated or crosses a referenced region");
                output.push_back(a.range(*indices, index_count * bytes_per_index).data());
                for (uint32_t item = 0; item < index_count; ++item) {
                    const auto raw = descriptor.attr_type == index16
                        ? a.be16(*indices + 2U * item) : a.range(*indices + item, 1)[0];
                    maximum_index[attribute] = std::max(maximum_index[attribute], uint32_t(raw));
                    const auto location = uint64_t(array_offsets[attribute]) +
                        uint64_t(raw) * descriptor.stride;
                    if (location > UINT32_MAX || location + widths[attribute] >
                                                a.next_target_offset(array_offsets[attribute]))
                        reject("Shape index references data outside its vertex array");
                    for (uint32_t axis = 0; axis < components[attribute]; ++axis) {
                        const auto value = component(a, uint32_t(location) +
                            axis * (widths[attribute] / components[attribute]),
                            descriptor.comp_type, descriptor.frac);
                        if (descriptor.attr == va_pos) {
                            mesh.minimum[axis] = std::min(mesh.minimum[axis], value);
                            mesh.maximum[axis] = std::max(mesh.maximum[axis], value);
                            model.minimum[axis] = std::min(model.minimum[axis], value);
                            model.maximum[axis] = std::max(model.maximum[axis], value);
                        }
                    }
                }
            }
        };
        const auto vertex_table = required(a, shape_set_offset + 12, shape_lists * 4U);
        validate_shape_indices(shape.vertex_attribute, vertex_table,
                               shape.vertex_index_count, shape.vertex_index_lists);
        if (shape.normal_index_count) {
            const auto normal_table = required(a, shape_set_offset + 24, shape_lists * 4U);
            validate_shape_indices(shape.normal_attribute, normal_table,
                                   shape.normal_index_count, shape.normal_index_lists);
        }
        mesh.shape = std::move(shape);
    }
    const bool lit = ((mesh.material->render_mode & 7U) == 4U) ||
                     (mesh.material->render_mode & 8U);
    if (lit && !has_normal)
        reject("Diffuse or specular lighting requires normal coordinates");
    // Original SetupEnvelopeModelMtx uploads normal matrices only when the
    // owning JObj has LIGHTING. A lit material without it would consume stale
    // GX state even with a valid normal vertex stream.
    if (lit && !mesh.envelopes.empty() && !(model.joints[mesh.joint_index].flags & 0x80U))
        reject("Lit envelope geometry requires its owning joint lighting flag");
    for (const auto& texture : mesh.material->textures) {
        if ((texture.source_flags & (1U << 24)) && !has_attribute(va_nbt))
            reject("Bump texture requires an interleaved GX_VA_NBT stream");
        if ((texture.source_flags & 15U) == 1) {
            if (!has_normal) reject("Reflection texture requires normal coordinates");
        } else if (!has_attribute(va_tex0 + texture.source - 4)) {
            reject("Texture requires a missing UV vertex source");
        }
    }
    const bool reflection = std::any_of(mesh.material->textures.begin(), mesh.material->textures.end(),
        [](const auto& texture) { return (texture.source_flags & 15U) == 1; });
    if (!reflection) {
        // HSD_TObjAssignResources assigns supported UV-only chains consecutive
        // generator IDs starting at zero, regardless of source UV or map IDs.
        // An active per-vertex texture matrix overrides GX_IDENTITY, but this
        // envelope path uploads those matrices only for reflection. Otherwise
        // it would read stale state. Attributes beyond active generators are
        // unused and do not require a texture palette.
        for (const auto& attr : mesh.attributes) {
            if (attr.attr >= 1 && attr.attr <= 8 && attr.attr <= mesh.material->textures.size())
                reject("Active indexed texture generator requires an uploaded reflection palette");
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
        const bool native_line_or_point = policy == DatMaterialPolicy::NativeDescriptors &&
            (opcode == 0xA8 || opcode == 0xB0 || opcode == 0xB8);
        if (opcode != 0x80 && opcode != 0x90 && opcode != 0x98 && opcode != 0xA0 && !native_line_or_point)
            reject("Unsupported display command or vertex format; expected an owned VAT0 primitive");
        if (bytes.size() - cursor < 2) reject("Truncated display primitive header");
        const auto count = uint32_t(bytes[cursor]) * 256 + bytes[cursor + 1];
        cursor += 2;
        if ((opcode == 0x80 && (count < 4 || count % 4)) ||
            (opcode == 0x90 && (count < 3 || count % 3)) ||
            ((opcode == 0x98 || opcode == 0xA0) && count < 3) ||
            (opcode == 0xA8 && (count < 2 || count % 2)) ||
            (opcode == 0xB0 && count < 2) || (opcode == 0xB8 && count < 1))
            reject("Invalid vertex count for primitive");
        if (++model.draw_packets > max_packets || count > max_vertices - model.submitted_vertices)
            reject("Model exceeds primitive or vertex budget");
        model.submitted_vertices += count;
        for (uint32_t vertex = 0; vertex < count; ++vertex) {
            uint32_t palette_slot = 0;
            for (size_t i = 0; i < mesh.attributes.size(); ++i) {
                const auto& attr = mesh.attributes[i];
                if (attr.attr_type == direct && attr.attr >= va_pos) {
                    if (bytes.size() - cursor < widths[i]) reject("Truncated direct color vertex packet");
                    cursor += widths[i];
                    continue;
                }
                const size_t encoded = attr.attr_type == index16 ? 2 : 1;
                if (bytes.size() - cursor < encoded) reject("Truncated indexed vertex packet");
                uint32_t index = bytes[cursor++];
                if (encoded == 2) index = index * 256 + bytes[cursor++];
                if (shape_animation && attr.attr == va_pos &&
                    index >= mesh.shape->vertex_index_count)
                    reject("Shape display position index exceeds its blended buffer");
                if (shape_animation && attr.attr == va_nrm &&
                    (!mesh.shape->normal_index_count ||
                     index >= mesh.shape->normal_index_count))
                    reject("Shape display normal index exceeds its blended buffer");
                if (attr.attr <= 8) {
                    const auto base = attr.attr == 0 ? 0U : 30U;
                    if (mesh.shared_joint_offset) {
                        if (index != base && index != base + 3U)
                            reject("Shared-joint vertex matrix index is outside PNMTX0/1");
                    } else if (index < base || (index - base) % 3 ||
                               (index - base) / 3 >= mesh.envelopes.size())
                        reject("Vertex matrix index is outside its loaded palette");
                    if (attr.attr == 0) palette_slot = index / 3;
                    continue;
                }
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
                        if (!mesh.envelopes.empty()) {
                            auto& low = mesh.palette_minimum[palette_slot][axis];
                            auto& high = mesh.palette_maximum[palette_slot][axis];
                            if (!(mesh.palette_used_mask & (1U << palette_slot))) low = high = value;
                            else { low = std::min(low, value); high = std::max(high, value); }
                        }
                        mesh.minimum[axis] = std::min(mesh.minimum[axis], value);
                        mesh.maximum[axis] = std::max(mesh.maximum[axis], value);
                        model.minimum[axis] = std::min(model.minimum[axis], value);
                        model.maximum[axis] = std::max(model.maximum[axis], value);
                    }
                }
                if (attr.attr == va_pos && !mesh.envelopes.empty())
                    mesh.palette_used_mask |= uint16_t(1U << palette_slot);
            }
        }
    }
    if (model.draw_packets == packets_before) reject("Polygon has no draw primitives");
    for (size_t i = 0; i < mesh.attributes.size(); ++i) {
        auto& attr = mesh.attributes[i];
        if (attr.attr_type == direct) continue;
        attr.byte_size = maximum_index[i] * uint32_t(attr.stride) + widths[i];
        (void) a.range(array_offsets[i], attr.byte_size);
    }
}

uint32_t public_joint_offset(const std::shared_ptr<const DatArchive>& archive,
                             const std::string& name) {
    if (!archive) reject("Archive is missing");
    const auto& roots = archive->public_symbols();
    const auto root = std::find_if(roots.begin(), roots.end(), [&](const auto& s) { return s.name == name; });
    if (root == roots.end()) reject("Public model symbol is missing");
    return root->data_offset;
}
}

RigidModel::RigidModel(std::shared_ptr<const DatArchive> source, const std::string& root_name)
    : RigidModel(source, public_joint_offset(source, root_name), root_name, ModelRenderPass::All) {}

RigidModel::RigidModel(std::shared_ptr<const DatArchive> source, uint32_t joint_offset,
                       const std::string& label, ModelRenderPass pass, DatMaterialPolicy materials)
    : archive(std::move(source)),
      minimum{INFINITY, INFINITY, INFINITY}, maximum{-INFINITY, -INFINITY, -INFINITY},
      root_offset(joint_offset), render_pass(pass), symbol(label) {
    if (!archive) reject("Archive is missing");
    if (pass != ModelRenderPass::All && pass != ModelRenderPass::Opaque)
        reject("Unsupported model render-pass selection");
    const auto& a = *archive;
    struct PendingJoint { uint32_t offset, parent; };
    std::vector<PendingJoint> pending{{joint_offset, RigidJoint::no_parent}};
    std::set<uint32_t> visited_joints;
    std::map<uint32_t, std::shared_ptr<const DatMaterial>> material_cache;
    size_t texture_bytes = 0;
    size_t source_mesh_count = 0;
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
        // These flags change the descriptor union itself, so this is not a
        // DObj graph whose render pass we can classify without another loader.
        if ((node.flags & 0x20U) || ((node.flags & 0x4000U) && materials != DatMaterialPolicy::NativeDescriptors)) reject("Particle and spline joint graphs are unsupported");
        for (uint32_t axis = 0; axis < 3; ++axis) {
            node.rotation[axis] = a.f32(joint + 20 + 4 * axis);
            node.scale[axis] = a.f32(joint + 32 + 4 * axis);
            node.translation[axis] = a.f32(joint + 44 + 4 * axis);
            if (!std::isfinite(node.rotation[axis]) || !std::isfinite(node.scale[axis]) ||
                !std::isfinite(node.translation[axis])) reject("Joint SRT values must be finite");
        }
        if (const auto inverse = a.pointer(joint + 56, 48)) {
            if (*inverse % 4 || a.next_target_offset(*inverse) - *inverse < 48)
                reject("Joint inverse bind matrix is unaligned or crosses a referenced region");
            std::array<float, 12> matrix;
            for (uint32_t element = 0; element < matrix.size(); ++element) {
                matrix[element] = a.f32(*inverse + 4 * element);
                if (!std::isfinite(matrix[element]) || std::abs(matrix[element]) > 1000000.F)
                    reject("Joint inverse bind matrix must be finite and bounded");
            }
            node.inverse_bind = matrix;
        }
        (void) a.pointer(joint + 60); // Retained dependencies are validated below.
        const auto joint_index = uint32_t(joints.size());
        joints.push_back(node);
        // Siblings inherit this node's parent, not this node. Push child last so
        // the iterative traversal visits parents before descendants without recursion.
        if (auto next = a.pointer(joint + 12, 64)) pending.push_back({*next, parent});
        if (auto child = a.pointer(joint + 8, 64)) pending.push_back({*child, joint_index});
        if (node.flags & 0x4000U) continue; // Native owner validates the spline union separately.
        std::set<uint32_t> objects;
        auto dobj = a.pointer(joint + 16, 16);
        while (dobj) {
            if (*dobj % 4 || !objects.insert(*dobj).second || dobj_count >= max_meshes)
                reject("Cyclic, unaligned or oversized display-object chain");
            const auto dobj_index = dobj_count++;
            absent(a, *dobj, "Custom display-object classes are unsupported");
            const auto mat = required(a, *dobj + 8, 24);
            const auto material_pass = read_dat_material_pass(a, mat);
            const bool omitted = pass == ModelRenderPass::Opaque && material_pass != DatMaterialPass::Opaque;
            if (omitted) ++omitted_dobjs;
            auto cached = material_cache.find(mat);
            if (!omitted && cached == material_cache.end()) {
                auto decoded = std::make_shared<DatMaterial>(read_dat_material(a, mat, materials));
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
                if (*pobj % 4 || !polygons.insert(*pobj).second || source_mesh_count++ >= max_meshes)
                    reject("Cyclic, unaligned or oversized polygon chain");
                if (omitted) {
                    if (material_pass == DatMaterialPass::Translucent) ++omitted_translucent_meshes;
                    else ++omitted_texture_edge_meshes;
                    pobj = a.pointer(*pobj + 4, 24);
                    continue;
                }
                RigidMesh mesh;
                mesh.joint_index = joint_index;
                mesh.dobj_index = dobj_index;
                mesh.material = cached->second;
                geometry(a, *pobj, mesh, *this, materials);
                meshes.push_back(std::move(mesh));
                pobj = a.pointer(*pobj + 4, 24);
            }
            dobj = a.pointer(*dobj + 4, 16);
        }
    }
    if ((meshes.empty() || !draw_packets) && materials != DatMaterialPolicy::NativeDescriptors)
        reject("Model has no draw primitives");
    if (pass == ModelRenderPass::Opaque) {
        // Preserve the transform closure, including joints used by retained
        // envelopes even if all of their own geometry belongs to another pass.
        std::map<uint32_t, uint32_t> source_indices;
        for (uint32_t i = 0; i < joints.size(); ++i) source_indices.emplace(joints[i].descriptor_offset, i);
        std::vector<bool> keep(joints.size(), false);
        const auto retain_ancestors = [&](uint32_t index) {
            while (index != RigidJoint::no_parent && !keep[index]) {
                keep[index] = true;
                index = joints[index].parent;
            }
        };
        for (const auto& mesh : meshes) {
            retain_ancestors(mesh.joint_index);
            for (const auto& envelope : mesh.envelopes) for (const auto& influence : envelope.influences) {
                const auto found = source_indices.find(influence.joint);
                if (found == source_indices.end()) reject("Envelope references a joint outside the selected model");
                retain_ancestors(found->second);
            }
        }
        std::vector<uint32_t> remap(joints.size(), RigidJoint::no_parent);
        std::vector<RigidJoint> retained;
        for (uint32_t index = 0; index < joints.size(); ++index) {
            if (!keep[index]) { ++omitted_joints; continue; }
            auto node = joints[index];
            remap[index] = uint32_t(retained.size());
            if (node.parent != RigidJoint::no_parent) node.parent = remap[node.parent];
            retained.push_back(std::move(node));
        }
        for (auto& mesh : meshes) mesh.joint_index = remap[mesh.joint_index];
        joints = std::move(retained);
    }
    for (const auto& node : joints) {
        // Never clear unsupported transform flags. An omitted billboard leaf
        // is safe only when neither a selected mesh nor an envelope needs it.
        // Native HSD computes ordinary, vertical, horizontal and rotation billboards.
        // The inspection renderer still rejects that camera-dependent behavior.
        /* Native displayfunc.c implements perspective billboards as well as
         * the ordinary/axis/rotation variants. Keep the inspection path
         * strict because it does not have that camera-dependent transform. */
        const uint32_t allowed = 0x701D01DFu |
            (materials == DatMaterialPolicy::NativeDescriptors ? 0x6e00u : 0u);
        if ((node.flags & ~allowed) || (node.flags & 0xe00u)>0x800u) reject("Joint flags require unsupported HSD behavior");
        absent(a, node.descriptor_offset + 60, "Joint references are unsupported");
    }
    std::map<uint32_t, uint32_t> joint_indices;
    for (uint32_t index = 0; index < joints.size(); ++index)
        joint_indices.emplace(joints[index].descriptor_offset, index);
    for (auto& mesh : meshes) {
        if (mesh.envelopes.empty()) continue;
        const auto& owner = joints[mesh.joint_index];
        // _HSD_mkEnvelopeModelNodeMtx requires a skeleton ancestor unless the
        // owning node is itself the skeleton root.
        if (!(owner.flags & 2U)) {
            auto ancestor = mesh.joint_index;
            while (ancestor != RigidJoint::no_parent && !(joints[ancestor].flags & 3U))
                ancestor = joints[ancestor].parent;
            if (ancestor == RigidJoint::no_parent)
                reject("Envelope model has no skeleton ancestor");
            if (!(joints[ancestor].flags & 2U) && !joints[ancestor].inverse_bind)
                reject("Envelope skeleton ancestor requires an inverse bind matrix");
        }
        for (auto& envelope : mesh.envelopes) {
            const bool inverse_required = !(owner.flags & 2U) ||
                envelope.influences.front().weight < 1.F - std::numeric_limits<float>::epsilon();
            for (auto& influence : envelope.influences) {
                const auto found = joint_indices.find(influence.joint);
                if (found == joint_indices.end()) reject("Envelope references a joint outside the selected model");
                influence.joint = found->second;
                if (inverse_required && !joints[influence.joint].inverse_bind)
                    reject("Weighted envelope joint requires an inverse bind matrix");
            }
        }
    }
}
}
