#pragma once

#include "gameplay_menu_host.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

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
inline constexpr uint32_t kFreshFirstSssConsumedPadSequence = 1595;
inline constexpr uint32_t kFreshFirstSssSchedulerEndSequence = 1599;
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
inline constexpr std::array<uint8_t, 32> kFreshFirstSssSourceSha256 = {
    0x4f,0xd5,0x4b,0x4a,0xa9,0x53,0x72,0x92,
    0x29,0x5a,0xd8,0x3f,0xbb,0x75,0x65,0x5f,
    0x82,0x13,0xa4,0x75,0xa8,0x76,0xfd,0xd1,
    0x46,0x8a,0xff,0xb6,0x8d,0x3d,0x21,0x1f
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
    uint32_t source_entry_sequence = 0;
    uint32_t source_return_sequence = 0;
    uint32_t envelope_version = 0;
};

struct ConsumedPadInput {
    std::array<uint8_t, 4 * 11> ports{};
    std::string source_sha256;
};

// Version3 input-only first-row capability: source references and literal PAD,
// never post-tick/draw expectations. Context remains the checked v2 bootstrap.
inline constexpr size_t kFreshRowInputBytes = 8 + 4 + 32 + 12 * 4 + 4 * 11;
struct FreshRowInput {
    ConsumedPadInput consumed{};
    uint32_t entry_sequence=0,return_sequence=0,consume_sequence=0;
    uint32_t tick_sequence=0,draw_enter_sequence=0,draw_return_sequence=0;
    uint32_t poll_sequence=0,source_tick=0,draw_ordinal=0;
    uint32_t draw_source_tick=0,poll_source_tick=0,poll_draw_ordinal=0;
};

inline FreshRowInput decode_fresh_row_v3(const uint8_t* bytes, size_t size,
                                         const ContextInput& context) {
    if (!bytes || size != kFreshRowInputBytes ||
        std::string(reinterpret_cast<const char*>(bytes), 8) != "STC3PAD1" ||
        read_be32(bytes + 8) != 3)
        throw std::runtime_error("Fresh CSS row input has an invalid exact size/magic/version");
    if (context.envelope_version != 2 || context.source_sha256.empty() ||
        std::all_of(bytes+12,bytes+44,[](uint8_t value){return value==0;}) ||
        hex(bytes + 12, 32) != context.source_sha256)
        throw std::runtime_error("Fresh CSS row input does not belong to its v2 bootstrap source");
    FreshRowInput input{};
    input.consumed.source_sha256 = context.source_sha256;
    uint32_t* fields[] = {&input.entry_sequence,&input.return_sequence,
        &input.consume_sequence,&input.tick_sequence,&input.draw_enter_sequence,
        &input.draw_return_sequence,&input.poll_sequence,&input.source_tick,
        &input.draw_ordinal,&input.draw_source_tick,&input.poll_source_tick,
        &input.poll_draw_ordinal};
    size_t offset = 44;
    for (auto* field : fields) { *field=read_be32(bytes+offset);offset+=4; }
    if (input.entry_sequence != context.source_entry_sequence ||
        input.return_sequence != context.source_return_sequence ||
        !(input.entry_sequence < input.return_sequence &&
          input.return_sequence < input.consume_sequence &&
          input.consume_sequence < input.tick_sequence &&
          input.tick_sequence < input.draw_enter_sequence &&
          input.draw_enter_sequence < input.draw_return_sequence &&
          input.draw_return_sequence < input.poll_sequence) ||
        input.source_tick != 0 || input.draw_ordinal != 0 ||
        input.draw_source_tick != 1 || input.poll_source_tick != 1 ||
        input.poll_draw_ordinal != 1)
        throw std::runtime_error("Fresh CSS row input source pairing/first local clock differs");
    std::copy_n(bytes+offset,input.consumed.ports.size(),input.consumed.ports.begin());
    return input;
}

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


