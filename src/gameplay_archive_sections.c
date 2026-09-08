#include "gameplay_archive_sections.h"
#include "gameplay_bootstrap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct MeleeWebArchiveSections { struct MeleeWebArchiveSections* next; size_t count; uint64_t heap_generation; MeleeWebArchiveSymbol entries[]; };
static MeleeWebArchiveSections* scopes;
static size_t total;
typedef struct ArchiveHandle {
    struct ArchiveHandle* next;
    char* filename;
} ArchiveHandle;
static ArchiveHandle* handles;
static size_t handle_count;
static int fail(char* e,size_t n,const char* s){if(e&&n)snprintf(e,n,"%s",s);return 0;}
static void fatal(const char* filename,const char* symbol,const char* why) {
    fprintf(stderr,"Native archive sections: %s (%s / %s)\n",why,filename?filename:"null",symbol?symbol:"null");abort();
}
static const MeleeWebArchiveSymbol* find_symbol(const char* filename,const char* symbol) {
    for(MeleeWebArchiveSections* h=scopes;h;h=h->next)
        for(size_t i=0;i<h->count;i++)
            if(!strcmp(filename,h->entries[i].filename)&&!strcmp(symbol,h->entries[i].symbol))
                return &h->entries[i];
    return NULL;
}
static void* lookup(const char* filename,const char* symbol) {
    const MeleeWebArchiveSymbol* entry=find_symbol(filename,symbol);
    if(entry && !entry->native_data)
        fatal(filename,symbol,"Source public symbol is present but not hydrated");
    return entry?entry->native_data:NULL;
}
static int has_archive(const char* filename) {
    if(!filename)return 0;
    for(MeleeWebArchiveSections* scope=scopes;scope;scope=scope->next)
        for(size_t i=0;i<scope->count;i++)
            if(!strcmp(filename,scope->entries[i].filename))return 1;
    return 0;
}
static ArchiveHandle* checked_handle(void* candidate) {
    for(ArchiveHandle* h=handles;h;h=h->next)if(h==candidate)return h;
    fatal(NULL,NULL,"Unknown or released typed archive handle");return NULL;
}
void* melee_web_archive_sections_open(const char* filename) {
    if(!has_archive(filename))fatal(filename,NULL,"Typed archive is not registered");
    if(handle_count>=256)fatal(filename,NULL,"Typed archive handle budget exceeded");
    ArchiveHandle* h=calloc(1,sizeof(*h));
    if(!h)fatal(filename,NULL,"Typed archive handle allocation failed");
    h->filename=strdup(filename);
    if(!h->filename){free(h);fatal(filename,NULL,"Typed archive name allocation failed");}
    h->next=handles;handles=h;++handle_count;return h;
}
void* melee_web_archive_sections_public(void* candidate,const char* symbol) {
    ArchiveHandle* h=checked_handle(candidate);
    if(!symbol)fatal(h->filename,NULL,"Missing public symbol name");
    return lookup(h->filename,symbol);
}
void melee_web_archive_sections_release(void* candidate) {
    ArchiveHandle* h=checked_handle(candidate);
    ArchiveHandle** link=&handles;while(*link!=h)link=&(*link)->next;
    *link=h->next;--handle_count;free(h->filename);free(h);
}
MeleeWebArchiveSections* melee_web_archive_sections_register(const MeleeWebArchiveSymbol* input,size_t count,char* e,size_t n) {
    if(!input||!count||count>256-total){fail(e,n,"Native archive symbol budget exceeded");return NULL;}
    for(size_t i=0;i<count;i++) {
        if(!input[i].filename||!input[i].symbol||!input[i].filename[0]||!input[i].symbol[0]||
           strnlen(input[i].filename,1025)>1024||strnlen(input[i].symbol,1025)>1024) {
            fail(e,n,"Invalid native archive symbol");return NULL;
        }
        for(MeleeWebArchiveSections* prior=scopes;prior;prior=prior->next)
            if(prior->heap_generation)for(size_t j=0;j<prior->count;j++)
                if(!strcmp(input[i].filename,prior->entries[j].filename)) {
                    fail(e,n,"Scene-heap archive requires exclusive file ownership");return NULL;
                }
        if(find_symbol(input[i].filename,input[i].symbol)){fail(e,n,"Native archive symbol already registered");return NULL;}
        for(size_t j=0;j<i;j++)if(!strcmp(input[i].filename,input[j].filename)&&!strcmp(input[i].symbol,input[j].symbol)){
            fail(e,n,"Duplicate native archive symbol in scope");return NULL;
        }
    }
    MeleeWebArchiveSections* h=calloc(1,sizeof(*h)+count*sizeof(*input));
    if(!h){fail(e,n,"Native archive scope allocation failed");return NULL;}
    for(size_t i=0;i<count;i++) {
        char* filename=strdup(input[i].filename);char* symbol=strdup(input[i].symbol);
        h->entries[i]=(MeleeWebArchiveSymbol){filename,symbol,input[i].native_data};
        if(!filename||!symbol){
            for(size_t j=0;j<=i;j++){free((void*)h->entries[j].filename);free((void*)h->entries[j].symbol);}free(h);
            fail(e,n,"Native archive name allocation failed");return NULL;
        }
    }
    h->count=count;h->next=scopes;scopes=h;total+=count;if(e&&n)*e=0;return h;
}
MeleeWebArchiveSections* melee_web_archive_sections_register_heap(const MeleeWebArchiveSymbol* input,size_t count,char* e,size_t n) {
    const uint64_t generation=melee_web_gameplay_stats().generation;
    if(!generation){fail(e,n,"Scene-heap archives require a live SDK world");return NULL;}
    if(!input || !count || count>256){fail(e,n,"Invalid scene-heap archive catalog");return NULL;}
    for(size_t i=0;i<count;i++)if(has_archive(input[i].filename)) {
        fail(e,n,"Scene-heap archive requires exclusive file ownership");return NULL;
    }
    MeleeWebArchiveSections* scope=melee_web_archive_sections_register(input,count,e,n);
    if(scope)scope->heap_generation=generation;
    return scope;
}
int melee_web_archive_sections_close(MeleeWebArchiveSections* h,char* e,size_t n) {
    if(!h)return 1;
    MeleeWebArchiveSections** link=&scopes;while(*link&&*link!=h)link=&(*link)->next;
    if(!*link)return fail(e,n,"Native archive scope is not registered");
    if(h->heap_generation) {
        if(melee_web_gameplay_world_exists())
            return fail(e,n,"Scene-heap archive consumers must be destroyed before scope release");
        /* Original lbArchive_80016DBC handles may be discarded by their
         * callers and freed with the scene heap. Only this explicitly owned
         * catalog may reclaim them, after the SDK world has been destroyed. */
        ArchiveHandle* opened=handles;
        while(opened) {
            ArchiveHandle* next=opened->next;
            for(size_t i=0;i<h->count;i++)if(!strcmp(opened->filename,h->entries[i].filename)) {
                melee_web_archive_sections_release(opened);break;
            }
            opened=next;
        }
    }
    for(ArchiveHandle* opened=handles;opened;opened=opened->next)
        for(size_t i=0;i<h->count;i++)if(!strcmp(opened->filename,h->entries[i].filename))
            return fail(e,n,"Native archive scope still has open handles");
    *link=h->next;total-=h->count;
    for(size_t i=0;i<h->count;i++){free((void*)h->entries[i].filename);free((void*)h->entries[i].symbol);}free(h);
    if(e&&n)*e=0;return 1;
}
void melee_web_archive_sections_load(void* archive,const char* filename,void* first,va_list args) {
    if(!filename||!first)fatal(filename,NULL,"Missing archive name or output");
    void** destinations[64];void* values[64];size_t count=0;void* output=first;
    va_list copy;va_copy(copy,args);
    while(output) {
        const char* name=va_arg(copy,const char*);
        if(!name||count==64)fatal(filename,name,"Invalid or excessive section request");
        void* value=lookup(filename,name);if(!value)fatal(filename,name,"Typed section is not registered");
        destinations[count]=output;values[count++]=value;output=va_arg(copy,void*);
    }
    va_end(copy);
    void* handle=archive?melee_web_archive_sections_open(filename):NULL;
    for(size_t i=0;i<count;i++)*destinations[i]=values[i];
    if(archive)*(void**)archive=handle;
}
