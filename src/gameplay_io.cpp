#include "gameplay_io.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <string>
namespace {
constexpr size_t max_bytes=64U*1024U*1024U,max_objects=512,max_requests=1024,max_name=4096;
struct Block {
    MeleeWebIoKind kind;
    size_t size;
    std::unique_ptr<unsigned char,decltype(&std::free)> bytes{nullptr,&std::free};
    std::string name;
};
struct Request {
    uint64_t id;
    std::shared_ptr<Block> source,dest;
    size_t source_offset,dest_offset,length;
    MeleeWebIoCallback callback;
    void* user;
    bool cancelled=false;
};
uint64_t next_identity=1;
int fail(char* error,size_t size,const char* message){if(error&&size){size_t n=std::min(size-1,std::strlen(message));std::memcpy(error,message,n);error[n]=0;}return 0;}
int ok(char* error,size_t size){if(error&&size)*error=0;return 1;}
bool valid_name(const char* name){
    if(!name||!*name)return false;
    for(size_t i=0;i<=max_name;i++)if(!name[i])return i<=max_name;
    return false;
}
uint64_t identity(){if(next_identity==UINT64_MAX)throw std::bad_alloc();return next_identity++;}
}
struct MeleeWebIo {
    std::map<uint64_t,std::shared_ptr<Block>> blocks;
    std::deque<Request> requests;
    size_t bytes=0;
    bool pumping=false;
};
namespace {
std::shared_ptr<Block> endpoint(MeleeWebIo* io,MeleeWebIoEndpoint e,size_t length){
    auto found=io->blocks.find(e.handle);
    if(found==io->blocks.end()||found->second->kind!=e.kind)return {};
    const auto& b=found->second;
    if(e.offset>b->size||length>b->size-e.offset)return {};
    return b;
}
int add(MeleeWebIo* io,MeleeWebIoKind kind,const char* name,const void* input,size_t length,uint64_t* handle,char* error,size_t size){
    if(!io||!handle||!length||length>max_bytes||length>max_bytes-io->bytes||io->blocks.size()>=max_objects)
        return fail(error,size,"I/O allocation exceeds its owned storage budget or has invalid arguments");
    try{
        auto b=std::make_shared<Block>();b->kind=kind;b->size=length;
        const size_t aligned=(length+31)&~size_t{31};
        b->bytes.reset(static_cast<unsigned char*>(std::aligned_alloc(32,aligned)));
        if(!b->bytes)throw std::bad_alloc();
        if(name)b->name=name;
        if(input)std::memcpy(b->bytes.get(),input,length);else std::memset(b->bytes.get(),0,length);
        uint64_t id=identity();io->blocks.emplace(id,std::move(b));io->bytes+=length;*handle=id;return ok(error,size);
    }catch(const std::bad_alloc&){return fail(error,size,"Cannot allocate owned I/O storage");}
}
}
extern "C" {
MeleeWebIo* melee_web_io_create(char* error,size_t size){
    auto* io=new(std::nothrow)MeleeWebIo;
    if(!io){fail(error,size,"Cannot allocate I/O context");return nullptr;}
    ok(error,size);return io;
}
int melee_web_io_add_file(MeleeWebIo* io,const char* name,const void* bytes,size_t length,uint64_t* handle,char* error,size_t size){
    if(!io||!valid_name(name)||!bytes)return fail(error,size,"I/O file requires a bounded nonempty name and actual bytes");
    for(const auto& [id,b]:io->blocks)if(b->kind==MELEE_WEB_IO_FILE&&b->name==name)return fail(error,size,"I/O file name already registered");
    return add(io,MELEE_WEB_IO_FILE,name,bytes,length,handle,error,size);
}
int melee_web_io_find_file(MeleeWebIo* io,const char* name,uint64_t* handle,size_t* length,char* error,size_t size){
    if(!io||!valid_name(name)||!handle||!length)return fail(error,size,"Invalid I/O file lookup arguments");
    for(const auto& [id,b]:io->blocks)if(b->kind==MELEE_WEB_IO_FILE&&b->name==name){*handle=id;*length=b->size;return ok(error,size);}
    return fail(error,size,"Requested local I/O file is not registered");
}
int melee_web_io_add_buffer(MeleeWebIo* io,MeleeWebIoKind kind,size_t length,uint64_t* handle,char* error,size_t size){
    if(kind!=MELEE_WEB_IO_MEMORY&&kind!=MELEE_WEB_IO_AUXILIARY)return fail(error,size,"I/O buffer requires an explicit memory or auxiliary kind");
    return add(io,kind,nullptr,nullptr,length,handle,error,size);
}
int melee_web_io_buffer(MeleeWebIo* io,uint64_t handle,void** bytes,size_t* length,char* error,size_t size){
    if(!io||!bytes||!length)return fail(error,size,"Invalid I/O buffer lookup arguments");
    auto it=io->blocks.find(handle);
    if(it==io->blocks.end()||it->second->kind==MELEE_WEB_IO_FILE)return fail(error,size,"I/O handle is not an owned writable buffer");
    *bytes=it->second->bytes.get();*length=it->second->size;return ok(error,size);
}
int melee_web_io_remove(MeleeWebIo* io,uint64_t handle,char* error,size_t size){
    if(!io)return fail(error,size,"I/O context is missing");
    auto it=io->blocks.find(handle);
    if(it==io->blocks.end())return fail(error,size,"I/O handle is stale or belongs to another context");
    if(it->second.use_count()!=1)return fail(error,size,"I/O storage is retained by a queued transfer");
    io->bytes-=it->second->size;io->blocks.erase(it);return ok(error,size);
}
int melee_web_io_submit(MeleeWebIo* io,MeleeWebIoEndpoint source,MeleeWebIoEndpoint dest,size_t length,MeleeWebIoCallback cb,void* user,uint64_t* request,char* error,size_t size){
    if(!io||!request||!length||io->requests.size()>=max_requests||dest.kind==MELEE_WEB_IO_FILE)
        return fail(error,size,"Invalid I/O transfer or queue capacity exceeded");
    auto src=endpoint(io,source,length),dst=endpoint(io,dest,length);
    if(!src||!dst)return fail(error,size,"I/O transfer kind, handle or complete byte range is invalid");
    try{
        uint64_t id=identity();io->requests.push_back({id,std::move(src),std::move(dst),source.offset,dest.offset,length,cb,user,false});
        *request=id;return ok(error,size);
    }catch(const std::bad_alloc&){return fail(error,size,"Cannot allocate queued I/O request");}
}
int melee_web_io_cancel(MeleeWebIo* io,uint64_t request,char* error,size_t size){
    if(!io)return fail(error,size,"I/O context is missing");
    for(auto& r:io->requests)if(r.id==request){r.cancelled=true;return ok(error,size);}
    return fail(error,size,"I/O request is no longer queued");
}
int melee_web_io_pump(MeleeWebIo* io,uint32_t limit,uint32_t* completed,char* error,size_t size){
    if(!io||!completed||io->pumping||!limit)return fail(error,size,"I/O pump requires a context, positive budget and no reentry");
    const size_t count=std::min(size_t{limit},io->requests.size());io->pumping=true;*completed=0;
    for(size_t i=0;i<count;i++){
        Request r=std::move(io->requests.front());io->requests.pop_front();
        if(!r.cancelled)std::memmove(r.dest->bytes.get()+r.dest_offset,r.source->bytes.get()+r.source_offset,r.length);
        // Storage is still retained while the callback observes completion.
        if(r.callback)r.callback(r.id,r.cancelled?MELEE_WEB_IO_CANCELLED:MELEE_WEB_IO_COMPLETE,r.cancelled?0:r.length,r.user);
        ++*completed;
    }
    io->pumping=false;return ok(error,size);
}
size_t melee_web_io_pending(const MeleeWebIo* io){return io?io->requests.size():0;}
int melee_web_io_destroy(MeleeWebIo* io,char* error,size_t size){
    if(!io)return ok(error,size);
    if(io->pumping||!io->requests.empty())return fail(error,size,"I/O context still has active or queued transfers");
    delete io;return ok(error,size);
}
}
