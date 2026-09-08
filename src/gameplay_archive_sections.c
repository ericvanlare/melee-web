#include "gameplay_archive_sections.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct MeleeWebArchiveSections { struct MeleeWebArchiveSections* next; size_t count; MeleeWebArchiveSymbol entries[]; };
static MeleeWebArchiveSections* scopes;
static size_t total;
static int fail(char* e,size_t n,const char* s){if(e&&n)snprintf(e,n,"%s",s);return 0;}
static void fatal(const char* filename,const char* symbol,const char* why) {
    fprintf(stderr,"Native archive sections: %s (%s / %s)\n",why,filename?filename:"null",symbol?symbol:"null");abort();
}
static void* lookup(const char* filename,const char* symbol) {
    for(MeleeWebArchiveSections* h=scopes;h;h=h->next)
        for(size_t i=0;i<h->count;i++)if(!strcmp(filename,h->entries[i].filename)&&!strcmp(symbol,h->entries[i].symbol))return h->entries[i].native_data;
    return NULL;
}
MeleeWebArchiveSections* melee_web_archive_sections_register(const MeleeWebArchiveSymbol* input,size_t count,char* e,size_t n) {
    if(!input||!count||count>256-total){fail(e,n,"Native archive symbol budget exceeded");return NULL;}
    for(size_t i=0;i<count;i++) {
        if(!input[i].filename||!input[i].symbol||!input[i].native_data||!input[i].filename[0]||!input[i].symbol[0]||
           strnlen(input[i].filename,1025)>1024||strnlen(input[i].symbol,1025)>1024) {
            fail(e,n,"Invalid native archive symbol");return NULL;
        }
        if(lookup(input[i].filename,input[i].symbol)){fail(e,n,"Native archive symbol already registered");return NULL;}
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
int melee_web_archive_sections_close(MeleeWebArchiveSections* h,char* e,size_t n) {
    if(!h)return 1;
    MeleeWebArchiveSections** link=&scopes;while(*link&&*link!=h)link=&(*link)->next;
    if(!*link)return fail(e,n,"Native archive scope is not registered");
    *link=h->next;total-=h->count;
    for(size_t i=0;i<h->count;i++){free((void*)h->entries[i].filename);free((void*)h->entries[i].symbol);}free(h);
    if(e&&n)*e=0;return 1;
}
void melee_web_archive_sections_load(void* archive,const char* filename,void* first,va_list args) {
    if(archive)fatal(filename,NULL,"Raw archive handles are not supported by typed section storage");
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
    for(size_t i=0;i<count;i++)*destinations[i]=values[i];
}
