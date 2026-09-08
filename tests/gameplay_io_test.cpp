#include "gameplay_io.h"
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
struct Callbacks {
    MeleeWebIo* io;
    MeleeWebIoEndpoint source,dest;
    std::vector<uint64_t> ids;
    std::vector<MeleeWebIoResult> results;
    std::vector<size_t> sizes;
    bool enqueue=false;
    uint64_t followup=0;
};
void callback(uint64_t id,MeleeWebIoResult result,size_t size,void* user){
    auto& s=*static_cast<Callbacks*>(user);char error[256];uint32_t completed=99;
    check(!melee_web_io_pump(s.io,1,&completed,error,sizeof(error)),"Reentrant pump rejected");
    check(!melee_web_io_destroy(s.io,error,sizeof(error)),"Callback-time destruction rejected");
    check(!melee_web_io_remove(s.io,s.dest.handle,error,sizeof(error)),"Active callback destination retained");
    s.ids.push_back(id);s.results.push_back(result);s.sizes.push_back(size);
    if(s.enqueue){s.enqueue=false;check(melee_web_io_submit(s.io,s.source,s.dest,4,callback,&s,&s.followup,error,sizeof(error)),error);}
}
int main(){try{
    char error[256];auto* io=melee_web_io_create(error,sizeof(error));check(io,error);
    std::array<unsigned char,64> source;for(unsigned i=0;i<source.size();i++)source[i]=i+1;
    uint64_t file,memory,aux,found;size_t len;
    check(melee_web_io_add_file(io,"/EfMr.dat",source.data(),source.size(),&file,error,sizeof(error)),error);
    source.fill(0); // Owned file bytes must not retain the caller's input span.
    check(melee_web_io_find_file(io,"/EfMr.dat",&found,&len,error,sizeof(error))&&found==file&&len==64,"Stable file identity and size");
    check(!melee_web_io_find_file(io,"missing",&found,&len,error,sizeof(error)),"Missing file rejected");
    check(!melee_web_io_add_file(io,"/EfMr.dat",source.data(),64,&found,error,sizeof(error)),"Duplicate filename rejected");
    check(melee_web_io_add_buffer(io,MELEE_WEB_IO_MEMORY,64,&memory,error,sizeof(error)),error);
    check(melee_web_io_add_buffer(io,MELEE_WEB_IO_AUXILIARY,64,&aux,error,sizeof(error)),error);
    void* output;check(melee_web_io_buffer(io,memory,&output,&len,error,sizeof(error)),error);
    check(reinterpret_cast<uintptr_t>(output)%32==0&&len==64,"Owned buffer alignment and length");
    for(unsigned i=0;i<len;i++)check(static_cast<unsigned char*>(output)[i]==0,"Explicit zero-initialized buffer");
    Callbacks state{io,{MELEE_WEB_IO_FILE,file,0},{MELEE_WEB_IO_MEMORY,memory,0},{},{},{},false,0};uint64_t first,second;
    check(melee_web_io_submit(io,state.source,{MELEE_WEB_IO_AUXILIARY,aux,0},64,nullptr,nullptr,&first,error,sizeof(error)),error);
    check(melee_web_io_submit(io,{MELEE_WEB_IO_AUXILIARY,aux,0},state.dest,64,callback,&state,&second,error,sizeof(error)),error);
    check(state.ids.empty()&&melee_web_io_pending(io)==2&&static_cast<unsigned char*>(output)[0]==0,"Posting never completes inline");
    check(!melee_web_io_remove(io,file,error,sizeof(error)),"Queued source cannot disappear");
    check(!melee_web_io_destroy(io,error,sizeof(error)),"Pending work cannot be silently dropped");
    uint32_t completed;check(melee_web_io_pump(io,1,&completed,error,sizeof(error))&&completed==1,"Bounded pump");
    check(state.ids.empty()&&static_cast<unsigned char*>(output)[0]==0,"FIFO first leg only");
    check(melee_web_io_pump(io,16,&completed,error,sizeof(error))&&completed==1,"FIFO second leg");
    check(state.ids==std::vector<uint64_t>{second}&&state.results[0]==MELEE_WEB_IO_COMPLETE&&state.sizes[0]==64,"Completion identity and actual byte count");
    for(unsigned i=0;i<64;i++)check(static_cast<unsigned char*>(output)[i]==i+1,"File through auxiliary to memory bytes");
    // Reject every invalid transfer before touching the destination or invoking callbacks.
    uint64_t ignored;
    check(!melee_web_io_submit(io,{MELEE_WEB_IO_FILE,file,60},state.dest,8,callback,&state,&ignored,error,sizeof(error)),"EOF overread rejected");
    check(!melee_web_io_submit(io,state.source,{MELEE_WEB_IO_MEMORY,memory,63},2,callback,&state,&ignored,error,sizeof(error)),"Destination overrun rejected");
    check(!melee_web_io_submit(io,state.source,state.dest,std::numeric_limits<size_t>::max(),callback,&state,&ignored,error,sizeof(error)),"Length overflow rejected");
    check(!melee_web_io_submit(io,{MELEE_WEB_IO_FILE,file,std::numeric_limits<size_t>::max()},state.dest,1,callback,&state,&ignored,error,sizeof(error)),"Offset overflow rejected");
    check(!melee_web_io_submit(io,{MELEE_WEB_IO_MEMORY,file,0},state.dest,4,callback,&state,&ignored,error,sizeof(error)),"Numerically identical wrong-kind handle rejected");
    check(!melee_web_io_submit(io,state.dest,state.source,4,callback,&state,&ignored,error,sizeof(error)),"File destination rejected");
    check(state.ids.size()==1&&!melee_web_io_pending(io),"Invalid requests produce no success callbacks");
    // Cancellation completes explicitly without modifying bytes.
    std::memset(output,0,64);check(melee_web_io_submit(io,state.source,state.dest,16,callback,&state,&first,error,sizeof(error)),error);
    check(melee_web_io_cancel(io,first,error,sizeof(error)),error);check(melee_web_io_pump(io,1,&completed,error,sizeof(error)),error);
    check(state.results.back()==MELEE_WEB_IO_CANCELLED&&state.sizes.back()==0&&static_cast<unsigned char*>(output)[0]==0,"Cancellation moves no bytes");
    check(!melee_web_io_cancel(io,first,error,sizeof(error)),"Completed request cannot be cancelled again");
    state.enqueue=true;check(melee_web_io_submit(io,state.source,state.dest,4,callback,&state,&first,error,sizeof(error)),error);
    check(melee_web_io_pump(io,99,&completed,error,sizeof(error))&&completed==1&&melee_web_io_pending(io)==1,"Callback submission waits until next pump");
    check(melee_web_io_pump(io,99,&completed,error,sizeof(error))&&completed==1&&state.ids.back()==state.followup,"Followup completes on next pump");
    // Actual memmove semantics for same-buffer overlap.
    check(melee_web_io_submit(io,state.dest,{MELEE_WEB_IO_MEMORY,memory,1},3,nullptr,nullptr,&first,error,sizeof(error)),error);
    check(melee_web_io_pump(io,1,&completed,error,sizeof(error)),error);
    check(!std::memcmp(output,"\1\1\2\3",4),"Overlapping owned-memory copy");
    check(melee_web_io_remove(io,file,error,sizeof(error)),error);
    check(!melee_web_io_submit(io,state.source,state.dest,4,nullptr,nullptr,&ignored,error,sizeof(error)),"Removed source handle rejected");
    check(melee_web_io_destroy(io,error,sizeof(error)),error);
    io=melee_web_io_create(error,sizeof(error));check(io,error);check(melee_web_io_add_buffer(io,MELEE_WEB_IO_MEMORY,64,&found,error,sizeof(error)),error);
    check(found!=memory&&!melee_web_io_submit(io,state.dest,{MELEE_WEB_IO_MEMORY,found,0},4,nullptr,nullptr,&ignored,error,sizeof(error)),"Restart does not reuse stale handle identities");
    check(melee_web_io_destroy(io,error,sizeof(error)),error);
    std::cout<<"Owned local-file/typed transfer bytes, FIFO callbacks, cancellation, bounds and restart passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
