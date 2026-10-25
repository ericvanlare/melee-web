/* One named original class allocator. No source bytes or restore authority. */
namespace {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
struct PieceCell { uint32_t geometry,tag,generation; };
struct PieceOwner {
 uint64_t epoch=0,world=0,instance=0,serial=0,capture=0;
 uint32_t begin=0,end=0,count=0,next_generation=0,state=0,reason=0;
 MeleeWebClassPieceEvent first{}; PieceCell* cells=nullptr;
} piece_owner;
static_assert(sizeof(PieceCell)==12);
#if UINTPTR_MAX==UINT32_MAX
static_assert(sizeof(PieceOwner)==120);
#endif
constexpr uint32_t piece_live=0x80000000u,piece_units=0x03ffffffu;
// states:0 unavailable,1 AwaitCold,2 Tracking,3 Armed,4 Refused.
// reasons:1 unavailable,2 not-cold,3 bounds,4 missing,5 conflict,
// 6 generation/serial exhausted,7 interval changed,8 topology/mirror,
// 9 stale owner/anchor,10 header,11 descriptor lifetime.
void piece_refuse(uint32_t reason,uint32_t kind=0,const HostAllocation* a=nullptr,
                  uintptr_t host=0,uint32_t requested=0,uint32_t physical=0,
                  uint32_t before=0,uint32_t after=0) {
 if(piece_owner.reason)return;
 piece_owner.reason=reason;piece_owner.state=4;
 piece_owner.first={piece_owner.serial,static_cast<uint64_t>(host),a?a->generation:0,
   kind,a?a->source.value()+static_cast<uint32_t>(host-a->host):0,requested,physical,before,after};
}
bool piece_event(uint32_t kind,const HostAllocation* a=nullptr,uintptr_t p=0,
                 uint32_t req=0,uint32_t physical=0,uint32_t before=0,uint32_t after=0) {
 if(!active||piece_owner.reason||!piece_owner.cells)return false;
 if(piece_owner.serial==UINT64_MAX){piece_refuse(6,kind,a,p,req,physical,before,after);return false;}
 ++piece_owner.serial;
 if(piece_owner.state==3){piece_refuse(7,kind,a,p,req,physical,before,after);return false;}
 if(piece_owner.state!=2){piece_refuse(2,kind,a,p,req,physical,before,after);return false;}
 return true;
}
void piece_init(uint32_t begin,uint32_t end) {
 const uint64_t instance=piece_owner.instance;
 std::free(piece_owner.cells);piece_owner={};piece_owner.instance=instance;
 if(instance==UINT64_MAX||begin>=end||(begin&31)||(end&31)||end-begin>0x7fffffe0u){piece_refuse(1);return;}
 ++piece_owner.instance;piece_owner.epoch=owner_epoch;piece_owner.world=world_generation;
 piece_owner.begin=begin;piece_owner.end=end;piece_owner.count=(end-begin)/32;
 if(piece_owner.count>SIZE_MAX/sizeof(PieceCell)){piece_refuse(1);return;}
 piece_owner.cells=static_cast<PieceCell*>(std::calloc(piece_owner.count,sizeof(PieceCell)));
 if(!piece_owner.cells){piece_refuse(1);return;}
 piece_owner.state=1;
}
void piece_end() {piece_owner.state=0;piece_owner.epoch=piece_owner.world=0;
 std::free(piece_owner.cells);piece_owner.cells=nullptr;}
bool piece_span(uintptr_t p,uint32_t physical,HostAllocation*& row,uint32_t& source,uint32_t& index) {
 row=find_host_allocation(p);
 if(!row||row->source.value()<piece_owner.begin||uint64_t(row->source.value())+row->requested>piece_owner.end||!physical||(physical&31)||(p&31)||p-row->host>row->requested||
    physical>row->requested-(p-row->host))return false;
 const uint64_t s=uint64_t(row->source.value())+(p-row->host);
 if(s<piece_owner.begin||s>=piece_owner.end||(s&31)||physical>piece_owner.end-s)return false;
 source=static_cast<uint32_t>(s);index=(source-piece_owner.begin)/32;
 return index<piece_owner.count;
}
uint32_t piece_extent(const PieceCell& c){return (c.geometry&piece_units)*32;}
uint32_t piece_requested(const PieceCell& c){return piece_extent(c)-((c.geometry>>26)&31);}
bool piece_topology(uint32_t kind,const HostAllocation* row=nullptr) {
 if(!active||!piece_owner.cells||piece_owner.reason)return false;
 if(piece_owner.serial==UINT64_MAX){piece_refuse(6,kind);return false;}
 ++piece_owner.serial;
 if(piece_owner.state==3){piece_refuse(8,kind,row,row?row->host:0,row?row->requested:0);return false;}
 return true;
}
void piece_clear_row(const HostAllocation& row) {
 if(!piece_topology(8,&row))return;
 const uint64_t b=row.source.value(),e=b+row.requested;
 if(b<piece_owner.begin||e>piece_owner.end){piece_refuse(3);return;}
 const uint32_t first=(b-piece_owner.begin)/32,last=(e-piece_owner.begin+31)/32;
 std::memset(piece_owner.cells+first,0,(last-first)*sizeof(PieceCell));
}
void piece_allocate(void* pointer,int32_t requested,uint32_t physical,uint32_t kind) {
 if(!active||!piece_owner.cells||piece_owner.reason)return;
 const auto p=reinterpret_cast<uintptr_t>(pointer);HostAllocation* row=nullptr;uint32_t source=0,index=0;
 if(requested<=0||physical<uint32_t(requested)||physical-uint32_t(requested)>31||
    !piece_span(p,physical,row,source,index)){piece_refuse(3,kind);return;}
 // All starts in the same outer ledger row, including starts before this piece.
 const uint32_t first=(row->source.value()-piece_owner.begin)/32;
 const uint32_t last=(uint64_t(row->source.value())+row->requested-piece_owner.begin+31)/32;
 for(uint32_t i=first;i<last;++i){const auto& c=piece_owner.cells[i];
  if((c.geometry&piece_live)&&uint64_t(i)*32< uint64_t(index)*32+physical&&
     uint64_t(index)*32<uint64_t(i)*32+piece_extent(c)){piece_refuse(5,kind,row,p,requested,physical);return;}}
 if(piece_owner.next_generation==UINT32_MAX){piece_refuse(6,kind,row,p,requested,physical);return;}
 if(!piece_event(kind,row,p,requested,physical))return;
 piece_owner.cells[index]={piece_live|(physical/32)|((physical-uint32_t(requested))<<26),0,++piece_owner.next_generation};
}
#endif
}
static_assert(sizeof(MeleeWebClassPieceAnchor)==40);
static_assert(sizeof(MeleeWebClassPieceEvent)==48);
static_assert(sizeof(MeleeWebClassPieceWitness)==208);
static_assert(offsetof(MeleeWebClassPieceWitness,first_event)==144);
static_assert(offsetof(MeleeWebClassPieceWitness,reserved)==192);
static_assert(offsetof(MeleeWebClassPieceAnchor,ready)==32);
extern "C" void melee_web_class_piece_cold(const void* list,int32_t count) {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(!active||!piece_owner.cells||piece_owner.reason||piece_owner.state!=1)return;
 if(list||count){piece_refuse(2);return;}piece_owner.state=2;
#else
 (void)list;(void)count;
#endif
}
extern "C" void melee_web_class_piece_alloc(void* p,int32_t req,uint32_t extent) {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 piece_allocate(p,req,extent,1);
#else
 (void)p;(void)req;(void)extent;
#endif
}
extern "C" void melee_web_class_piece_promote(void* p,int32_t req) {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(!active||piece_owner.reason)return;
 auto* row=find_host_allocation(reinterpret_cast<uintptr_t>(p));
 if(!row||row->host!=reinterpret_cast<uintptr_t>(p)||req<=0||row->requested!=uint32_t(req)||(req&31)){
  piece_refuse(3,2);return;}
 piece_allocate(p,req,req,2);
#else
 (void)p;(void)req;
#endif
}
extern "C" void melee_web_class_piece_free(void* p,int32_t req) {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(!active||!piece_owner.cells||piece_owner.reason||!p)return;
 HostAllocation* row=nullptr;uint32_t source=0,index=0;
 if(req<=0||uint32_t(req)>0x7fffffc0u||!piece_span(reinterpret_cast<uintptr_t>(p),(uint32_t(req)+31)&~31u,row,source,index)){
  piece_refuse(3,3);return;}
 auto& c=piece_owner.cells[index];
 if(!(c.geometry&piece_live)||piece_requested(c)!=uint32_t(req)){piece_refuse(4,3,row,reinterpret_cast<uintptr_t>(p),req);return;}
 if(piece_event(3,row,reinterpret_cast<uintptr_t>(p),req,piece_extent(c)))c={};
#else
 (void)p;(void)req;
#endif
}
extern "C" void melee_web_class_piece_label(void* p,const void* info,int32_t size,uint32_t stage,const void* previous) {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(!active||!piece_owner.cells||piece_owner.reason)return;
 const uintptr_t tag=reinterpret_cast<uintptr_t>(info),host=reinterpret_cast<uintptr_t>(p);
 HostAllocation* row=nullptr;uint32_t source=0,index=0;
 if(size<4||size>32767||!tag||tag>UINT32_MAX||(tag&3)||!piece_span(host,(uint32_t(size)+31)&~31u,row,source,index)){
  piece_refuse(3,stage+3);return;}
 auto& c=piece_owner.cells[index];
 if(!(c.geometry&piece_live)||piece_requested(c)!=uint32_t(size)||stage<1||stage>3||
    (stage==1&&c.tag)||(stage==2&&c.tag!=(uint32_t(tag)|1))||
    (stage==3&&((c.tag&3)!=2||(c.tag&~3u)!=reinterpret_cast<uintptr_t>(previous)))){piece_refuse(4,stage+3,row,host,size);return;}
 const uint32_t before=c.tag&~3u;
 if(stage!=1){uint32_t header=0;std::memcpy(&header,p,4);if(header!=tag){piece_refuse(10,stage+3,row,host,size);return;}}
 if(piece_event(stage+3,row,host,size,piece_extent(c),before,tag))c.tag=uint32_t(tag)|(stage==1?1u:2u);
#else
 (void)p;(void)info;(void)size;(void)stage;(void)previous;
#endif
}
extern "C" void melee_web_class_piece_descriptor(const void* info) {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(!active||!piece_owner.cells||piece_owner.reason)return;
 const uintptr_t tag=reinterpret_cast<uintptr_t>(info);
 for(uint32_t i=0;i<piece_owner.count;++i)if((piece_owner.cells[i].geometry&piece_live)&&
   (piece_owner.cells[i].tag&~3u)==tag){piece_refuse(11,7);return;}
 // Descriptor setup before the cold allocator handshake is legitimate.
 if(piece_owner.state==1){piece_topology(7);return;}
 piece_event(7,nullptr,0,0,0,0,static_cast<uint32_t>(tag));
#else
 (void)info;
#endif
}
extern "C" void melee_web_class_piece_amnesia() {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(piece_topology(9))piece_refuse(8,9);
#endif
}
extern "C" MeleeWebClassPieceAnchor melee_web_class_piece_arm() {
 MeleeWebClassPieceAnchor a{};
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(piece_owner.state==3)piece_refuse(9,11);
 a={owner_epoch,world_generation,piece_owner.instance,piece_owner.serial,0,piece_owner.reason};
 if(!active||!healthy||piece_owner.state!=2||piece_owner.reason){if(!a.reason)a.reason=9;return a;}
 piece_owner.capture=piece_owner.serial;piece_owner.state=3;a.ready=1;
#endif
 return a;
}
extern "C" void melee_web_class_piece_disarm(MeleeWebClassPieceAnchor a) {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(a.ready&&a.epoch==piece_owner.epoch&&a.world==piece_owner.world&&a.instance==piece_owner.instance&&
    a.serial==piece_owner.capture&&piece_owner.state==3)piece_owner.state=2;
#else
 (void)a;
#endif
}
