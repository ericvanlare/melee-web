#pragma once

#include "gameplay_menu_host.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>

namespace melee_web::stadium_first_css_diagnostic {

inline std::string hex32(uint32_t value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result(8, '0');
    for (int i = 7; i >= 0; --i) {
        result[static_cast<size_t>(i)] = digits[value & 0x0f];
        value >>= 4;
    }
    return result;
}

inline std::string hex64(uint64_t value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int i = 15; i >= 0; --i) {
        result[static_cast<size_t>(i)] = digits[value & 0x0f];
        value >>= 4;
    }
    return result;
}

// Exact first-CSS writer bodies extracted from native_menu_host_trace.cpp;
// preserve string pointer identities and explicit ABI padding output.
template <typename Pointer>
inline void write_pointer_identity(std::ostream& out, Pointer value) {
 out << '"' << (value == nullptr ? "null" : "unresolved_nonnull") << '"';
}

inline void write_rules(std::ostream& out,const StartMeleeRules& r){
 out << "{\"match_kind\":" << unsigned(r.match_kind)
     << ",\"x0_3\":" << unsigned(r.x0_3)
     << ",\"timer_enabled\":" << unsigned(r.timer_enabled)
     << ",\"timer_counts_up\":" << unsigned(r.timer_counts_up)
     << ",\"x1_0\":" << unsigned(r.x1_0) << ",\"x1_1\":" << unsigned(r.x1_1)
     << ",\"x1_2\":" << unsigned(r.x1_2) << ",\"x1_3\":" << unsigned(r.x1_3)
     << ",\"x1_4\":" << unsigned(r.x1_4) << ",\"x1_5\":" << unsigned(r.x1_5)
     << ",\"timer_shows_hours\":" << unsigned(r.timer_shows_hours)
     << ",\"friendly_fire\":" << unsigned(r.friendly_fire)
     << ",\"is_stock\":" << unsigned(r.is_stock)
     << ",\"x2_1\":" << unsigned(r.x2_1) << ",\"x2_2\":" << unsigned(r.x2_2)
     << ",\"single_button\":" << unsigned(r.single_button)
     << ",\"disable_pausing\":" << unsigned(r.disable_pausing)
     << ",\"x2_5\":" << unsigned(r.x2_5) << ",\"x2_6\":" << unsigned(r.x2_6)
     << ",\"x2_7\":" << unsigned(r.x2_7)
     << ",\"x3_0\":" << unsigned(r.x3_0) << ",\"x3_1\":" << unsigned(r.x3_1)
     << ",\"x3_2\":" << unsigned(r.x3_2) << ",\"x3_3\":" << unsigned(r.x3_3)
     << ",\"x3_4\":" << unsigned(r.x3_4) << ",\"x3_5\":" << unsigned(r.x3_5)
     << ",\"x3_6\":" << unsigned(r.x3_6) << ",\"x3_7\":" << unsigned(r.x3_7)
     << ",\"x4_0\":" << unsigned(r.x4_0) << ",\"is_vs\":" << unsigned(r.is_vs)
     << ",\"x4_2\":" << unsigned(r.x4_2) << ",\"x4_3\":" << unsigned(r.x4_3)
     << ",\"x4_4\":" << unsigned(r.x4_4) << ",\"x4_5\":" << unsigned(r.x4_5)
     << ",\"x4_6\":" << unsigned(r.x4_6) << ",\"x4_7\":" << unsigned(r.x4_7)
     << ",\"x5_0\":" << unsigned(r.x5_0) << ",\"x5_1\":" << unsigned(r.x5_1)
     << ",\"x5_2\":" << unsigned(r.x5_2) << ",\"x5_3\":" << unsigned(r.x5_3)
     << ",\"x5_4\":" << unsigned(r.x5_4) << ",\"x5_5\":" << unsigned(r.x5_5)
     << ",\"x5_6\":" << unsigned(r.x5_6) << ",\"x5_7\":" << unsigned(r.x5_7)
     << ",\"x6\":" << unsigned(r.x6) << ",\"x7\":" << unsigned(r.x7)
     << ",\"is_teams\":" << unsigned(r.is_teams) << ",\"x9\":" << unsigned(r.x9)
     << ",\"xA\":" << unsigned(r.xA) << ",\"xB\":" << int(r.xB)
     << ",\"xC\":" << int(r.xC) << ",\"xD\":" << unsigned(r.xD)
     << ",\"stage_kind\":" << r.stkind << ",\"time_limit\":" << r.time_limit
     << ",\"x14\":" << unsigned(r.x14) << ",\"x18\":" << r.x18
     << ",\"x1C_pad\":[" << r.x1C_pad[0] << ']'
     << ",\"item_mask\":\"" << hex64(r.x20) << "\",\"x28\":" << r.x28
     << ",\"x2C_bits\":\"" << hex32(std::bit_cast<uint32_t>(r.x2C))
     << "\",\"damage_ratio_bits\":\"" << hex32(std::bit_cast<uint32_t>(r.x30))
     << "\",\"game_speed_bits\":\"" << hex32(std::bit_cast<uint32_t>(r.game_speed))
     << "\",\"on_unpause_override\":";
 write_pointer_identity(out,r.on_unpause_override);
 out << ",\"on_pause_override\":"; write_pointer_identity(out,r.on_pause_override);
 out << ",\"check_for_pauser_override\":"; write_pointer_identity(out,r.check_for_pauser_override);
 out << ",\"on_match_start\":"; write_pointer_identity(out,r.on_match_start);
 out << ",\"on_frame_start\":"; write_pointer_identity(out,r.on_frame_start);
 out << ",\"on_frame_end\":"; write_pointer_identity(out,r.on_frame_end);
 out << ",\"on_match_end\":"; write_pointer_identity(out,r.on_match_end);
 out << ",\"x54_pointer\":"; write_pointer_identity(out,r.x54);
 out << ",\"x58_pointer\":"; write_pointer_identity(out,r.x58);
 out << ",\"pad_x5C\":\"";
 const char digits[]="0123456789abcdef";
 for(uint8_t byte:r.pad_x5C)out<<digits[byte>>4]<<digits[byte&15];
 out << "\"}";
}

