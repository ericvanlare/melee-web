#include "gameplay_cpu_statics.hpp"
#include <cstring>
#include <limits>
#include <new>
#include <exception>
namespace melee_web {
namespace {
bool interval(std::uintptr_t begin,std::size_t bytes,std::uintptr_t& end) noexcept
{
    if(!begin||!bytes||bytes>std::numeric_limits<std::uintptr_t>::max()-begin)return false;
    end=begin+bytes;return true;
}
bool overlap(std::uintptr_t a,std::size_t an,const void* b,std::size_t bn) noexcept
{
    if(!bn)return false;
    std::uintptr_t ae,be;const auto bb=reinterpret_cast<std::uintptr_t>(b);
    if(!interval(a,an,ae)||!interval(bb,bn,be))return true;
    return a<be&&bb<ae;
}
std::uint64_t identity(std::span<const CpuStaticRow> rows) noexcept
{
    std::uint64_t value=14695981039346656037ULL;
    for(const auto& row:rows)for(auto word:{row.address,row.bytes,
            static_cast<std::uint32_t>(row.policy),static_cast<std::uint32_t>(row.domain)})
        for(unsigned shift=0;shift<32;shift+=8){value^=(word>>shift)&255U;value*=1099511628211ULL;}
    return value;
}
}
std::unique_ptr<GameplayCpuStatics> GameplayCpuStatics::configure(
    std::span<const CpuStaticRow> rows,CpuStaticBounds bounds,
    std::span<const CpuIntervalSpan> forbidden) noexcept
{
    if(rows.empty()||!bounds.global_begin||bounds.data_end<=bounds.global_begin)return {};
    std::size_t bytes=0;std::uintptr_t previous=bounds.global_begin;
    for(const auto& row:rows){
        std::uintptr_t end;
        if(!interval(row.address,row.bytes,end)||row.address<previous||
           row.address<bounds.global_begin||end>bounds.data_end||
           (row.policy!=CpuStaticPolicy::Restore&&row.policy!=CpuStaticPolicy::Invariant)||
           (row.domain!=CpuStaticDomain::OriginalSourceC&&row.domain!=CpuStaticDomain::OriginalMplibBridge)||
           row.bytes>std::numeric_limits<std::size_t>::max()-bytes)return {};
        for(const auto& no:forbidden)
            if(overlap(row.address,row.bytes,no.address,no.bytes))return {};
        if(overlap(row.address,row.bytes,rows.data(),rows.size_bytes()))return {};
        bytes+=row.bytes;previous=end;
    }
    try{
        std::unique_ptr<GameplayCpuStatics> value(new GameplayCpuStatics);
        value->bounds_=bounds;value->rows_.assign(rows.begin(),rows.end());
        value->forbidden_.assign(forbidden.begin(),forbidden.end());
        value->before_.resize(bytes);value->after_.resize(bytes);
        value->table_identity_=identity(value->rows_);
        if(!value->layout_valid())return {};
        return value;
    }catch(const std::exception&){return {};}
}
bool GameplayCpuStatics::layout_valid() const noexcept
{
    if(rows_.empty()||identity(rows_)!=table_identity_||before_.size()!=after_.size())return false;
    std::size_t bytes=0;std::uintptr_t previous=bounds_.global_begin;
    for(const auto& row:rows_){
        std::uintptr_t end;
        if(!interval(row.address,row.bytes,end)||row.address<previous||
           row.address<bounds_.global_begin||end>bounds_.data_end||
           (row.policy!=CpuStaticPolicy::Restore&&row.policy!=CpuStaticPolicy::Invariant)||
           (row.domain!=CpuStaticDomain::OriginalSourceC&&row.domain!=CpuStaticDomain::OriginalMplibBridge)||
           row.bytes>before_.size()-bytes)return false;
        if(overlap(row.address,row.bytes,this,sizeof(*this))||
           overlap(row.address,row.bytes,rows_.data(),rows_.size()*sizeof(CpuStaticRow))||
           overlap(row.address,row.bytes,forbidden_.data(),forbidden_.size()*sizeof(CpuIntervalSpan))||
           overlap(row.address,row.bytes,before_.data(),before_.size())||
           overlap(row.address,row.bytes,after_.data(),after_.size()))return false;
        for(const auto& no:forbidden_)
            if(overlap(row.address,row.bytes,no.address,no.bytes))return false;
        previous=end;bytes+=row.bytes;
    }
    return bytes==before_.size();
}
bool GameplayCpuStatics::capture() noexcept
{
    if(phase_!=Phase::Configured||!layout_valid())return false;
    std::size_t offset=0;
    for(const auto& row:rows_){
        std::memcpy(before_.data()+offset,reinterpret_cast<const void*>(std::uintptr_t(row.address)),row.bytes);
        offset+=row.bytes;
    }
    phase_=Phase::Captured;return true;
}
bool GameplayCpuStatics::invariants_match() const noexcept
{
    std::size_t offset=0;
    for(const auto& row:rows_){
        if(row.policy==CpuStaticPolicy::Invariant&&std::memcmp(before_.data()+offset,
              reinterpret_cast<const void*>(std::uintptr_t(row.address)),row.bytes))return false;
        offset+=row.bytes;
    }
    return true;
}
bool GameplayCpuStatics::seal_post() noexcept
{
    if(phase_!=Phase::Captured||!layout_valid()||!invariants_match())return false;
    std::size_t offset=0;
    for(const auto& row:rows_){
        std::memcpy(after_.data()+offset,reinterpret_cast<const void*>(std::uintptr_t(row.address)),row.bytes);
        offset+=row.bytes;
    }
    phase_=Phase::Sealed;return true;
}
bool GameplayCpuStatics::admit() const noexcept
{return phase_==Phase::Sealed&&layout_valid()&&invariants_match();}
bool GameplayCpuStatics::prepare_commit() noexcept
{
    if(!admit())return false;
    phase_=Phase::Prepared;return true;
}
bool GameplayCpuStatics::commit_prevalidated() noexcept
{
    if(phase_!=Phase::Prepared)return false;
    std::size_t offset=0;
    for(const auto& row:rows_){
        if(row.policy==CpuStaticPolicy::Restore)
            std::memcpy(reinterpret_cast<void*>(std::uintptr_t(row.address)),before_.data()+offset,row.bytes);
        offset+=row.bytes;
    }
    phase_=Phase::Restored;return true;
}
CpuComparison GameplayCpuStatics::compare_post() const noexcept
{
    const auto domain=static_cast<std::uint32_t>(CpuComparisonDomain::Statics);
    if(phase_!=Phase::Restored||!layout_valid())
        return {domain,1,UINT32_MAX,UINT32_MAX,0,0,0,0,1};
    std::size_t offset=0;
    for(std::size_t i=0;i<rows_.size();++i){
        const auto& row=rows_[i];
        const auto* actual=reinterpret_cast<const unsigned char*>(std::uintptr_t(row.address));
        for(std::uint32_t j=0;j<row.bytes;++j){
            const auto expected=static_cast<unsigned char>(after_[offset+j]);
            if(expected!=actual[j])return {domain,2,static_cast<std::uint32_t>(i),j,
                expected,actual[j],row.address,row.bytes,0};
        }
        offset+=row.bytes;
    }
    return {};
}
bool GameplayCpuStatics::matches_post() const noexcept
{return compare_post().status==0;}

}
