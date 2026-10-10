#pragma once

#include "gameplay_menu_host.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace melee_web::stadium_first_css_diagnostic {

inline constexpr size_t kPadBytes = 822;
inline constexpr size_t kCssBytes = 0x148;
inline constexpr size_t kKoBytes = 6;
inline constexpr size_t kRulesBytes = 0x18;
inline constexpr size_t kSaveBytes = 0x55E8;
inline constexpr size_t kContextHeaderBytes = 52;
inline constexpr size_t kContextBytes = kContextHeaderBytes + 4 + kPadBytes +
    kCssBytes + kKoBytes + kRulesBytes + kSaveBytes;
inline constexpr size_t kConsumedPadHeaderBytes = 52;
inline constexpr size_t kConsumedPadBytes = kConsumedPadHeaderBytes + 4 * 11;
inline constexpr size_t kFirstSssTickInputHeaderBytes = 52;
inline constexpr size_t kFirstSssTickInputBytes = kFirstSssTickInputHeaderBytes + 4 * 11;
inline constexpr uint32_t kFirstSssConsumedPadSequence = 1604;
inline constexpr uint32_t kFirstSssSchedulerEndSequence = 1608;
inline constexpr uint32_t kPostdrawFirstConsumeSequence = 839;
inline constexpr uint32_t kPostdrawBatchCount = 148;
inline constexpr size_t kPostdrawInputHeaderBytes = 52;
inline constexpr size_t kPostdrawStatusesPerBatch = 4 * 11;
inline constexpr size_t kPostdrawInputBytes = kPostdrawInputHeaderBytes +
    kPostdrawBatchCount * kPostdrawStatusesPerBatch;
inline constexpr uint32_t kEntrySequence = 704;
inline constexpr uint32_t kReturnSequence = 833;
inline constexpr uint32_t kPadConsumeSequence = 834;
inline constexpr uint32_t kSourceTickSequence = 835;
inline constexpr uint32_t kKoAddress = 0x804D6730;
inline constexpr uint32_t kSeedAddress = 0x804D5F90;

inline constexpr std::array<uint8_t, 32> kSourceSha256 = {
    0x36,0x1e,0x8c,0x11,0x08,0xcc,0x2d,0x02,
    0xba,0x94,0xd1,0xf3,0xb3,0xbe,0x96,0x12,
    0xa8,0x0d,0x02,0x27,0x67,0x24,0x45,0xcd,
    0x4a,0x2b,0x0e,0x21,0x19,0x91,0x77,0x78
};

inline uint32_t read_be32(const uint8_t* bytes) {
    return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) |
           (uint32_t(bytes[2]) << 8) | uint32_t(bytes[3]);
}

inline std::string hex(const uint8_t* bytes, size_t size) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        result.push_back(digits[bytes[i] >> 4]);
        result.push_back(digits[bytes[i] & 0x0F]);
    }
    return result;
}

struct ContextInput {
    std::array<uint8_t, kPadBytes> pad{};
    std::array<uint8_t, kCssBytes> css{};
    std::array<uint8_t, kKoBytes> ko{};
    std::array<uint8_t, kRulesBytes> rules{};
    std::array<uint8_t, kSaveBytes> save{};
    uint32_t seed = 0;
    std::string source_sha256;
};

struct ConsumedPadInput {
    std::array<uint8_t, 4 * 11> ports{};
    std::string source_sha256;
};

struct FirstSssTickInput {
    std::array<uint8_t, 4 * 11> ports{};
    std::string source_sha256;
};

struct PostdrawInput {
    std::array<uint8_t, kPostdrawBatchCount * kPostdrawStatusesPerBatch> statuses{};
    std::string source_sha256;
    uint32_t first_consume_sequence = 0;
};

inline void decode_postdraw_pad_statuses(const PostdrawInput& input,
                                          uint32_t batch_index,
                                          PADStatus (&ports)[4]) {
    if (batch_index >= kPostdrawBatchCount)
        throw std::runtime_error("First-CSS post-draw input batch index is out of range");
    const uint8_t* const batch = input.statuses.data() +
        batch_index * kPostdrawStatusesPerBatch;
    for (size_t index = 0; index < 4; ++index) {
        const uint8_t* raw = batch + index * 11;
        ports[index].button = static_cast<uint16_t>(
            (uint16_t(raw[0]) << 8) | uint16_t(raw[1]));
        ports[index].stickX = static_cast<int8_t>(raw[2]);
        ports[index].stickY = static_cast<int8_t>(raw[3]);
        ports[index].substickX = static_cast<int8_t>(raw[4]);
        ports[index].substickY = static_cast<int8_t>(raw[5]);
        ports[index].triggerLeft = raw[6];
        ports[index].triggerRight = raw[7];
        ports[index].analogA = raw[8];
        ports[index].analogB = raw[9];
        ports[index].err = static_cast<int8_t>(raw[10]);
    }
}

