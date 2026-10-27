#include "runtime_asset_scope.hpp"
#include "gameplay_source_files_runtime.hpp"
#include <cassert>
#include <string>
#include <vector>
#include <stdexcept>
#include <algorithm>

static std::vector<uint8_t> observed_logical,observed_tail;
static bool refuse_service;
extern "C" MeleeWebSourceFileScope* melee_web_source_files_begin(
        const MeleeWebSourceFileInput* input,size_t count,char*,size_t){
    // Explicit boundary spy: copy exactly what the real adapter borrows. This
    // does not model DVD request acceptance or claim native source execution.
    if(refuse_service)return nullptr;
    assert(count==1&&std::string(input[0].name)=="file.dat");
    observed_logical.assign(input[0].bytes,input[0].bytes+input[0].size);
    observed_tail.clear();
    if(input[0].transfer_tail_size)
        observed_tail.assign(input[0].transfer_tail,input[0].transfer_tail+input[0].transfer_tail_size);
    return reinterpret_cast<MeleeWebSourceFileScope*>(uintptr_t{1});
}
template<class F>static void refuses(F&& call){
    bool rejected=false;try{call();}catch(const std::runtime_error&){rejected=true;}assert(rejected);
}
static melee_web::RuntimeFiles native_files;
static melee_web::RuntimeAssetScope asset_scope(native_files);
static bool scoped_assets=true;
static void *world,*match,*archive_cache;
static std::string message;
static void check(bool okay,const char* error){if(!okay)throw std::runtime_error(error);}
// ACTUAL_NATIVE_TRANSFER_EXPORT
int main(){
    using melee_web::RuntimeFileBytes;
    using melee_web::RuntimeFiles;
    using melee_web::RuntimeAssetScope;
    std::vector<uint8_t> logical(33),tail(31);
    for(size_t i=0;i<logical.size();++i)logical[i]=static_cast<uint8_t>(i+1);
    for(size_t i=0;i<tail.size();++i)tail[i]=static_cast<uint8_t>(0x80+i);
    auto ng=asset_scope.request({"file.dat"});
    world=&ng;
    assert(!melee_web_native_asset_file_transfer(ng,"file.dat",logical.data(),33,tail.data(),31));
    assert(asset_scope.staged_file_count()==0);world=nullptr;
    assert(!melee_web_native_asset_file_transfer(ng+1,"file.dat",logical.data(),33,tail.data(),31));
    assert(!melee_web_native_asset_file_transfer(ng,"file.dat",logical.data(),33,tail.data(),30));
    assert(asset_scope.staged_file_count()==0);
    assert(melee_web_native_asset_file_transfer(ng,"file.dat",logical.data(),33,tail.data(),31));
    asset_scope.commit(ng);assert(native_files.at("file.dat").transfer_tail().size()==31);
    RuntimeFiles active{{"old.dat",{1,2,3}}};
    RuntimeAssetScope scope(active);
    const auto* old=active.at("old.dat").data();
    const auto generation=scope.request({"file.dat"});
    refuses([&]{scope.put(generation+1,"file.dat",logical,tail);});
    refuses([&]{scope.put(generation,"file.dat",logical,std::span(tail).first(30));});
    assert(scope.staged_file_count()==0&&active.at("old.dat").data()==old);
    scope.put(generation,"file.dat",logical,tail);
    assert(active.size()==1&&active.contains("old.dat"));
    // Abort must discard tail and bytes together, preserving the prior owner.
    scope.abort(generation);assert(active.at("old.dat").data()==old);
    const auto committed=scope.request({"file.dat"});scope.put(committed,"file.dat",logical,tail);
    scope.commit(committed);assert(active.at("file.dat").size()==33);
    assert(std::equal(logical.begin(),logical.end(),active.at("file.dat").begin()));
    assert(active.at("file.dat").transfer_tail().size()==31);
    RuntimeFiles copy=active;
    char error[64]{};
    assert(melee_web::begin_source_files(copy,error,sizeof(error))!=nullptr);
    assert(observed_logical==logical&&observed_tail==tail);
    refuse_service=true;assert(melee_web::begin_source_files(copy,error,sizeof(error))==nullptr);refuse_service=false;
    // A changed native vector extent must not accidentally keep tail authority.
    copy.at("file.dat").resize(34);
    refuses([&]{(void)melee_web::begin_source_files(copy,error,sizeof(error));});
    // Replacing logical bytes through the legacy vector assignment clears tail.
    copy.at("file.dat")=std::vector<uint8_t>(33,9);
    assert(copy.at("file.dat").transfer_tail().empty());
    assert(melee_web::begin_source_files(copy,error,sizeof(error))!=nullptr&&observed_tail.empty());
    // Stale/failed replacement never alters the committed active byte/tail pair.
    auto next=scope.request({"file.dat"});
    refuses([&]{scope.put(next,"file.dat",std::vector<uint8_t>(32),tail);});
    scope.abort(next);
    assert(melee_web::begin_source_files(active,error,sizeof(error))!=nullptr&&observed_tail==tail);
    next=scope.request({"file.dat"});std::vector<uint8_t> replacement(63,4),one_tail{0xf3};
    scope.put(next,"file.dat",replacement,one_tail);scope.commit(next);
    assert(melee_web::begin_source_files(active,error,sizeof(error))!=nullptr);
    assert(observed_logical==replacement&&observed_tail==one_tail);
    scope.release();assert(active.empty());
    // Existing literal vector construction/assignment and source size remain.
    RuntimeFileBytes old_api{1,2,3};assert(old_api.size()==3&&old_api.transfer_tail().empty());
    const uint8_t bytes[]={4,5};old_api.assign(bytes,bytes+2);assert(old_api.size()==2);
    return 0;
}
