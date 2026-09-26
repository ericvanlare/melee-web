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
    const void* object;
    MeleeWebArchiveSections* owner;
    int source_archive;
    int preloaded;
} ArchiveHandle;
static ArchiveHandle* handles;
static size_t handle_count;
static int fail(char* e,size_t n,const char* s){if(e&&n)snprintf(e,n,"%s",s);return 0;}
static const char* canonical_preload_name(const char* filename) {
    /* Retail DVD root paths use one leading slash; native asset catalogs use
     * the same root-relative name without that path marker. */
    if(filename&&filename[0]=='/'&&filename[1]!='\0'&&filename[1]!='/')
        return filename+1;
    return filename;
}
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
static MeleeWebArchiveSections* unique_archive_scope(const char* filename) {
    MeleeWebArchiveSections* found=NULL;
    filename=canonical_preload_name(filename);
    if(!filename)return NULL;
    for(MeleeWebArchiveSections* scope=scopes;scope;scope=scope->next)
        for(size_t i=0;i<scope->count;i++)
            if(!strcmp(filename,scope->entries[i].filename)) {
                if(found&&found!=scope)return NULL;
                found=scope;
            }
    return found;
}
static ArchiveHandle* checked_handle(void* candidate) {
    for(ArchiveHandle* h=handles;h;h=h->next)if(h->object==candidate)return h;
    fatal(NULL,NULL,"Unknown or released typed archive handle");return NULL;
}
static void remove_handle(ArchiveHandle* handle) {
    ArchiveHandle** link=&handles;
    while(*link&&*link!=handle)link=&(*link)->next;
    if(!*link)fatal(NULL,NULL,"Typed archive handle is not registered");
    *link=handle->next;--handle_count;free(handle->filename);free(handle);
}
void* melee_web_archive_sections_open(const char* filename) {
    if(!has_archive(filename))fatal(filename,NULL,"Typed archive is not registered");
    if(handle_count>=256)fatal(filename,NULL,"Typed archive handle budget exceeded");
    ArchiveHandle* h=calloc(1,sizeof(*h));
    if(!h)fatal(filename,NULL,"Typed archive handle allocation failed");
    h->filename=strdup(filename);
    if(!h->filename){free(h);fatal(filename,NULL,"Typed archive name allocation failed");}
    h->object=h;h->owner=NULL;h->source_archive=0;h->preloaded=0;
    h->next=handles;handles=h;++handle_count;return h;
}
void* melee_web_archive_sections_open_preloaded(const char* filename) {
    filename=canonical_preload_name(filename);
    MeleeWebArchiveSections* owner=unique_archive_scope(filename);
    if(!owner)return NULL;
    for(ArchiveHandle* h=handles;h;h=h->next)
        if(h->preloaded&&h->owner==owner&&!strcmp(h->filename,filename))
            return (void*)h->object;
    if(handle_count>=256)fatal(filename,NULL,"Typed archive handle budget exceeded");
    ArchiveHandle* h=calloc(1,sizeof(*h));
    if(!h)fatal(filename,NULL,"Typed archive handle allocation failed");
    h->filename=strdup(filename);
    if(!h->filename){free(h);fatal(filename,NULL,"Typed archive name allocation failed");}
    h->object=h;h->owner=owner;h->preloaded=1;
    h->next=handles;handles=h;++handle_count;return (void*)h->object;
}
int melee_web_archive_sections_attach_source(void* archive,const char* filename) {
    if(!archive||!has_archive(filename)||handle_count>=256||
       melee_web_archive_sections_is_handle(archive))return 0;
    ArchiveHandle* h=calloc(1,sizeof(*h));
    if(!h)return 0;
    h->filename=strdup(filename);
    if(!h->filename){free(h);return 0;}
    h->object=archive;h->owner=NULL;h->source_archive=1;h->preloaded=0;
    h->next=handles;handles=h;++handle_count;
    return 1;
}
int melee_web_archive_sections_is_handle(const void* candidate) {
    for(const ArchiveHandle* h=handles;h;h=h->next)if(h->object==candidate)return 1;
    return 0;
}
int melee_web_archive_sections_is_source_archive(const void* candidate) {
    for(const ArchiveHandle* h=handles;h;h=h->next)
        if(h->object==candidate)return h->source_archive;
    return 0;
}
void* melee_web_archive_sections_public(void* candidate,const char* symbol) {
    ArchiveHandle* h=checked_handle(candidate);
    if(!symbol)fatal(h->filename,NULL,"Missing public symbol name");
    return lookup(h->filename,symbol);
}
void melee_web_archive_sections_release(void* candidate) {
    ArchiveHandle* h=checked_handle(candidate);
    remove_handle(h);
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
    const int world_live=melee_web_gameplay_world_exists();
    if(h->heap_generation) {
        if(world_live)
            return fail(e,n,"Scene-heap archive consumers must be destroyed before scope release");
    }
    if(!world_live) {
        /* Source archives have no retail close call: their object is reclaimed
         * with the world heap. Drop its typed owner at the same post-shutdown
         * scope boundary, including for static symbol catalogs. */
        ArchiveHandle* opened=handles;
        while(opened) {
            ArchiveHandle* next=opened->next;
            for(size_t i=0;i<h->count;i++)if(!strcmp(opened->filename,h->entries[i].filename)) {
                if(h->heap_generation || opened->source_archive) remove_handle(opened);
                break;
            }
            opened=next;
        }
    }
    for(ArchiveHandle* opened=handles;opened;opened=opened->next)
        for(size_t i=0;i<h->count;i++)if(!strcmp(opened->filename,h->entries[i].filename))
            if(!(opened->preloaded&&opened->owner==h))
                return fail(e,n,"Native archive scope still has open handles");
    ArchiveHandle* opened=handles;
    while(opened) {
        ArchiveHandle* next=opened->next;
        if(opened->preloaded&&opened->owner==h)remove_handle(opened);
        opened=next;
    }
    *link=h->next;total-=h->count;
    for(size_t i=0;i<h->count;i++){free((void*)h->entries[i].filename);free((void*)h->entries[i].symbol);}free(h);
    if(e&&n)*e=0;return 1;
}
int melee_web_archive_sections_close_owned(MeleeWebArchiveSections* scope,void* candidate,char* e,size_t n) {
    MeleeWebArchiveSections* registered=scopes;while(registered&&registered!=scope)registered=registered->next;
    if(!registered||scope->heap_generation)return fail(e,n,"Owned-handle close requires a registered descriptor scope");
    ArchiveHandle* owned=handles;while(owned&&owned!=candidate)owned=owned->next;
    if(!owned)return fail(e,n,"Owned archive handle is not open");
    int belongs=0;
    for(size_t i=0;i<scope->count;i++)if(!strcmp(owned->filename,scope->entries[i].filename))belongs=1;
    if(!belongs)return fail(e,n,"Owned archive handle belongs to another scope");
    for(ArchiveHandle* opened=handles;opened;opened=opened->next)if(opened!=owned)
        for(size_t i=0;i<scope->count;i++)if(!strcmp(opened->filename,scope->entries[i].filename))
            if(!(opened->preloaded&&opened->owner==scope))
                return fail(e,n,"Native archive scope still has other open handles");
    melee_web_archive_sections_release(owned);
    return melee_web_archive_sections_close(scope,e,n);
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