inline void decode_consumed_pad_statuses(const ConsumedPadInput& input,
                                         PADStatus (&ports)[4]) {
    for (size_t index = 0; index < 4; ++index) {
        const uint8_t* raw = input.ports.data() + index * 11;
        ports[index].button = static_cast<uint16_t>(
            (uint16_t(raw[0]) << 8) | uint16_t(raw[1]));
        ports[index].stickX = static_cast<int8_t>(raw[2]);
        ports[index].stickY = static_cast<int8_t>(raw[3]);
        ports[index].substickX = static_cast<int8_t>(raw[4]);
        ports[index].substickY = static_cast<int8_t>(raw[5]);
        ports[index].triggerLeft = raw[6];
        ports[index].triggerRight = raw[7];
        ports[index].analogA = raw[8];
        ports[index].analogB = raw[9];
        ports[index].err = static_cast<int8_t>(raw[10]);
    }
}

inline ContextInput decode_context(const uint8_t* bytes, size_t size) {
    if (!bytes || size != kContextBytes)
        throw std::runtime_error("Stadium diagnostic input bundle has an unexpected exact length");
    if (std::string(reinterpret_cast<const char*>(bytes), 8) != "STC1INPT" ||
        read_be32(bytes + 8) != 1)
        throw std::runtime_error("Stadium diagnostic input bundle magic/version differs");
    if (!std::equal(kSourceSha256.begin(), kSourceSha256.end(), bytes + 12))
        throw std::runtime_error("Stadium diagnostic input source identity is not retained v6");
    if (read_be32(bytes + 44) != kEntrySequence ||
        read_be32(bytes + 48) != kReturnSequence)
        throw std::runtime_error("Stadium diagnostic input source sequence identities differ");

    ContextInput result{};
    result.source_sha256 = hex(bytes + 12, kSourceSha256.size());
    size_t offset = kContextHeaderBytes;
    result.seed = read_be32(bytes + offset);
    offset += 4;
    std::copy_n(bytes + offset, result.pad.size(), result.pad.begin());
    offset += result.pad.size();
    std::copy_n(bytes + offset, result.css.size(), result.css.begin());
    offset += result.css.size();
    std::copy_n(bytes + offset, result.ko.size(), result.ko.begin());
    offset += result.ko.size();
    std::copy_n(bytes + offset, result.rules.size(), result.rules.begin());
    offset += result.rules.size();
    std::copy_n(bytes + offset, result.save.size(), result.save.begin());
    offset += result.save.size();
    if (offset != size)
        throw std::runtime_error("Stadium diagnostic input bundle has trailing bytes");
    if (result.css[2] != 0 || result.css[3] != 0 ||
        read_be32(result.css.data() + 4) != kKoAddress ||
        !std::all_of(result.css.begin() + 0x48, result.css.begin() + 0x6C,
                     [](uint8_t value) { return value == 0; }))
        throw std::runtime_error(
            "Stadium diagnostic input has an invalid initial owner or callback region");
    return result;
}

inline ConsumedPadInput decode_consumed_pad(const uint8_t* bytes, size_t size,
                                             const std::string& expected_source_sha256) {
    if (!bytes || size != kConsumedPadBytes)
        throw std::runtime_error("First-CSS consumed PAD bundle has an unexpected exact length");
    if (std::string(reinterpret_cast<const char*>(bytes), 8) != "STC1PAD1" ||
        read_be32(bytes + 8) != 1)
        throw std::runtime_error("First-CSS consumed PAD bundle magic/version differs");
    if (read_be32(bytes + 44) != kPadConsumeSequence ||
        read_be32(bytes + 48) != kSourceTickSequence)
        throw std::runtime_error("First-CSS consumed PAD source sequence identities differ");
    ConsumedPadInput result{};
    result.source_sha256 = hex(bytes + 12, kSourceSha256.size());
    if (result.source_sha256 != expected_source_sha256 ||
        !std::equal(kSourceSha256.begin(), kSourceSha256.end(), bytes + 12))
        throw std::runtime_error(
            "First-CSS consumed PAD and context bundles name different source streams");
    std::copy_n(bytes + kConsumedPadHeaderBytes, result.ports.size(), result.ports.begin());
    return result;
}

