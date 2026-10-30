#include "gameplay_cpu_statics.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
using namespace melee_web;

extern "C" { extern char __global_base,__data_end; }
static std::array<std::uint32_t,3> values{10,20,30};
static unsigned allocations,releases;
static bool track_allocations;
void* operator new(std::size_t size)
{
    if(track_allocations)++allocations;
    if(void* value=std::malloc(size))return value;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept
{ if(value){if(track_allocations)++releases;std::free(value);} }
void operator delete(void* value,std::size_t) noexcept
{ if(value){if(track_allocations)++releases;std::free(value);} }

static CpuStaticRow row(unsigned index,CpuStaticPolicy policy=CpuStaticPolicy::Restore)
{
    return {static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&values[index])),
            sizeof(values[index]),policy,CpuStaticDomain::OriginalSourceC};
}
static std::unique_ptr<GameplayCpuStatics> owner()
{
    std::array<CpuStaticRow,3> rows{row(0),row(1,CpuStaticPolicy::Invariant),row(2)};
    std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return a.address<b.address;});
    CpuStaticBounds bounds{reinterpret_cast<std::uintptr_t>(&__global_base),
                           reinterpret_cast<std::uintptr_t>(&__data_end)};
    auto result=GameplayCpuStatics::configure(rows,bounds,{});
    assert(result&&result->capture());
    return result;
}

int main()
{
    auto history=owner();
    assert(history->begin_history(100));
    assert(!history->history_begin_incarnation(2,100));
    assert(history->history_begin_incarnation(1,100));
    values[0]=11;values[2]=31;
    assert(!history->history_seal_endpoint(1,108));
    assert(history->history_seal_endpoint(1,102));
    auto header=history->history_endpoint_header();
    assert(header.status==CpuStaticEndpointStatus::Ok&&header.schema==1&&
           header.phase==CpuStaticEndpointPhase::Sealed&&header.row_count==3);
    assert(history->history_endpoint_row(1).status==CpuStaticEndpointStatus::Ok);
    assert(!history->history_close_endpoint(2,102));
    assert(history->history_close_endpoint(1,102));
    assert(!history->history_close_endpoint(1,102));
    assert(history->history_prepare_commit(1,102));
    assert(!history->history_admit_commit(2,102));
    values[1]=21;
    const auto endpoint=values;
    assert(!history->history_admit_commit(1,102));
    assert(values==endpoint); /* First final-admission failure writes nothing. */
    values[1]=20;
    assert(history->history_admit_commit(1,102));
    track_allocations=true;allocations=releases=0;
    history->history_commit_prevalidated();
    track_allocations=false;
    assert(allocations==0&&releases==0&&values[0]==10&&values[1]==20&&values[2]==30);

    /* Correct the same incarnation to its exact END before advancing. */
    values[0]=11;values[2]=31;
    assert(history->history_seal_endpoint(1,102));
    assert(history->history_close_endpoint(1,102));

    /* The same immutable PRE and restore authority serve incarnation two. */
    assert(!history->history_begin_incarnation(3,102));
    assert(history->history_begin_incarnation(2,102));
    values[0]=12;values[2]=32;
    assert(history->history_seal_endpoint(2,105));
    assert(history->history_close_endpoint(2,105));
    assert(history->history_prepare_commit(2,105));
    assert(!history->history_admit_commit(2,104));
    assert(values[0]==12&&values[2]==32);
    assert(history->history_admit_commit(2,105));
    track_allocations=true;allocations=releases=0;
    history->history_commit_prevalidated();
    track_allocations=false;
    assert(allocations==0&&releases==0&&values[0]==10&&values[2]==30);

    values[0]=13;values[2]=33;
    assert(history->history_seal_endpoint(2,105));
    assert(history->history_close_endpoint(2,105));
    assert(history->history_retire_admit(2,105));
    history->history_retire_confirmed();
    assert(!history->history_begin_incarnation(3,105));

    values={10,20,30};
    auto legacy=owner();
    values[0]=99;values[2]=98;
    assert(legacy->seal_post()&&legacy->prepare_commit());
    assert(legacy->commit_prevalidated());
    assert(!legacy->prepare_commit()&&!legacy->commit_prevalidated());
    assert(values[0]==10&&values[2]==30);
    puts("Actual CPU statics retained-PRE history permits two bounded restores, refuses stale tickets before writes, and preserves legacy single-use mode");
}
