#include "gameplay_font_atlas.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct MeleeWebFontAtlas { void* data; };
static MeleeWebFontAtlas* current;
static int fail(char* error,size_t size,const char* message){
    if(error&&size)snprintf(error,size,"%s",message);return 0;
}
MeleeWebFontAtlas* melee_web_font_atlas_register(const void* bytes,size_t length,char* error,size_t size){
    if(current){fail(error,size,"Original font atlas is already registered");return NULL;}
    if(!bytes||length!=MELEE_WEB_FONT_ATLAS_BYTES){
        fail(error,size,"Original font atlas requires exactly 287 glyphs of 512 bytes");return NULL;
    }
    MeleeWebFontAtlas* owner=malloc(sizeof(*owner));
    if(!owner){fail(error,size,"Cannot allocate original font atlas owner");return NULL;}
    owner->data=aligned_alloc(32,MELEE_WEB_FONT_ATLAS_BYTES);
    if(!owner->data){free(owner);fail(error,size,"Cannot allocate original font atlas bytes");return NULL;}
    memcpy(owner->data,bytes,length);current=owner;
    if(error&&size)*error=0;return owner;
}
int melee_web_font_atlas_close(MeleeWebFontAtlas* owner,char* error,size_t size){
    if(!owner)return 1;
    if(owner!=current)return fail(error,size,"Original font atlas scope is not active");
    current=NULL;free(owner->data);free(owner);if(error&&size)*error=0;return 1;
}
void* melee_web_font_atlas_data(void){
    if(!current){fprintf(stderr,"Original font atlas is not registered; load the local GALE01 font asset before text rendering\n");abort();}
    return current->data;
}
