/* Actual private range engine; synthetic owned statics, not source admission. */
#include "gameplay_cpu_statics.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <limits>
using namespace melee_web;
static unsigned allocations,releases;
static bool fail_allocation;
void* operator new(std::size_t n){if(fail_allocation)throw std::bad_alloc();++allocations;if(void* p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {if(p)++releases;std::free(p);}
void operator delete(void* p,std::size_t) noexcept {if(p)++releases;std::free(p);}
extern "C" { extern char __global_base,__data_end; }
static std::array<std::uint32_t,4> values{10,20,30,40};
static std::array<CpuStaticRow,1> wire_alias;
static CpuStaticRow row(unsigned i,CpuStaticPolicy p=CpuStaticPolicy::Restore)
{return {static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&values[i])),4,p,CpuStaticDomain::OriginalSourceC};}
int main()
{
 const CpuStaticBounds limits{reinterpret_cast<std::uintptr_t>(&__global_base),reinterpret_cast<std::uintptr_t>(&__data_end)};
 const std::array<CpuStaticRow,3> rows{row(0),row(1,CpuStaticPolicy::Invariant),row(2)};
 const auto original=values;
 auto refused=[&](std::span<const CpuStaticRow> input,std::span<const CpuIntervalSpan> no={}){
   unsigned a=allocations,f=releases;assert(!GameplayCpuStatics::configure(input,limits,no));
   assert(values==original);assert(a==allocations&&f==releases);
 };
 refused({});auto bad=rows;bad[0].bytes=0;refused(bad);
 bad=rows;bad[1].address=bad[0].address;refused(bad);
 bad=rows;std::swap(bad[0],bad[2]);refused(bad);
 bad=rows;bad[0].address=0xfffffff0U;bad[0].bytes=64;refused(bad);
 bad=rows;bad[0].policy=static_cast<CpuStaticPolicy>(3);refused(bad);
 bad=rows;bad[0].domain=static_cast<CpuStaticDomain>(3);refused(bad);
 const std::array<CpuIntervalSpan,1> typed{{{&values[2],4}}};refused(rows,typed);
 wire_alias[0]={static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(wire_alias.data())),4,CpuStaticPolicy::Restore,CpuStaticDomain::OriginalSourceC};refused(wire_alias);
 fail_allocation=true;assert(!GameplayCpuStatics::configure(rows,limits,{}));fail_allocation=false;assert(values==original);
 auto snapshot=GameplayCpuStatics::configure(rows,limits,{});assert(snapshot&&snapshot->row_count()==3&&snapshot->table_identity());
 assert(!snapshot->commit_prevalidated());assert(values==original);
 assert(snapshot->capture());assert(!snapshot->capture());
 values[0]=111;values[2]=333;const auto post=values;
 values[1]=999;assert(!snapshot->seal_post());assert(values[0]==111&&values[2]==333);
 values[1]=20;assert(snapshot->seal_post());assert(snapshot->admit());
 values[1]=998;assert(!snapshot->prepare_commit());assert(values[0]==111&&values[2]==333);
 values[1]=20;assert(snapshot->prepare_commit());
 const unsigned a=allocations,f=releases;assert(snapshot->commit_prevalidated());
 assert(allocations==a&&releases==f&&values==original);
 assert(!snapshot->commit_prevalidated());assert(values==original);
 values=post;assert(snapshot->matches_post());values[2]++;assert(!snapshot->matches_post());
 puts("Actual CPU static range engine canonicality alias refusal and noalloc commit passed");
}
