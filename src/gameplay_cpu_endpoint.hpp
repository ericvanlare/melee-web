#pragma once

#include "gameplay_action_store.h"
#include "gameplay_audio.h"
#include "gameplay_bootstrap.h"
#include "gameplay_cpu_statics.hpp"
#include "gameplay_match_clock.h"
#include "gameplay_match_context.h"
#include "gameplay_match_flow.h"
#include "gameplay_source_memory_runtime.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace melee_web {
class GameplayCpuCheckpoint;
class GameplayMatchSession;

enum class GameplayCpuEndpointStatus : std::uint32_t {
    Ok=0, Invalid=1, WrongPhase=2, OwnerRefused=3, LeafRefused=4,
    InvalidIndex=5, Overflow=6, Incomplete=7
};
enum class GameplayCpuEndpointView : std::uint32_t { Pre=1, CpuEnd=2, DrawEnd=3 };
enum class GameplayCpuEndpointProvenance : std::uint32_t {
    SharedPre=1, Reference=2, Corrected=3
};
enum class GameplayCpuEndpointDomain : std::uint32_t {
    Owner=1, Effects=2, Actions=3, Payload=4, Flow=5,
    Context=6, Bootstrap=7, Audio=8, Statics=9
};

/* row_kind is scoped by domain. Kind 0 is the bounded domain header where
 * present. All metadata slots are u64 transport values; each row-kind below
 * names the only slots a host may serialize. Unused slots and flags/reserved
 * stay zero. borrowed_address is a transient HEAP byte source and is never
 * itself canonical metadata. */
enum class GameplayCpuEndpointRowKind : std::uint32_t {
    Header=0,
    Owner=1,
    EffectToken=1, LObjAnchor=2, TObjAnchor=3, ParticleAnchor=4,
    ActionBinding=1, ActionSlot=2, ActionCacheEntry=3, ActionEvent=4,
    PayloadRow=1, PayloadFreeCell=2, PayloadAllocatedCell=3,
    FlowClockRecord=1, FlowProcessMask=2, FlowProfilePhase=3, FlowCadenceRecord=4,
    ContextRange=1,
    BootstrapTicks=1, BootstrapDisabledLinks=2,
    AudioOwnedRange=1, AudioCommandBudget=2, AudioSoundMode=3,
    StaticRow=1
};

namespace GameplayCpuEndpointField {
namespace Owner { enum : std::uint32_t { Storage,Context,Flow,Audio,World,ContextEpoch,SourceFrame,Count }; }
namespace Effects { enum : std::uint32_t { Token,World,Serial,AnchorToken,Count,Reserved,ParticleCount,AppsrtCount }; }
namespace ActionHeader { enum : std::uint32_t { LeafSchema,BindingCount,SlotsPerBinding,CacheCount,EventCount,Count }; }
namespace ActionBinding { enum : std::uint32_t { Present,Generation,Fighter,Store,SelectCallback,TransferCallback,Table,Count }; }
namespace ActionSlot { enum : std::uint32_t { Binding,Slot,Present,Motion,Offset,ArchiveBytes,SymbolBytes,Fighter,Store,Table,Clip,Tree,RowIdentity,Animation,RowOwner,CommandOwner,Count }; }
namespace ActionCacheEntry { enum : std::uint32_t { CacheIndex,Entry,Next,Root,Owner,Cache,Runtime,IntervalOwner,HitCallback,ReplayCallback,RecordCallback,Offset,ArchiveBytes,NativeAction,SymbolBytes,Animation,Count }; }
namespace ActionEvent { enum : std::uint32_t { Kind,Policy,CacheHit,Motion,Slot,ResultDomain,Offset,ArchiveBytes,SymbolBytes,Cache,Runtime,Row,Animation,Destination,Source,DestinationGeneration,SourceGeneration,SourceTable,RowOwner,Clip,CommandOwner,Count }; }
namespace PayloadHeader { enum : std::uint32_t { Schema,HeapHandle,HeapBytes,OwnerEpoch,World,Watermark,TopologyRevision,ArenaBegin,ArenaEnd,RowCount,FreeCellCount,AllocatedCellCount,PayloadBytes,Count }; }
namespace PayloadRow { enum : std::uint32_t { Index,SourceAddress,RequestedBytes,HostAddress,AllocationGeneration,Count }; }
namespace PayloadCell { enum : std::uint32_t { Kind,SourceAddress,CellBytes,Index,Count }; }
namespace FlowHeader { enum : std::uint32_t { Identity,Epoch,Generation,SourceValid,Complete,OpeningDemo,ClockFieldCount,ProcessMaskBytes,ProfilePhaseBytes,CadenceFieldCount,Count }; }
namespace FlowRange { enum : std::uint32_t { Address,Extent,Count }; }
namespace FlowClock { enum : std::uint32_t { RecordAddress,RecordExtent,Unk0,Unk4,Unk8,UnkC,CallbackX0,CallbackX1,CallbackX2,CallbackX3,CallbackX4_0,CallbackX4_1,Unk10XC,Unk10X20,Unk10X28,Unk10X30,Unk10X34,Unk10X38_0,Unk10X38_1,Count }; }
namespace FlowCadence { enum : std::uint32_t { RecordAddress,RecordExtent,X0_0_X0,X0_0_X8,X0_0_X10,X0_1_X0,X0_1_X8,X0_1_X10,X4,X38,X40,X48,AlarmAddress,AlarmExtent,Count }; }
namespace ContextRange { enum : std::uint32_t { Address,Extent,World,Epoch,Count }; }
namespace BootstrapHeader { enum : std::uint32_t { RangeCount,Count }; }
namespace BootstrapRange { enum : std::uint32_t { Address,Extent,Count }; }
namespace AudioHeader { enum : std::uint32_t { Generation,OwnedRangeCount,CommandBudgetAddress,SoundModeAddress,Count }; }
namespace AudioRange { enum : std::uint32_t { Kind,Address,Extent,OwnerIdentity,Generation,Count }; }
namespace StaticsHeader { enum : std::uint32_t { TableIdentity,RowCount,Count }; }
namespace StaticRow { enum : std::uint32_t { Index,Address,Extent,Policy,Domain,Count }; }
}

