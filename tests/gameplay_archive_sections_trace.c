#include "gameplay_archive_sections.h"
#include "gameplay_bootstrap.h"
static uint64_t test_generation;
static int test_heap_exists;
int melee_web_gameplay_world_exists(void){return test_heap_exists;}
MeleeWebGameplayStats melee_web_gameplay_stats(void){MeleeWebGameplayStats s={0};s.generation=test_generation;return s;}
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void load(void* archive,const char* file,void* destination,...) {
    va_list args;va_start(args,destination);melee_web_archive_sections_load(archive,file,destination,args);va_end(args);
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
        assert(!melee_web_archive_sections_close(h,error,sizeof(error)));
        melee_web_archive_sections_release(opened);
        if(argc==2&&!strcmp(argv[1],"released"))melee_web_archive_sections_public(opened,"first");
        assert(!melee_web_archive_sections_close(h,error,sizeof(error)));
        melee_web_archive_sections_release(twice);
        load(NULL,"Authored.dat",&one,"first",&alias,"alias",NULL);assert(one==&a&&alias==one);
        assert(melee_web_archive_sections_close(h,error,sizeof(error)));
        load(NULL,"Second.dat",&one,"second",NULL);assert(one==&b);
        assert(melee_web_archive_sections_close(other,error,sizeof(error)));
        filename[0]='A';symbol[0]='f';
    }
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
    extra=melee_web_archive_sections_open("Extra.dat");
    assert(!melee_web_archive_sections_close(declared,error,sizeof(error)));
    test_generation=0;
    assert(!melee_web_archive_sections_close(declared,error,sizeof(error)));
    test_heap_exists=0;
    assert(melee_web_archive_sections_close(declared,error,sizeof(error)));
    if(argc==2&&!strcmp(argv[1],"heap_released"))melee_web_archive_sections_public(extra,"absent");
    puts("Typed archive sections copied names, resolved aliases, rejected duplicates and restarted");
}