inline void write_player(std::ostream& out, const PlayerInitData& p) {
    const unsigned flags_c = (unsigned(p.rumble_enabled) << 7) |
        (unsigned(p.xC_b1) << 6) | (unsigned(p.xC_b2) << 5) |
        (unsigned(p.xC_b3) << 4) | (unsigned(p.vs_invisible) << 3) |
        (unsigned(p.xC_b5) << 2) | (unsigned(p.xC_b6) << 1) |
        unsigned(p.xC_b7);
    const unsigned flags_d = (unsigned(p.xD_b0) << 7) |
        (unsigned(p.xD_b1) << 6) | (unsigned(p.xD_b2) << 5) |
        (unsigned(p.xD_b3) << 4) | (unsigned(p.xD_b4) << 3) |
        (unsigned(p.xD_b5) << 2) | (unsigned(p.xD_b6) << 1) |
        unsigned(p.xD_b7);
    out << "{\"ckind\":" << int(p.ckind)
        << ",\"slot_type\":" << unsigned(p.slot_type)
        << ",\"stocks\":" << int(p.stocks)
        << ",\"color\":" << unsigned(p.color)
        << ",\"slot\":" << unsigned(p.slot)
        << ",\"spawn\":" << int(p.x5)
        << ",\"spawn_direction\":" << int(p.spawn_dir)
        << ",\"sub_color\":" << unsigned(p.sub_color)
        << ",\"handicap\":" << int(p.handicap)
        << ",\"team\":" << unsigned(p.team)
        << ",\"nametag\":" << unsigned(p.nametag)
        << ",\"xB\":" << unsigned(p.xB)
        << ",\"flags_c\":" << flags_c << ",\"flags_d\":" << flags_d
        << ",\"cpu_kind\":" << unsigned(p.cpu_kind)
        << ",\"cpu_level\":" << unsigned(p.cpu_level)
        << ",\"damage_10\":" << p.x10 << ",\"damage_12\":" << p.x12
        << ",\"hp\":" << p.hp
        << ",\"attack_ratio_bits\":\""
        << hex32(std::bit_cast<uint32_t>(p.attack_ratio))
        << "\",\"defense_ratio_bits\":\""
        << hex32(std::bit_cast<uint32_t>(p.defense_ratio))
        << "\",\"model_scale_bits\":\""
        << hex32(std::bit_cast<uint32_t>(p.model_scale)) << "\"}";
}

inline void write_start(std::ostream& out, const StartMeleeData& start) {
    out << "{\"rules\":";
    write_rules(out, start.rules);
    out << ",\"players\":[";
    for (unsigned i = 0; i < GM_MAX_PLAYERS; ++i) {
        if (i) out << ',';
        write_player(out, start.players[i]);
    }
    out << "]}";
}

inline void write_vs_mode(std::ostream& out, const VsModeData& mode) {
    out << "{\"loser\":" << int(mode.loser)
        << ",\"ordered_stage_index\":" << int(mode.ordered_stage_index)
        << ",\"winner\":" << int(mode.winner)
        << ",\"unk_0x3\":" << unsigned(mode.unk_0x3)
        << ",\"unk_0x4\":" << unsigned(mode.unk_0x4)
        << ",\"unk_0x5\":" << unsigned(mode.unk_0x5)
        << ",\"unk_0x6\":" << unsigned(mode.unk_0x6)
        << ",\"unk_0x7\":" << unsigned(mode.unk_0x7)
        << ",\"start\":";
    write_start(out, mode.start);
    out << '}';
}

inline void write_css(std::ostream& out, const CSSData& css,
                      const char* ko_owner = "source_vs_owned") {
    out << "{\"unk_0x0\":" << css.unk_0x0
        << ",\"match_type\":" << unsigned(css.match_type)
        << ",\"pending_scene_change\":" << unsigned(css.pending_scene_change)
        << ",\"ko_counts_owner\":\"" << ko_owner << "\",\"vs\":";
    write_vs_mode(out, css.vs);
    out << '}';
}

} // namespace melee_web::stadium_first_css_diagnostic
