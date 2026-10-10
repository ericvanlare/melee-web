#include "gameplay_archive_sections.h"
#include "gameplay_bootstrap.h"
static uint64_t test_generation;
static int test_heap_exists;
int melee_web_gameplay_world_exists(void){return test_heap_exists;}
MeleeWebGameplayStats melee_web_gameplay_stats(void){MeleeWebGameplayStats s={0};s.generation=test_generation;return s;}
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static void load(void* archive,const char* file,void* destination,...) {
    va_list args;va_start(args,destination);melee_web_archive_sections_load(archive,file,destination,args);va_end(args);
}
static void heap_source_membership_controls(void) {
    char error[128];
    int root=7;
    MeleeWebArchiveSymbol toy={"TyDatai.usd","tyInitModelTbl",&root};
    MeleeWebArchiveSymbol other={"TyDatai.dat","tyInitModelTbl",&root};
    test_generation=41;test_heap_exists=1;
    MeleeWebArchiveSections* owner=melee_web_archive_sections_register_heap(&toy,1,error,sizeof(error));
    MeleeWebArchiveSections* foreign=melee_web_archive_sections_register_heap(&other,1,error,sizeof(error));
    assert(owner&&foreign);
    assert(melee_web_archive_sections_heap_scope_matches(owner,41));
    assert(!melee_web_archive_sections_heap_scope_matches(owner,42));
    assert(!melee_web_archive_sections_heap_scope_matches((const MeleeWebArchiveSections*)(uintptr_t)0x4321,41));
    /* This address is intentionally opaque; the membership API must compare it only. */
    void* source=(void*)(uintptr_t)0x1234;
    assert(melee_web_archive_sections_attach_source(source,"TyDatai.usd"));
    assert(melee_web_archive_sections_heap_source_matches(owner,41,source,"TyDatai.usd"));
    assert(!melee_web_archive_sections_heap_source_matches(foreign,41,source,"TyDatai.usd"));
    assert(!melee_web_archive_sections_heap_source_matches(owner,42,source,"TyDatai.usd"));
    assert(!melee_web_archive_sections_heap_source_matches(owner,41,source,"TyDatai.dat"));
    assert(!melee_web_archive_sections_heap_source_matches((const MeleeWebArchiveSections*)(uintptr_t)0x4321,
                                                            41,source,"TyDatai.usd"));
    void* copied=melee_web_archive_sections_open("TyDatai.usd");
    assert(copied&&!melee_web_archive_sections_heap_source_matches(owner,41,copied,"TyDatai.usd"));
    assert(melee_web_archive_sections_public(copied,"tyInitModelTbl")==&root);
    test_heap_exists=0;
    assert(!melee_web_archive_sections_heap_scope_matches(owner,41));
    assert(!melee_web_archive_sections_heap_source_matches(owner,41,source,"TyDatai.usd"));
    test_heap_exists=1;
    assert(melee_web_archive_sections_heap_source_matches(owner,41,source,"TyDatai.usd"));
    melee_web_archive_sections_release(copied);
    test_heap_exists=0;
    assert(melee_web_archive_sections_close(owner,error,sizeof(error)));
    assert(melee_web_archive_sections_close(foreign,error,sizeof(error)));
    assert(!melee_web_archive_sections_heap_scope_matches(owner,41));
    assert(!melee_web_archive_sections_heap_source_matches(owner,41,source,"TyDatai.usd"));
    test_generation=2;test_heap_exists=1;
    puts("Synthetic heap-source membership controls rejected stale, foreign, wrong-file and copied handles without reading candidate bytes");
}
static void stadium_sis_catalog_controls(void) {
    char error[128];
    int roots[8]={0};
    unsigned char text[]={0};
    void** sis=calloc(3,sizeof(*sis));assert(sis);
    sis[2]=text;
    MeleeWebArchiveSymbol symbols[]={
        {"GrPs.usd","map_head",&roots[0]},
        {"GrPs.usd","coll_data",&roots[1]},
        {"GrPs.usd","grGroundParam",&roots[2]},
        {"GrPs.usd","ALDYakuAll",&roots[3]},
        {"GrPs.usd","map_ptcl",&roots[4]},
        {"GrPs.usd","map_texg",&roots[5]},
        {"GrPs.usd","yakumono_param",&roots[6]},
        {"GrPs.usd","quake_model_set",&roots[7]},
        {"GrPs.usd","SIS_GrPStadiumData",sis},
    };
    /* Asset-free owned table; test catalog borrowing, not DAT hydration. */
    MeleeWebArchiveSections* missing=melee_web_archive_sections_register(
        symbols,8,error,sizeof(error));assert(missing);
    void* handle=melee_web_archive_sections_open("GrPs.usd");
    assert(melee_web_archive_sections_public(handle,"SIS_GrPStadiumData")==NULL);
    assert(!melee_web_archive_sections_close(missing,error,sizeof(error)));
    assert(melee_web_archive_sections_close_owned(missing,handle,error,sizeof(error)));
    assert(!melee_web_archive_sections_is_handle(handle));

    MeleeWebArchiveSections* complete=melee_web_archive_sections_register(
        symbols,9,error,sizeof(error));assert(complete);
    handle=melee_web_archive_sections_open("GrPs.usd");
    assert(melee_web_archive_sections_public(handle,"SIS_GrPStadiumData")==sis);
    assert(melee_web_archive_sections_public(handle,"SIS_GrPStadiumDat")==NULL);
    assert(!melee_web_archive_sections_open_preloaded("GrPStadium.usd"));
    int wrong_file=0;
    assert(!melee_web_archive_sections_attach_source(&wrong_file,"GrPStadium.usd"));
    assert(!melee_web_archive_sections_is_handle(&wrong_file));
    void* consumer=melee_web_archive_sections_open("GrPs.usd");
    assert(!melee_web_archive_sections_close(complete,error,sizeof(error)));
    assert(!melee_web_archive_sections_close_owned(complete,handle,error,sizeof(error)));
    assert(melee_web_archive_sections_is_handle(handle));
    assert(melee_web_archive_sections_public(consumer,"SIS_GrPStadiumData")==sis);
    assert(((void**)melee_web_archive_sections_public(handle,"SIS_GrPStadiumData"))[2]==text);
    melee_web_archive_sections_release(consumer);
    assert(melee_web_archive_sections_close_owned(complete,handle,error,sizeof(error)));
    assert(!melee_web_archive_sections_is_handle(handle));
    assert(sis[2]==text && *(unsigned char*)sis[2]==0);
    /* Catalog removal releases handles, never the borrowed descriptor graph. */
    free(sis);
    puts("Stadium eight-entry missing SIS and nine-entry owned SIS catalog controls passed");
}
static void stage_map_preload_retirement_controls(void) {
    char error[128];
    int map_head=13,other_root=17,source_object=0;
    typedef struct {void* archive;void* map;unsigned flags;} SlotWitness;
    MeleeWebArchiveSymbol map_symbols[]={
        {"GrPs.usd","map_head",&map_head},
        {"GrPs.usd","grGroundParam",&other_root},
    };
    test_generation=71;test_heap_exists=1;
    MeleeWebArchiveSections* scope=melee_web_archive_sections_register(
        map_symbols,2,error,sizeof(error));assert(scope);
    void* native_owner=melee_web_archive_sections_open("GrPs.usd");assert(native_owner);
    void* preload=melee_web_archive_sections_open_preloaded("/GrPs.usd");assert(preload);
    uint64_t identity=0,native_identity=0,native_out=0;
    assert(melee_web_archive_sections_preloaded_stage_map_matches(
        scope,71,preload,native_owner,&map_head,0,&identity,0,&native_out));
    native_identity=native_out;
    assert(identity!=0&&native_identity!=0);

    /* Registry close preflight is read-only. This local row-shaped witness
     * is not passed to StageMap and therefore does not test the armed
     * StageMap retirement transaction; that needs its real composed trace. */
    SlotWitness slots[4]={{preload,&map_head,0},{0},{0},{0}};
    SlotWitness slots_before[4];memcpy(slots_before,slots,sizeof(slots));
    void* extra_open=melee_web_archive_sections_open("GrPs.usd");assert(extra_open);
    assert(!melee_web_archive_sections_close_owned_preflight(scope,native_owner));
    assert(memcmp(slots,slots_before,sizeof(slots))==0);
    uint64_t still_same=0;
    assert(melee_web_archive_sections_preloaded_stage_map_matches(
        scope,71,preload,native_owner,&map_head,identity,&still_same,0,&native_out));
    assert(still_same==identity&&melee_web_archive_sections_is_handle(extra_open));
    melee_web_archive_sections_release(extra_open);
    assert(melee_web_archive_sections_close_owned_preflight(scope,native_owner));

    uint64_t output_sentinel=UINT64_C(0xfeedbeef12345678);
    assert(!melee_web_archive_sections_preloaded_stage_map_matches(
        scope,72,preload,native_owner,&map_head,0,&output_sentinel,0,&native_out));
    assert(output_sentinel==UINT64_C(0xfeedbeef12345678));
    assert(!melee_web_archive_sections_preloaded_stage_map_matches(
        scope,71,(void*)(uintptr_t)0x4321,native_owner,&map_head,0,&output_sentinel,0,&native_out));
    assert(output_sentinel==UINT64_C(0xfeedbeef12345678));

    void* owned_copy=melee_web_archive_sections_open("GrPs.usd");assert(owned_copy);
    assert(!melee_web_archive_sections_preloaded_stage_map_matches(
        scope,71,owned_copy,native_owner,&map_head,0,&output_sentinel,0,&native_out));
    assert(output_sentinel==UINT64_C(0xfeedbeef12345678));
    assert(melee_web_archive_sections_attach_source(&source_object,"GrPs.usd"));
    assert(!melee_web_archive_sections_preloaded_stage_map_matches(
        scope,71,&source_object,native_owner,&map_head,0,&output_sentinel,0,&native_out));
    assert(output_sentinel==UINT64_C(0xfeedbeef12345678));

    /* Both identities are required even when the current numeric pointer is
     * valid. This rejects replacement ownership independently of malloc reuse. */
    uint64_t native_sentinel=UINT64_C(0x12345678feedbeef);
    assert(!melee_web_archive_sections_preloaded_stage_map_matches(
        scope,71,preload,native_owner,&map_head,identity,&output_sentinel,
        native_identity+1,&native_sentinel));
    assert(output_sentinel==UINT64_C(0xfeedbeef12345678));
    assert(native_sentinel==UINT64_C(0x12345678feedbeef));
    melee_web_archive_sections_release(native_owner);
    native_owner=melee_web_archive_sections_open("GrPs.usd");assert(native_owner);
    assert(!melee_web_archive_sections_preloaded_stage_map_matches(
        scope,71,preload,native_owner,&map_head,identity,&output_sentinel,
        native_identity,&native_sentinel));
    assert(output_sentinel==UINT64_C(0xfeedbeef12345678));
    assert(native_sentinel==UINT64_C(0x12345678feedbeef));
    assert(melee_web_archive_sections_preloaded_stage_map_matches(
        scope,71,preload,native_owner,&map_head,identity,&output_sentinel,
        0,&native_out));
    assert(native_out!=native_identity);
    native_identity=native_out;
    output_sentinel=UINT64_C(0xfeedbeef12345678);

    /* Reopen receives a new registry incarnation even if malloc reuses the
     * released handle cell; a stale captured identity cannot pass. */
    melee_web_archive_sections_release(preload);
    void* replacement=melee_web_archive_sections_open_preloaded("GrPs.usd");
    assert(replacement);
    uint64_t replacement_identity=0;
    assert(melee_web_archive_sections_preloaded_stage_map_matches(
        scope,71,replacement,native_owner,&map_head,0,&replacement_identity,0,&native_out));
    assert(replacement_identity!=identity);
    assert(!melee_web_archive_sections_preloaded_stage_map_matches(
        scope,71,replacement,native_owner,&map_head,identity,&output_sentinel,0,&native_out));
    assert(output_sentinel==UINT64_C(0xfeedbeef12345678));
    assert(melee_web_archive_sections_preloaded_stage_map_matches(
        scope,71,replacement,native_owner,&map_head,replacement_identity,&output_sentinel,0,&native_out));
    assert(output_sentinel==replacement_identity);

    melee_web_archive_sections_release(replacement);
    melee_web_archive_sections_release(owned_copy);
    melee_web_archive_sections_release(&source_object);
    assert(melee_web_archive_sections_close_owned(scope,native_owner,error,sizeof(error)));
    assert(!melee_web_archive_sections_is_handle(replacement));

    /* A second scope naming the same file invalidates unique map ownership.
     * Keep this a separate subcontrol so its registry collision does not also
     * prevent the replacement-incarnation test above from reopening a preload. */
    MeleeWebArchiveSections* unique_scope=melee_web_archive_sections_register(
        map_symbols,2,error,sizeof(error));assert(unique_scope);
    void* unique_native=melee_web_archive_sections_open("GrPs.usd");assert(unique_native);
    void* unique_preload=melee_web_archive_sections_open_preloaded("GrPs.usd");assert(unique_preload);
    MeleeWebArchiveSymbol foreign_symbol={"GrPs.usd","other_public",&other_root};
    MeleeWebArchiveSections* foreign=melee_web_archive_sections_register(
        &foreign_symbol,1,error,sizeof(error));assert(foreign);
    uint64_t foreign_out=UINT64_C(0xfeedbeef12345678);
    assert(!melee_web_archive_sections_preloaded_stage_map_matches(
        unique_scope,71,unique_preload,unique_native,&map_head,0,&foreign_out,0,&native_out));
    assert(foreign_out==UINT64_C(0xfeedbeef12345678));
    assert(melee_web_archive_sections_is_handle(unique_native));
    assert(melee_web_archive_sections_is_handle(unique_preload));
    assert(melee_web_archive_sections_close_owned(unique_scope,unique_native,error,sizeof(error)));
    assert(melee_web_archive_sections_close(foreign,error,sizeof(error)));
    test_heap_exists=0;
    puts("StageMap preload registry proof rejected foreign/replaced handles and released once before owned-scope close");
}
int main(int argc,char** argv) {
    char error[128];int a=17,b=29;char filename[]="Authored.dat",symbol[]="first";
    MeleeWebArchiveSymbol symbols[]={{filename,symbol,&a},{filename,"alias",&a}};
    for(unsigned pass=0;pass<2;pass++) {
        MeleeWebArchiveSections* h=melee_web_archive_sections_register(symbols,2,error,sizeof(error));assert(h);
        assert(!melee_web_archive_sections_register(symbols,2,error,sizeof(error)));
        filename[0]='x';symbol[0]='x';
        MeleeWebArchiveSymbol second={"Second.dat","second",&b};
        MeleeWebArchiveSections* other=melee_web_archive_sections_register(&second,1,error,sizeof(error));assert(other);
        int source_archive_storage=0;
        assert(melee_web_archive_sections_attach_source(&source_archive_storage,"Second.dat"));
        assert(melee_web_archive_sections_is_handle(&source_archive_storage));
        assert(melee_web_archive_sections_is_source_archive(&source_archive_storage));
        assert(melee_web_archive_sections_public(&source_archive_storage,"second")==&b);
        melee_web_archive_sections_release(&source_archive_storage);
        assert(!melee_web_archive_sections_is_handle(&source_archive_storage));
        void* one=NULL;void* alias=NULL;
        if(argc==2&&!strcmp(argv[1],"missing"))load(NULL,"Authored.dat",&one,"first",&alias,"absent",NULL);
        if(argc==2&&!strcmp(argv[1],"unknown"))melee_web_archive_sections_open("Absent.dat");
        if(argc==2&&!strcmp(argv[1],"invalid"))melee_web_archive_sections_public(&a,"first");
        void* opened=NULL;
        load(&opened,"Authored.dat",&alias,"first",NULL);
        assert(alias==&a&&opened);
        assert(melee_web_archive_sections_public(opened,"alias")==&a);
        assert(melee_web_archive_sections_public(opened,"absent")==NULL);
        void* twice=melee_web_archive_sections_open("Authored.dat");
        assert(twice!=opened);
        assert(!melee_web_archive_sections_close_owned(h,opened,error,sizeof(error)));
        assert(!melee_web_archive_sections_close_owned(other,opened,error,sizeof(error)));
        assert(melee_web_archive_sections_public(opened,"first")==&a);
        assert(melee_web_archive_sections_public(twice,"alias")==&a);
        assert(!melee_web_archive_sections_close(h,error,sizeof(error)));
        melee_web_archive_sections_release(opened);
        if(argc==2&&!strcmp(argv[1],"released"))melee_web_archive_sections_public(opened,"first");
        assert(!melee_web_archive_sections_close(h,error,sizeof(error)));
        melee_web_archive_sections_release(twice);
        load(NULL,"Authored.dat",&one,"first",&alias,"alias",NULL);assert(one==&a&&alias==one);
        assert(melee_web_archive_sections_close(h,error,sizeof(error)));
        load(NULL,"Second.dat",&one,"second",NULL);assert(one==&b);
        void* owned=melee_web_archive_sections_open("Second.dat");
        assert(!melee_web_archive_sections_close_owned(other,&a,error,sizeof(error)));
        assert(melee_web_archive_sections_public(owned,"second")==&b);
    assert(melee_web_archive_sections_close_owned(other,owned,error,sizeof(error)));
    filename[0]='A';symbol[0]='f';
    }
    MeleeWebArchiveSymbol preloaded_symbol={"Preloaded.dat","root",&a};
    MeleeWebArchiveSections* preload_scope=melee_web_archive_sections_register(
        &preloaded_symbol,1,error,sizeof(error));assert(preload_scope);
    assert(!melee_web_archive_sections_open_preloaded("Absent.dat"));
    void* preloaded=melee_web_archive_sections_open_preloaded("Preloaded.dat");
    assert(preloaded&&melee_web_archive_sections_open_preloaded("Preloaded.dat")==preloaded);
    assert(melee_web_archive_sections_open_preloaded("/Preloaded.dat")==preloaded);
    assert(melee_web_archive_sections_public(preloaded,"root")==&a);
    void* ordinary=melee_web_archive_sections_open("Preloaded.dat");
    assert(ordinary!=preloaded);
    assert(!melee_web_archive_sections_close(preload_scope,error,sizeof(error)));
    assert(melee_web_archive_sections_is_handle(preloaded));
    assert(melee_web_archive_sections_close_owned(preload_scope,ordinary,error,sizeof(error)));
    assert(!melee_web_archive_sections_is_handle(ordinary));
    assert(!melee_web_archive_sections_is_handle(preloaded));
    MeleeWebArchiveSymbol catalog={"Extra.dat","source_options_root",NULL};
    MeleeWebArchiveSections* declared=melee_web_archive_sections_register(&catalog,1,error,sizeof(error));
    assert(declared && !melee_web_archive_sections_register(&catalog,1,error,sizeof(error)));
    void* extra=melee_web_archive_sections_open("Extra.dat");
    assert(melee_web_archive_sections_public(extra,"absent")==NULL);
    if(argc==2&&!strcmp(argv[1],"unhydrated"))melee_web_archive_sections_public(extra,"source_options_root");
    assert(!melee_web_archive_sections_close(declared,error,sizeof(error)));
    melee_web_archive_sections_release(extra);
    assert(melee_web_archive_sections_close(declared,error,sizeof(error)));
    assert(!melee_web_archive_sections_register_heap(&catalog,1,error,sizeof(error)));
    test_generation=1;test_heap_exists=1;
    declared=melee_web_archive_sections_register_heap(&catalog,1,error,sizeof(error));assert(declared);
    MeleeWebArchiveSymbol same_file={"Extra.dat","another_root",&a};
    assert(!melee_web_archive_sections_register(&same_file,1,error,sizeof(error)));
    extra=melee_web_archive_sections_open_preloaded("/Extra.dat");
    assert(extra&&melee_web_archive_sections_is_handle(extra));
    assert(!melee_web_archive_sections_close(declared,error,sizeof(error)));
    test_generation=0;
    assert(!melee_web_archive_sections_close(declared,error,sizeof(error)));
    test_heap_exists=0;
    assert(melee_web_archive_sections_close(declared,error,sizeof(error)));
    if(argc==2&&!strcmp(argv[1],"heap_released"))melee_web_archive_sections_public(extra,"absent");
    test_generation=2;test_heap_exists=1;
    int pdpm_owner=41;void* pdpm_root=&pdpm_owner;
    MeleeWebArchiveSymbol pdpm_symbol={"PdPm.dat","plLoadCommonData",&pdpm_root};
    MeleeWebArchiveSections* pdpm_scope=melee_web_archive_sections_register(
        &pdpm_symbol,1,error,sizeof(error));assert(pdpm_scope);
    void* pdpm_preloaded=melee_web_archive_sections_open_preloaded("PdPm.dat");
    assert(pdpm_preloaded&&pdpm_preloaded==
        melee_web_archive_sections_open_preloaded("PdPm.dat"));
    int pdpm_source_archive=0;
    assert(melee_web_archive_sections_attach_source(&pdpm_source_archive,"PdPm.dat"));
    void* pdpm_public=melee_web_archive_sections_public(&pdpm_source_archive,"plLoadCommonData");
    assert(pdpm_public==&pdpm_root&&*(void**)pdpm_public==&pdpm_owner);
    assert(!melee_web_archive_sections_close(pdpm_scope,error,sizeof(error)));
    assert(melee_web_archive_sections_is_handle(pdpm_preloaded));
    test_heap_exists=0;
    assert(melee_web_archive_sections_close(pdpm_scope,error,sizeof(error)));
    assert(!melee_web_archive_sections_is_handle(pdpm_preloaded));
    assert(!melee_web_archive_sections_is_handle(&pdpm_source_archive));
    test_heap_exists=1;
    MeleeWebArchiveSymbol source_catalog={"Source.dat","source_root",&a};
    MeleeWebArchiveSections* source_scope=melee_web_archive_sections_register(
        &source_catalog,1,error,sizeof(error));assert(source_scope);
    int source_archive_storage=0;
    assert(melee_web_archive_sections_attach_source(&source_archive_storage,"Source.dat"));
    assert(!melee_web_archive_sections_close(source_scope,error,sizeof(error)));
    test_heap_exists=0;
    assert(melee_web_archive_sections_close(source_scope,error,sizeof(error)));
    assert(!melee_web_archive_sections_is_handle(&source_archive_storage));
    heap_source_membership_controls();
    stadium_sis_catalog_controls();
    stage_map_preload_retirement_controls();
    puts("Typed archive sections copied names, resolved aliases, rejected duplicates and restarted");
}