// Version4 finite CSS input: phase-local source schedule plus literal PAD only.
// Count is checked against exact transport length before any allocation/mutation.
inline constexpr size_t kFreshFiniteHeaderBytes = 60;
inline constexpr size_t kFreshFiniteRowBytes = 10 * 4 + 4 * 11;
struct FreshFiniteCssInput {
    std::string source_sha256;
    uint32_t entry_sequence=0,return_sequence=0,phase_end_sequence=0;
    std::vector<FreshRowInput> rows;
};
inline FreshFiniteCssInput decode_fresh_finite_css_v4(
    const uint8_t* bytes,size_t size,const ContextInput& context) {
    if(!bytes || size<kFreshFiniteHeaderBytes ||
       std::string(reinterpret_cast<const char*>(bytes),8)!="STC4PFX1" ||
       read_be32(bytes+8)!=4 || (size-kFreshFiniteHeaderBytes)%kFreshFiniteRowBytes)
        throw std::runtime_error("Fresh finite CSS input size/magic/version differs");
    const uint32_t count=read_be32(bytes+56);
    if(count<2 || count!=(size-kFreshFiniteHeaderBytes)/kFreshFiniteRowBytes ||
       context.envelope_version!=2 || context.source_sha256.empty() ||
       std::all_of(bytes+12,bytes+44,[](uint8_t v){return v==0;}) ||
       hex(bytes+12,32)!=context.source_sha256)
        throw std::runtime_error("Fresh finite CSS input count/source differs");
    FreshFiniteCssInput input{};
    input.source_sha256=context.source_sha256;
    input.entry_sequence=read_be32(bytes+44);
    input.return_sequence=read_be32(bytes+48);
    input.phase_end_sequence=read_be32(bytes+52);
    if(input.entry_sequence!=context.source_entry_sequence ||
       input.return_sequence!=context.source_return_sequence ||
       !(input.entry_sequence<input.return_sequence &&
         input.return_sequence<input.phase_end_sequence))
        throw std::runtime_error("Fresh finite CSS input context/phase references differ");
    input.rows.reserve(count);
    uint64_t previous_poll=input.return_sequence;
    size_t offset=kFreshFiniteHeaderBytes;
    for(uint32_t index=0;index<count;++index){
        FreshRowInput row{};
        row.consumed.source_sha256=input.source_sha256;
        row.entry_sequence=input.entry_sequence;row.return_sequence=input.return_sequence;
        uint32_t* fields[]={&row.consume_sequence,&row.tick_sequence,&row.draw_enter_sequence,
            &row.draw_return_sequence,&row.poll_sequence,&row.source_tick,&row.draw_ordinal,
            &row.draw_source_tick,&row.poll_source_tick,&row.poll_draw_ordinal};
        for(auto* field:fields){*field=read_be32(bytes+offset);offset+=4;}
        const uint64_t sequence[]={row.consume_sequence,row.tick_sequence,
            row.draw_enter_sequence,row.draw_return_sequence,row.poll_sequence};
        bool ordered=sequence[0]==previous_poll+1;
        for(size_t i=1;i<5;++i)ordered=ordered&&sequence[i]==sequence[i-1]+1;
        if(!ordered || row.poll_sequence>=input.phase_end_sequence ||
           row.source_tick!=index || row.draw_ordinal!=index ||
           uint64_t(row.draw_source_tick)!=uint64_t(index)+1 ||
           uint64_t(row.poll_source_tick)!=uint64_t(index)+1 ||
           uint64_t(row.poll_draw_ordinal)!=uint64_t(index)+1)
            throw std::runtime_error("Fresh finite CSS input source gap/order/local clock differs");
        std::copy_n(bytes+offset,row.consumed.ports.size(),row.consumed.ports.begin());
        offset+=row.consumed.ports.size();
        if(row.consumed.ports[10]!=PAD_ERR_NONE)
            throw std::runtime_error("Fresh finite CSS literal P1 is not connected");
        previous_poll=row.poll_sequence;
        input.rows.push_back(std::move(row));
    }
    if(offset!=size || previous_poll+1!=input.phase_end_sequence)
        throw std::runtime_error("Fresh finite CSS input does not end at its exact phase boundary");
    return input;
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
    result.source_entry_sequence = kEntrySequence;
    result.source_return_sequence = kReturnSequence;
    result.envelope_version = 1;
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

inline ContextInput decode_constructor_return_context_v2(
    const uint8_t* bytes, size_t size) {
    if (!bytes || size != kContextBytes)
        throw std::runtime_error(
            "CSS constructor-return context has an unexpected exact length");
    if (std::string(reinterpret_cast<const char*>(bytes), 8) != "STC2CSSR" ||
        read_be32(bytes + 8) != 2)
        throw std::runtime_error(
            "CSS constructor-return context magic/version differs");
    if (std::all_of(bytes + 12, bytes + 44,
                    [](uint8_t value) { return value == 0; }))
        throw std::runtime_error(
            "CSS constructor-return context has an empty source identity");
    const uint32_t entry_sequence = read_be32(bytes + 44);
    const uint32_t return_sequence = read_be32(bytes + 48);
    if (!entry_sequence || !return_sequence || entry_sequence >= return_sequence)
        throw std::runtime_error(
            "CSS constructor-return source phase references are not ordered");

    ContextInput result{};
    result.source_sha256 = hex(bytes + 12, 32);
    result.source_entry_sequence = entry_sequence;
    result.source_return_sequence = return_sequence;
    result.envelope_version = 2;
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
        throw std::runtime_error(
            "CSS constructor-return context has trailing bytes");
    if (result.css[2] != 0 || result.css[3] != 0 ||
        read_be32(result.css.data() + 4) != kKoAddress ||
        !std::all_of(result.css.begin() + 0x48, result.css.begin() + 0x6C,
                     [](uint8_t value) { return value == 0; }))
        throw std::runtime_error(
            "CSS constructor-return context has an invalid initial owner or callback region");
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
    const std::string magic(reinterpret_cast<const char*>(bytes), 8);
    const uint32_t version = read_be32(bytes + 8);
    const bool legacy_v1 = magic == "STC1SSS1" && version == 1;
    const bool fresh_v2 = magic == "STC1SSS2" && version == 2;
    if (!legacy_v1 && !fresh_v2)
        throw std::runtime_error(
            "First-SSS consumed PAD bundle magic/version differs");
    FirstSssTickInput result{};
    result.source_sha256 = hex(bytes + 12, kSourceSha256.size());
    const std::string version_source_sha256 = fresh_v2
        ? hex(kFreshFirstSssSourceSha256.data(), kFreshFirstSssSourceSha256.size())
        : hex(kSourceSha256.data(), kSourceSha256.size());
    if (result.source_sha256 != version_source_sha256 ||
        result.source_sha256 != expected_source_sha256)
        throw std::runtime_error(
            "First-SSS consumed PAD source identity differs from its exact profile/context");
    const uint32_t expected_consume = fresh_v2 ? kFreshFirstSssConsumedPadSequence
                                                : kFirstSssConsumedPadSequence;
    const uint32_t expected_tick = fresh_v2 ? kFreshFirstSssSchedulerEndSequence
                                             : kFirstSssSchedulerEndSequence;
    if (read_be32(bytes + 44) != expected_consume ||
        read_be32(bytes + 48) != expected_tick)
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

inline constexpr unsigned kFreshSssPrefix132Count = 132;
struct FreshSssPrefix132Input {
    std::string source_sha256;
    std::array<FirstSssPrefixRecord,kFreshSssPrefix132Count> records{};
};
inline FreshSssPrefix132Input decode_fresh_sss_prefix_132_input(
    const uint8_t* bytes,size_t size,const std::string& expected_source_sha256) {
    if(!bytes || size!=7968 ||
       std::string(reinterpret_cast<const char*>(bytes),8)!="STC1SSSP" ||
       read_be32(bytes+8)!=2 || read_be32(bytes+44)!=kFreshSssPrefix132Count)
        throw std::runtime_error("Fresh SSS prefix requires its exact132 version2 wire");
    FreshSssPrefix132Input result{};
    result.source_sha256=hex(bytes+12,32);
    if(result.source_sha256!=expected_source_sha256 ||
       !std::equal(kFreshFirstSssSourceSha256.begin(),kFreshFirstSssSourceSha256.end(),bytes+12))
        throw std::runtime_error("Fresh SSS prefix source identity differs");
    uint32_t previous=1601;
    for(unsigned i=0;i<kFreshSssPrefix132Count;++i){
        auto& row=result.records[i];const auto* p=bytes+48+i*60;
        for(unsigned j=0;j<4;++j){
            row.sequences[j]=read_be32(p+j*4);
            if(row.sequences[j]<=previous)
                throw std::runtime_error("Fresh SSS prefix sequence order differs");
            previous=row.sequences[j];
        }
        std::copy_n(p+16,44,row.ports.begin());
        ConsumedPadInput consumed{};consumed.ports=row.ports;
        PADStatus typed[4]{};decode_consumed_pad_statuses(consumed,typed);
    }
    if(result.records.front().sequences!=std::array<uint32_t,4>{1603,1607,1608,1609} ||
       result.records.back().sequences!=std::array<uint32_t,4>{2475,2477,2478,2479})
        throw std::runtime_error("Fresh SSS prefix exact endpoints differ");
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