struct alignas(8) GameplayCpuEndpointRecord {
    std::uint32_t status{};             // 0
    std::uint32_t schema{};             // 4
    std::uint32_t view{};               // 8
    std::uint32_t provenance{};         // 12
    std::uint32_t domain{};             // 16
    std::uint32_t row_kind{};           // 20
    std::uint32_t index{};              // 24
    std::uint32_t count{};              // 28
    std::uint32_t metadata_bytes{};     // 32, used u64 slots only
    std::uint32_t flags{};              // 36, reserved zero
    std::uint64_t fields[32]{};         // 40..295, named slots only
    std::uint64_t borrowed_address{};   // 296, transient HEAP address
    std::uint32_t borrowed_bytes{};     // 304
    std::uint32_t reserved{};           // 308, zero
};
static_assert(std::is_trivially_copyable_v<GameplayCpuEndpointRecord>);
static_assert(sizeof(GameplayCpuEndpointRecord)==312);
static_assert(offsetof(GameplayCpuEndpointRecord,status)==0);
static_assert(offsetof(GameplayCpuEndpointRecord,schema)==4);
static_assert(offsetof(GameplayCpuEndpointRecord,view)==8);
static_assert(offsetof(GameplayCpuEndpointRecord,provenance)==12);
static_assert(offsetof(GameplayCpuEndpointRecord,domain)==16);
static_assert(offsetof(GameplayCpuEndpointRecord,row_kind)==20);
static_assert(offsetof(GameplayCpuEndpointRecord,index)==24);
static_assert(offsetof(GameplayCpuEndpointRecord,count)==28);
static_assert(offsetof(GameplayCpuEndpointRecord,metadata_bytes)==32);
static_assert(offsetof(GameplayCpuEndpointRecord,flags)==36);
static_assert(offsetof(GameplayCpuEndpointRecord,fields)==40);
static_assert(offsetof(GameplayCpuEndpointRecord,borrowed_address)==296);
static_assert(offsetof(GameplayCpuEndpointRecord,borrowed_bytes)==304);
static_assert(offsetof(GameplayCpuEndpointRecord,reserved)==308);

/* A stack-resident synchronous collector. begin() performs the full owner
 * admissions and bounded audio inventory once. record() is indexed and
 * allocation-free. revalidate() repeats admissions after the host has copied
 * every row, then closes the batch. It has no restore or caller-range API. */
class GameplayCpuEndpointReader final {
public:
    bool begin(const GameplayMatchSession&,const GameplayCpuCheckpoint&,
               GameplayCpuEndpointView,GameplayCpuEndpointProvenance) noexcept;
    GameplayCpuEndpointRecord record(GameplayCpuEndpointDomain,
                                     GameplayCpuEndpointRowKind,
                                     std::uint32_t index=0) const noexcept;
    bool revalidate() noexcept;
    GameplayCpuEndpointStatus status() const noexcept { return status_; }
    bool active() const noexcept { return active_; }
private:
    struct BatchData {
        MeleeWebActionEndpointHeader actions{};
        MeleeWebSourcePayloadEndpointHeader payload{};
        MeleeWebMatchFlowCpuEndpoint flow{};
        MeleeWebMatchContextRange context{};
        MeleeWebBootstrapCpuStorage bootstrap{};
        MeleeWebClockCpuStorage clock{};
        MeleeWebClockCpuEndpoint clock_endpoint{};
        MeleeWebCadenceCpuStorage cadence{};
        MeleeWebCadenceCpuEndpoint cadence_endpoint{};
        MeleeWebAudioCpuStorage audio{};
        CpuStaticEndpointHeader statics{};
        std::array<MeleeWebAudioOwnedRange,MELEE_WEB_AUDIO_OWNED_RANGE_MAX> audio_ranges{};
        std::uint32_t audio_count{};
        std::uint32_t source_frame{};
    };
    bool collect(BatchData&) const noexcept;
    bool same_batch(const BatchData&,const BatchData&) const noexcept;
    const GameplayMatchSession* session_{};
    const GameplayCpuCheckpoint* checkpoint_{};
    GameplayCpuEndpointView view_{};
    GameplayCpuEndpointProvenance provenance_{};
    BatchData data_{};
    GameplayCpuEndpointStatus status_=GameplayCpuEndpointStatus::Invalid;
    bool active_{};
};
}
