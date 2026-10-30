#pragma once
#include <cstdint>
namespace melee_web {
enum class CpuComparisonDomain : std::uint32_t {
    None=0, Owner=1, Effects=2, Actions=3, Payload=4, Flow=5,
    Context=6, Bootstrap=7, Audio=8, Statics=9
};
/* Fixed private evidence. status0=equal,1=refusal,2=first actual differing
 * byte. Refusal has no invented byte/offset; detail retains its typed code.
 * row names the admitted allocation/table row or typed owner kind; address
 * and extent come from that exact owner. No provenance is inferred here. */
struct CpuComparison {
    std::uint32_t domain=0,status=0,row=UINT32_MAX,offset=UINT32_MAX;
    std::uint32_t expected=0,actual=0,address=0,extent=0,detail=0;
};
static_assert(sizeof(CpuComparison)==36);
}
