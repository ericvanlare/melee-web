#include "gameplay_archive_sections.h"
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
        if(argc==2&&!strcmp(argv[1],"handle"))load(&one,"Authored.dat",&alias,"first",NULL);
        load(NULL,"Authored.dat",&one,"first",&alias,"alias",NULL);assert(one==&a&&alias==one);
        assert(melee_web_archive_sections_close(h,error,sizeof(error)));
        load(NULL,"Second.dat",&one,"second",NULL);assert(one==&b);
        assert(melee_web_archive_sections_close(other,error,sizeof(error)));
        filename[0]='A';symbol[0]='f';
    }
    puts("Typed archive sections copied names, resolved aliases, rejected duplicates and restarted");
}
