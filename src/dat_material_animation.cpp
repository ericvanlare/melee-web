#include "dat_material_animation.hpp"
#include "gameplay_compat.h"
#include "dat_texture.hpp"
#include "hsd_animation_bridge.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>
// Original C headers declare __assert with mutable strings. This diagnostic
// scope covers those declarations only; all port code retains normal warnings.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/fobj.h>
#pragma GCC diagnostic pop

namespace melee_web {
namespace {
void require(bool condition, const char* reason) { if (!condition) throw DatError(reason); }
struct IndexCurveToken {
    std::uint8_t opcode = 0;
    std::uint32_t value_bits = 0, slope_bits = 0;
    std::uint32_t wait = std::numeric_limits<std::uint32_t>::max();
    bool operator==(const IndexCurveToken&) const = default;
};
struct NativeTrack {
    HSD_FObjDesc descriptor{};
    std::vector<uint8_t> bytes;
    std::vector<IndexCurveToken> index_curve;
    std::optional<std::uint32_t> source_initial_index;
    bool uses_slope = false;
};
struct NativeMaterialAnimation {
    HSD_AObjDesc descriptor{};
    std::vector<std::unique_ptr<NativeTrack>> tracks;
};
struct NativeTextureAnimation {
    HSD_TexAnim descriptor{};
    HSD_AObjDesc animation{};
    std::vector<std::unique_ptr<NativeTrack>> tracks;
    std::vector<HSD_ImageDesc> images;
    std::vector<HSD_ImageDesc*> image_table;
    std::vector<HSD_TlutDesc> palettes;
    std::vector<HSD_TlutDesc*> palette_table;
};
// Original TObjUpdateFunc converts the produced scalar to an integer, then
// directly selects the TIMG/TCLT table entry. Validate the complete source
// FObj curve, not only its authored points, before the native callback can
// access those tables.
bool validate_indices(NativeTrack& track, uint32_t count,
                      bool dispatched_only=false)
{
    require(count > 0, "Texture animation index has no table");
    const auto format = track.descriptor.frac_value;
    require(format == 0 || ((format >> 5) >= 1 && (format >> 5) <= 4 && (format & 31) < 31),
            "Unsupported texture animation scalar encoding");
    size_t cursor = 0;
    auto byte = [&]() { require(cursor < track.bytes.size(), "Truncated texture animation stream"); return track.bytes[cursor++]; };
    auto varint = [&](uint32_t initial, unsigned shift, uint8_t previous) {
        uint32_t result = initial;
        while (previous & 128) {
            require(shift < 16, "Texture animation variable integer exceeds HSD limit");
            previous = byte(); result += uint32_t(previous & 127) << shift; shift += 7;
            require(result <= 65535, "Texture animation variable integer exceeds HSD limit");
        }
        return result;
    };
    auto scalar = [&](uint8_t scalar_format) {
        require(scalar_format == 0 || ((scalar_format >> 5) >= 1 &&
                (scalar_format >> 5) <= 4 && (scalar_format & 31) < 31),
                "Unsupported texture animation scalar encoding");
        double value;
        if (scalar_format == 0) {
            uint32_t bits = byte();
            for (unsigned shift = 8; shift <= 24; shift += 8)
                bits |= uint32_t(byte()) << shift;
            value = std::bit_cast<float>(bits);
        } else {
            uint32_t bits = byte();
            const unsigned kind = scalar_format >> 5;
            if (kind == 1 || kind == 2) bits |= uint32_t(byte()) << 8;
            const int32_t integer = kind == 1 ? int16_t(bits) :
                kind == 3 ? int8_t(bits) : int32_t(bits);
            value = double(integer) / double(uint32_t(1) << (scalar_format & 31));
        }
        require(std::isfinite(value), "Texture animation stream contains a nonfinite scalar");
        return value;
    };
    auto check_range = [&](double low, double high, double guard,
                           const char* reason) {
        const double consumer_limit = track.descriptor.type == 10 ? 256.0 : 65535.0;
        // TIMG uses a signed float-to-int conversion before indexing, so
        // values in (-1, 0) truncate to index zero. TCLT narrows to u8 and
        // therefore must stay nonnegative before its authored table lookup.
        const bool lower_in_range = track.descriptor.type == 10
            ? low - guard >= 0 : low - guard > -1.0;
        const bool valid = std::isfinite(low) && std::isfinite(high) && lower_in_range &&
                high + guard <= 65535 &&
                (track.descriptor.type != 10 || high + guard < consumer_limit) &&
                (dispatched_only || high + guard < count);
        if (!valid) {
            throw DatError(std::string(reason) + " (type=" +
                std::to_string(track.descriptor.type) + " range=" +
                std::to_string(low) + ".." + std::to_string(high) +
                " guard=" + std::to_string(guard) + " table=" +
                std::to_string(count) + ")");
        }
    };
    auto check_value = [&](double value) {
        check_range(value, value, 0,
                    "Texture animation source curve selects outside its table");
    };
    auto check_linear = [&](double p0, double p1, double duration) {
        if (duration == 0) {
            check_value(p1);
            return;
        }
        const double guard = 32 * std::numeric_limits<float>::epsilon() *
            std::max({1.0, std::abs(p0), std::abs(p1)});
        check_range(std::min(p0, p1), std::max(p0, p1), guard,
                    "Texture animation linear curve can select outside its table");
    };
    auto check_spline = [&](double p0, double p1, double d0, double d1,
                            double duration) {
        if (duration == 0) {
            check_value(p1);
            return;
        }
        // splGetHelmite(1/duration, time, ...) expressed in u=time/duration.
        const double a = 2 * p0 - 2 * p1 + duration * (d0 + d1);
        const double b = -3 * p0 + 3 * p1 + duration * (-2 * d0 - d1);
        const double c = duration * d0;
        const double d = p0;
        auto evaluate = [&](double u) {
            return ((a * u + b) * u + c) * u + d;
        };
        double low = std::min(p0, p1), high = std::max(p0, p1);
        auto include = [&](double u) {
            if (u > 0 && u < 1) {
                const double value = evaluate(u);
                require(std::isfinite(value), "Texture animation spline has a nonfinite extremum");
                low = std::min(low, value);
                high = std::max(high, value);
            }
        };
        const double discriminant = 4 * b * b - 12 * a * c;
        if (a == 0) {
            if (b != 0) include(-c / (2 * b));
        } else if (discriminant >= 0) {
            const double root = std::sqrt(discriminant);
            include((-2 * b - root) / (6 * a));
            include((-2 * b + root) / (6 * a));
        }
        const double magnitude = std::max({1.0, std::abs(a), std::abs(b),
                                           std::abs(c), std::abs(d)});
        const double guard = 32 * std::numeric_limits<float>::epsilon() *
                             (1 + magnitude);
        check_range(low, high, guard,
                    "Texture animation spline can select outside its table");
    };

    double p0 = 0, p1 = 0, d0 = 0, d1 = 0;
    unsigned previous_opcode = 0;
    std::optional<uint32_t> previous_wait;
    size_t values = 0;
    unsigned first_value_opcode = 0;
    bool uses_slope = false;
    while (cursor < track.bytes.size()) {
        const auto header = byte(); const auto opcode = header & 15;
        require(opcode >= 1 && opcode <= 6,
                "Texture table animation has an unsupported FObj opcode");
        uses_slope |= opcode == 4 || opcode == 5;
        const auto entries = varint(((header >> 4) & 7) + 1, 3, header);
        for (uint32_t i = 0; i < entries; ++i) {
            if (opcode == 5) {
                d0 = d1;
                d1 = scalar(track.descriptor.frac_slope);
                track.index_curve.push_back({
                    static_cast<std::uint8_t>(opcode), 0,
                    std::bit_cast<std::uint32_t>(static_cast<float>(d1))});
            } else {
                if (!first_value_opcode) first_value_opcode = opcode;
                const double value = scalar(format);
                double incoming_slope = 0;
                if (opcode == 4) incoming_slope = scalar(track.descriptor.frac_slope);
                p0 = p1;
                p1 = value;
                if (opcode == 3) {
                    d0 = d1;
                    d1 = 0;
                } else if (opcode == 4) {
                    d0 = d1;
                    d1 = incoming_slope;
                } else if (opcode != 6 && previous_opcode != 5) {
                    d0 = d1;
                    d1 = 0;
                }

                check_value(value);
                if (previous_wait) {
                    // FObjLoadData assigns op_intrp from the preceding
                    // opcode before it reads this datum's opcode. SLP emits
                    // no object update, but its slopes carry into the next
                    // source interpolation exactly as in FObjInterpretAnim.
                    if (previous_opcode == 3 || previous_opcode == 4 ||
                        previous_opcode == 5)
                        check_spline(p0, p1, d0, d1, *previous_wait);
                    else if (previous_opcode == 1 || previous_opcode == 6) {
                        // CON and KEY select only their endpoints.
                        check_value(p0);
                        check_value(p1);
                    } else if (previous_opcode == 2) {
                        // LIN stays between its endpoints; include an outward
                        // float-evaluation margin before direct table access.
                        check_linear(p0, p1, *previous_wait);
                    }
                }
                ++values;
                previous_wait.reset();
                std::uint32_t curve_wait =
                    std::numeric_limits<std::uint32_t>::max();
                if (cursor < track.bytes.size()) {
                    const auto first = byte();
                    curve_wait = varint(first & 127, 7, first);
                    previous_wait = curve_wait;
                }
                track.index_curve.push_back({
                    static_cast<std::uint8_t>(opcode),
                    std::bit_cast<std::uint32_t>(static_cast<float>(value)), 0,
                    curve_wait});
            }
            previous_opcode = opcode;
            require(cursor < track.bytes.size() || i + 1 == entries, "Texture animation packet is truncated");
        }
    }
    require(values >= 2 || (values == 1 && first_value_opcode == 6),
            "Texture animation requires a source value pair or key");
    return uses_slope;
}
}
struct DatMaterialAnimation::Storage {
    std::shared_ptr<const DatArchive> archive;
    std::vector<std::unique_ptr<HSD_MatAnimJoint>> joints;
    std::vector<std::unique_ptr<HSD_MatAnim>> materials;
    std::vector<std::unique_ptr<NativeMaterialAnimation>> material_animations;
    std::vector<std::unique_ptr<NativeTextureAnimation>> textures;
    HSD_MatAnimJoint* root = nullptr;
    std::vector<HSD_MatAnimJoint> indexed;
    bool indexable=false;
    uint32_t images = 0;
};
DatMaterialAnimation::DatMaterialAnimation(std::shared_ptr<const DatArchive> archive, uint32_t root,
                                         const MeleeWebNativeGraph& model,
                                         TextureIndexValidation index_validation)
    : storage_(std::make_unique<Storage>())
{
    auto& s = *storage_; s.archive = std::move(archive);
    require(bool(s.archive), "Material animation requires its archive owner");
    require(model.joint_count <= 256,
            "Material animation model exceeds the native 256-joint budget");
    const auto& a = *s.archive;
    std::set<uint32_t> material_seen, texture_seen;
    size_t stream_bytes = 0, palette_validation_bytes = 0;
    std::map<uint32_t,uint32_t> image_max_indices;
    auto record = [&](uint32_t offset, size_t length) {
        require(!(offset & 3), "Material animation descriptor is not aligned");
        (void)a.range(offset, length);
        require(length <= a.next_target_offset(offset) - offset, "Material animation descriptor crosses a referenced region");
    };
    auto required = [&](uint32_t slot, size_t length) { auto p = a.pointer(slot,length); require(bool(p), "Material animation requires a nonnull pointer"); return *p; };
    std::function<HSD_TexAnim*(std::optional<uint32_t>, const MeleeWebNativeMaterialDesc&)> texture_chain;
    texture_chain = [&](std::optional<uint32_t> offset, const MeleeWebNativeMaterialDesc& material) -> HSD_TexAnim* {
        if (!offset) return nullptr;
        require(s.textures.size() < 1024 && texture_seen.insert(*offset).second, "Material texture animation cycle or count limit");
        record(*offset,24);
        auto owner = std::make_unique<NativeTextureAnimation>(); auto& t = *owner;
        const uint32_t id = a.be32(*offset+4);
        require(!material.material.texture_count || material.textures,
                "Material animation model textures are absent");
        bool found = false;const MeleeWebNativeTextureDesc* native_texture=nullptr;
        for (uint32_t i=0;i<material.material.texture_count;++i) if(material.textures[i].source_id==id){found=true;native_texture=&material.textures[i];}
        require(found, "Texture animation ID is absent from the matching model material");
        t.descriptor.id = static_cast<GXTexMapID>(id);
        const uint16_t ni = a.be16(*offset+20), np = a.be16(*offset+22);
        require((!np || ni) && ni <= 256 && np <= 256 && s.images + ni <= 4096, "Texture animation table count exceeds limit");
        const auto it = a.pointer(*offset+12,size_t(ni)*4);
        require(bool(it) == (ni != 0), "Texture animation image count/pointer mismatch");
        const auto pt = a.pointer(*offset+16,size_t(np)*4);
        require(bool(pt) == (np != 0), "Texture animation palette count/pointer mismatch");
        t.images.resize(ni); t.image_table.resize(ni);
        std::vector<DatTextureImage> images;
        for (uint32_t i=0;i<ni;++i) {
            images.push_back(read_dat_texture_image(a,required(*it+4*i,24)));
            const auto& im = images.back();
            t.images[i] = {const_cast<uint8_t*>(im.bytes.data()), im.width, im.height, static_cast<GXTexFmt>(im.format), im.mipmap, im.min_lod, im.max_lod};
            t.image_table[i] = &t.images[i];
        }
        t.palettes.resize(np); t.palette_table.resize(np);
        const auto ao = required(*offset+8,16); record(ao,16);
        const auto flags = a.be32(ao); const auto end = a.f32(ao+4);
        require(!(flags & ~0x30000000U) && std::isfinite(end) && end >= 0 && end <= 65535,
                "Material animation flags or end frame are unsupported");
        require(!a.pointer(ao+12), "Material animation object references are unsupported");
        t.animation.flags = flags; t.animation.end_frame = end;
        auto fo = a.pointer(ao+8,20); unsigned channels = 0;
        // A source FObj may be shared by separate texture AObjs. Detect a
        // cycle within this chain while allowing authored aliases across
        // independent material channels.
        std::set<uint32_t> texture_track_seen;
        while (fo) {
            require(t.tracks.size()<24 && texture_track_seen.insert(*fo).second, "Texture animation track cycle or count limit"); record(*fo,20);
            auto track = std::make_unique<NativeTrack>(); auto& f = track->descriptor;
            f.length = a.be32(*fo+4); f.startframe = a.f32(*fo+8);
            const auto fields = a.range(*fo+12,4); f.type=fields[0]; f.frac_value=fields[1]; f.frac_slope=fields[2];
            require((f.type>=1 && f.type<=24) && !(channels & (1U<<f.type)), "Unsupported or duplicate material animation channel");
            channels |= 1U << f.type;
            require(std::isfinite(f.startframe) && f.startframe>=-32768 && f.startframe<=32767 && std::floor(f.startframe)==f.startframe,
                    "Texture animation start frame exceeds original signed frame storage");
            require(f.length && f.length<=65535 && (stream_bytes += f.length)<=4*1024*1024, "Texture animation stream exceeds limit");
            const auto bytes = required(*fo+16,f.length); const auto span = a.range(bytes,f.length);
            require(f.length<=a.next_target_offset(bytes)-bytes, "Texture animation stream crosses a referenced region");
            track->bytes.assign(span.begin(),span.end()); f.ad=track->bytes.data();
            require(f.type!=11||native_texture->has_lod,"Texture LOD animation requires native LOD storage");
            require(f.type<12||f.type>23||native_texture->has_tev,"Texture TEV animation requires native TEV storage");
            if(f.type!=1 && f.type!=10) {
                const MeleeWebAnimationTrack view{track->bytes.data(),track->bytes.size(),0,1,f.frac_value,f.frac_slope};
                char error[256];
                require(melee_web_animation_validate_native_track(&view,error,sizeof(error)),error);
            } else track->uses_slope=validate_indices(*track,f.type==1?ni:np,
                index_validation!=TextureIndexValidation::AllEncodedValues);
            if ((f.type == 1 || f.type == 10) &&
                index_validation == TextureIndexValidation::StaticSourceFrameZero) {
                const auto zero = std::bit_cast<std::uint32_t>(0.0F);
                require(std::bit_cast<std::uint32_t>(f.startframe) == zero &&
                            !track->index_curve.empty() &&
                            track->index_curve.front().opcode != 5 &&
                            track->index_curve.front().value_bits == zero &&
                            track->index_curve.front().wait !=
                                std::numeric_limits<std::uint32_t>::max() &&
                            track->index_curve.front().wait > 0,
                        "Static source TObj animation does not begin with a held zero table index");
                track->source_initial_index = 0;
            }
            if (!t.tracks.empty()) t.tracks.back()->descriptor.next = &f;
            else t.animation.fobjdesc = &f;
            t.tracks.push_back(std::move(track)); fo=a.pointer(*fo,20);
        }
        require(!t.tracks.empty(), "Texture animation has no supported channels");

        // TIMG and TCLT are evaluated independently by HSD_TObjUpdateFunc.
        // Same-clock FObj programs with identical normalized opcode, decoded
        // scalar, slope, and wait tokens prove that both selections match at
        // every update, even when their fixed-point encodings differ. For
        // other tracks validate the full image/palette Cartesian product;
        // endpoint proximity alone cannot establish which pairs the original
        // float evaluator visits.
        const NativeTrack* image_track = nullptr;
        const NativeTrack* palette_track = nullptr;
        for (const auto& track : t.tracks) {
            if (track->descriptor.type == 1) image_track = track.get();
            if (track->descriptor.type == 10) palette_track = track.get();
        }
        const bool synchronized_index_tracks =
            ni == np && image_track != nullptr && palette_track != nullptr &&
            std::bit_cast<uint32_t>(image_track->descriptor.startframe) ==
                std::bit_cast<uint32_t>(palette_track->descriptor.startframe) &&
            image_track->index_curve == palette_track->index_curve;
        auto maximum_index = [&](const DatTextureImage& im) {
            auto found = image_max_indices.find(im.descriptor_offset);
            if (found == image_max_indices.end()) {
                palette_validation_bytes += im.bytes.size();
                require(palette_validation_bytes <= 256U * 1024U * 1024U,
                        "Material animation palette validation exceeds work limit");
                found = image_max_indices.emplace(im.descriptor_offset,
                    dat_texture_max_palette_index(im)).first;
            }
            return found->second;
        };
        auto store_palette = [&](uint32_t palette_index, const DatTexturePalette& pal) {
            t.palettes[palette_index] = {
                const_cast<uint8_t*>(pal.bytes.data()),
                static_cast<GXTlutFmt>(pal.format), pal.source_name, pal.entries};
            t.palette_table[palette_index] = &t.palettes[palette_index];
        };
        auto validate_image_palette = [&](uint32_t image_index,
                                          uint32_t palette_index) {
            const auto& im = images[image_index];
            const bool indexed = im.format == 8 || im.format == 9 || im.format == 10;
            const auto offset_palette = required(*pt + 4 * palette_index, 16);
            // HSD_TObjUpdateFunc updates TIMG and TCLT independently. The
            // selected TLUT is consumed by HSD_TObjSetup only for CI images;
            // I/IA/RGB/CMPR images still require an authored TLUT descriptor
            // and an in-range TCLT table index, but do not interpret palette
            // entries for their pixels. Use the largest GX CI table capacity
            // for this descriptor-only non-CI path.
            const auto pal = read_dat_texture_palette_descriptor(
                a, offset_palette, indexed ? im.format : 10);
            if (indexed) {
                const auto maximum = maximum_index(im);
                if (maximum >= pal.entries)
                    throw DatError("Animated image references an index outside its authored TLUT entries: texture=" +
                        std::to_string(*offset) + " image=" + std::to_string(im.descriptor_offset) +
                        " palette=" + std::to_string(offset_palette) + " image_index=" +
                        std::to_string(image_index) + " palette_index=" + std::to_string(palette_index) +
                        " maximum=" + std::to_string(maximum) + " entries=" +
                        std::to_string(pal.entries));
            }
            store_palette(palette_index, pal);
        };

        if (index_validation == TextureIndexValidation::StaticSourceFrameZero) {
            for (uint32_t p = 0; p < np; ++p)
                store_palette(p, read_dat_texture_palette_descriptor(
                    a, required(*pt + 4*p, 16), 10));
            if (image_track && palette_track) {
                validate_image_palette(*image_track->source_initial_index,
                                       *palette_track->source_initial_index);
            } else if (image_track) {
                const auto& im = images[*image_track->source_initial_index];
                if (im.format == 8 || im.format == 9 || im.format == 10) {
                    const auto& base = native_texture->texture;
                    require(base.palette_data && base.palette_entries &&
                                base.palette_bytes >= size_t(base.palette_entries) * 2,
                            "Static animated image requires the source base palette");
                    require(maximum_index(im) < base.palette_entries,
                            "Static animated image references an index outside its source base TLUT palette");
                }
            } else if (palette_track) {
                const auto base_chain = read_dat_texture_chain(
                    a, native_texture->source_offset, true);
                require(!base_chain.empty(),
                        "Static animated palette has no source base texture");
                const auto& im = base_chain.front().image;
                const auto palette_index = *palette_track->source_initial_index;
                const auto palette_offset = required(*pt + 4 * palette_index, 16);
                const bool indexed = im.format == 8 || im.format == 9 || im.format == 10;
                const auto pal = read_dat_texture_palette_descriptor(
                    a, palette_offset, indexed ? im.format : 10);
                if (indexed)
                    require(dat_texture_max_palette_index(im) < pal.entries,
                            "Static source image references an index outside its selected TLUT palette");
            }
        } else if (!palette_track) {
            // HSD_TObjAddAnim resets tlut_no to -1. TIMG only changes the
            // image; without TCLT, HSD_TObjSetup keeps the model's base TLUT.
            // Retain every authored table descriptor, but do not pair these
            // unselected palettes with animated images.
            for (uint32_t p = 0; p < np; ++p)
                store_palette(p, read_dat_texture_palette_descriptor(
                    a, required(*pt + 4*p, 16), 10));
            for (const auto& im : images) {
                if (im.format != 8 && im.format != 9 && im.format != 10) continue;
                const auto& base = native_texture->texture;
                require(base.palette_data && base.palette_entries &&
                        base.palette_bytes >= size_t(base.palette_entries)*2,
                        "Indexed animated image requires the source base palette");
                require(maximum_index(im) < base.palette_entries,
                        "Animated image references an index outside its source base TLUT palette");
            }
        } else if (synchronized_index_tracks) {
            for (uint32_t i = 0; i < ni; ++i)
                validate_image_palette(i, i);
        } else {
            for (uint32_t p = 0; p < np; ++p)
                for (uint32_t i = 0; i < ni; ++i)
                    validate_image_palette(i, p);
        }

        t.descriptor.aobjdesc=&t.animation; t.descriptor.imagetbl=ni?t.image_table.data():nullptr;
        t.descriptor.tluttbl=np?t.palette_table.data():nullptr; t.descriptor.n_imagetbl=ni; t.descriptor.n_tluttbl=np;
        auto* result=&t.descriptor; s.images+=ni; s.textures.push_back(std::move(owner));
        result->next=texture_chain(a.pointer(*offset,24),material); return result;
    };
    std::function<HSD_MatAnim*(std::optional<uint32_t>,uint32_t)> material_chain;
    material_chain = [&](std::optional<uint32_t> offset,uint32_t dobj) -> HSD_MatAnim* {
        if (!offset) { require(dobj==UINT32_MAX,"Material animation DObj topology is incomplete"); return nullptr; }
        require(dobj<model.dobj_count && s.materials.size()<4096 && material_seen.insert(*offset).second,
                "Material animation DObj topology/cycle/count is invalid"); record(*offset,16);
        require(!a.pointer(*offset+12), "Active render animation is unsupported");
        auto m=std::make_unique<HSD_MatAnim>(); auto* result=m.get(); s.materials.push_back(std::move(m));
        if (const auto ao=a.pointer(*offset+4,16)) {
            record(*ao,16); auto owner=std::make_unique<NativeMaterialAnimation>(); auto& n=*owner;
            n.descriptor.flags=a.be32(*ao); n.descriptor.end_frame=a.f32(*ao+4);
            require(!(n.descriptor.flags&~0x30000000U)&&std::isfinite(n.descriptor.end_frame)&&
                    n.descriptor.end_frame>=0&&n.descriptor.end_frame<=65535,
                    "Material alpha animation flags or end frame are invalid");
            require(!a.pointer(*ao+12),"Material alpha animation object reference is unsupported");
            auto fo=a.pointer(*ao+8,20);unsigned material_channels=0;
            // Separate material AObjs can share an authored FObj. The
            // chain-local set still rejects a real next-pointer cycle.
            std::set<uint32_t> material_track_seen;
            while(fo) {
                require(n.tracks.size()<10&&material_track_seen.insert(*fo).second,"Material alpha animation duplicate track or cycle");
                record(*fo,20); auto track=std::make_unique<NativeTrack>(); auto& f=track->descriptor;
                f.length=a.be32(*fo+4);f.startframe=a.f32(*fo+8);
                const auto fields=a.range(*fo+12,4);f.type=fields[0];f.frac_value=fields[1];f.frac_slope=fields[2];
                require(f.type>=1&&f.type<=HSD_A_M_ALPHA&&!(material_channels&(1u<<f.type)),"Unsupported or duplicate material animation channel");
                material_channels|=1u<<f.type;
                require(std::isfinite(f.startframe)&&f.startframe>=-32768&&f.startframe<=32767&&std::floor(f.startframe)==f.startframe,
                        "Material alpha animation start frame exceeds source signed storage");
                require(f.length&&f.length<=65535&&(stream_bytes+=f.length)<=4U*1024U*1024U,
                        "Material alpha animation stream budget exceeded");
                const auto bytes=required(*fo+16,f.length);
                require(f.length<=a.next_target_offset(bytes)-bytes,"Material alpha stream crosses referenced region");
                const auto span=a.range(bytes,f.length);track->bytes.assign(span.begin(),span.end());f.ad=track->bytes.data();
                const MeleeWebAnimationTrack view{f.ad,f.length,0,1,f.frac_value,f.frac_slope};char error[256];
                require(melee_web_animation_validate_native_track(&view,error,sizeof(error)),error);
                // Original guarded color update checks the interpolated conversion range.
                if(n.tracks.empty())n.descriptor.fobjdesc=&f;else n.tracks.back()->descriptor.next=&f;
                n.tracks.push_back(std::move(track));fo=a.pointer(*fo,20);
            }
            result->aobjdesc=&n.descriptor;s.material_animations.push_back(std::move(owner));
        }
        require(model.dobjs[dobj].material < model.material_count,
                "Material animation model material index is invalid");
        result->texanim=texture_chain(a.pointer(*offset+8,24),model.materials[model.dobjs[dobj].material]);
        result->next=material_chain(a.pointer(*offset,16),model.dobjs[dobj].next); return result;
    };
    std::map<HSD_MatAnimJoint*,uint32_t> indices;
    bool contiguous=model.root==0;
    std::function<HSD_MatAnimJoint*(std::optional<uint32_t>,uint32_t)> joint_tree;
    joint_tree = [&](std::optional<uint32_t> offset,uint32_t joint) -> HSD_MatAnimJoint* {
        if (!offset) { require(joint==UINT32_MAX,"Material animation joint topology is incomplete"); return nullptr; }
        require(joint<model.joint_count && s.joints.size()<model.joint_count,
                "Material animation joint topology/cycle/count is invalid"); record(*offset,12);
        auto j=std::make_unique<HSD_MatAnimJoint>(); auto* result=j.get();indices[result]=joint;contiguous=contiguous&&*offset==root+joint*12;s.joints.push_back(std::move(j));
        result->matanim=material_chain(a.pointer(*offset+8,16),model.joints[joint].dobj);
        result->child=joint_tree(a.pointer(*offset,12),model.joints[joint].child);
        result->next=joint_tree(a.pointer(*offset+4,12),model.joints[joint].next); return result;
    };
    require(model.joints && model.root<model.joint_count &&
            (!model.dobj_count || model.dobjs) && (!model.material_count || model.materials),
            "Material animation requires a checked model graph");
    auto* tree=joint_tree(root,model.root);s.indexed.resize(model.joint_count);
    for(const auto& [old,index]:indices){
        auto& out=s.indexed[index];out=*old;
        out.child=old->child?&s.indexed.at(indices.at(old->child)):nullptr;
        out.next=old->next?&s.indexed.at(indices.at(old->next)):nullptr;
    }
    s.root=tree?&s.indexed.at(indices.at(tree)):nullptr;s.indexable=contiguous&&indices.size()==model.joint_count;
}
DatMaterialAnimation::~DatMaterialAnimation()=default;
void* DatMaterialAnimation::descriptor() const noexcept { return storage_->root; }
void* DatMaterialAnimation::indexed_descriptor()const{
    require(storage_->indexable,"Native material animation source is not a complete contiguous bone-indexed array");return storage_->root;
}
uint32_t DatMaterialAnimation::texture_animation_count() const noexcept { return uint32_t(storage_->textures.size()); }
uint32_t DatMaterialAnimation::image_count() const noexcept { return storage_->images; }
}