inline FirstSssTickInput decode_first_sss_tick_input(
    const uint8_t* bytes, size_t size,
    const std::string& expected_source_sha256) {
    if (!bytes || size != kFirstSssTickInputBytes)
        throw std::runtime_error(
            "First-SSS consumed PAD bundle has an unexpected exact length");
    if (std::string(reinterpret_cast<const char*>(bytes), 8) != "STC1SSS1" ||
        read_be32(bytes + 8) != 1)
        throw std::runtime_error(
            "First-SSS consumed PAD bundle magic/version differs");
    FirstSssTickInput result{};
    result.source_sha256 = hex(bytes + 12, kSourceSha256.size());
    if (result.source_sha256 != expected_source_sha256 ||
        !std::equal(kSourceSha256.begin(), kSourceSha256.end(), bytes + 12))
        throw std::runtime_error(
            "First-SSS consumed PAD source identity differs from retained v6");
    if (read_be32(bytes + 44) != kFirstSssConsumedPadSequence ||
        read_be32(bytes + 48) != kFirstSssSchedulerEndSequence)
        throw std::runtime_error(
            "First-SSS consumed PAD sequence identities differ");
    std::copy_n(bytes + kFirstSssTickInputHeaderBytes, result.ports.size(),
                result.ports.begin());
    return result;
}

inline constexpr unsigned kFirstSssPrefixCount = 124;
struct FirstSssPrefixRecord {
    std::array<uint32_t,4> sequences{};
    std::array<uint8_t,44> ports{};
};
struct FirstSssPrefixInput {
    std::string source_sha256;
    std::array<FirstSssPrefixRecord,kFirstSssPrefixCount> records{};
};
inline FirstSssPrefixInput decode_first_sss_prefix_input(
    const uint8_t* bytes,size_t size,const std::string& expected_source_sha256) {
    if(!bytes || size!=48+kFirstSssPrefixCount*60)
        throw std::runtime_error("SSS prefix input exact length differs");
    if(std::string(reinterpret_cast<const char*>(bytes),8)!="STC1SSSP" ||
       read_be32(bytes+8)!=1 || read_be32(bytes+44)!=kFirstSssPrefixCount)
        throw std::runtime_error("SSS prefix input magic/version/count differs");
    FirstSssPrefixInput result{};
    result.source_sha256=hex(bytes+12,32);
    if(result.source_sha256!=expected_source_sha256 ||
       !std::equal(kSourceSha256.begin(),kSourceSha256.end(),bytes+12))
        throw std::runtime_error("SSS prefix input source identity differs");
    uint32_t previous=1610;
    for(unsigned i=0;i<kFirstSssPrefixCount;++i){
        auto& row=result.records[i];const auto* p=bytes+48+i*60;
        for(unsigned j=0;j<4;++j){
            row.sequences[j]=read_be32(p+j*4);
            if(row.sequences[j]<=previous)
                throw std::runtime_error("SSS prefix input sequence order differs");
            previous=row.sequences[j];
        }
        std::copy_n(p+16,44,row.ports.begin());
        ConsumedPadInput consumed{};consumed.ports=row.ports;
        PADStatus typed[4]{};decode_consumed_pad_statuses(consumed,typed);
    }
    return result;
}

inline PostdrawInput decode_postdraw_input(
    const uint8_t* bytes, size_t size,
    const std::string& expected_source_sha256) {
    if (!bytes || size != kPostdrawInputBytes)
        throw std::runtime_error(
            "First-CSS post-draw input bundle has an unexpected exact length");
    if (std::string(reinterpret_cast<const char*>(bytes), 8) != "STC1PSTR" ||
        read_be32(bytes + 8) != 1)
        throw std::runtime_error(
            "First-CSS post-draw input bundle magic/version differs");
    PostdrawInput result{};
    result.source_sha256 = hex(bytes + 12, kSourceSha256.size());
    result.first_consume_sequence = read_be32(bytes + 44);
    if (result.source_sha256 != expected_source_sha256 ||
        !std::equal(kSourceSha256.begin(), kSourceSha256.end(), bytes + 12))
        throw std::runtime_error(
            "First-CSS post-draw input and context bundles name different source streams");
    if (result.first_consume_sequence != kPostdrawFirstConsumeSequence ||
        read_be32(bytes + 48) != kPostdrawBatchCount)
        throw std::runtime_error(
            "First-CSS post-draw input source sequence/count differs");
    std::copy_n(bytes + kPostdrawInputHeaderBytes, result.statuses.size(),
                result.statuses.begin());
    return result;
}

} // namespace melee_web::stadium_first_css_diagnostic
