#include "gameplay_font_atlas.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
int main(int argc,char** argv){
    if(argc==2&&!strcmp(argv[1],"unregistered")){melee_web_font_atlas_data();return 2;}
    char error[128];unsigned char* bytes=malloc(MELEE_WEB_FONT_ATLAS_BYTES);assert(bytes);
    for(size_t i=0;i<MELEE_WEB_FONT_ATLAS_BYTES;i++)bytes[i]=(unsigned char)(i*37);
    if(argc==2){FILE* file=fopen(argv[1],"rb");assert(file);
        assert(fread(bytes,1,MELEE_WEB_FONT_ATLAS_BYTES,file)==MELEE_WEB_FONT_ATLAS_BYTES);
        assert(fgetc(file)==EOF);fclose(file);
    }
    assert(!melee_web_font_atlas_register(bytes,MELEE_WEB_FONT_ATLAS_BYTES-1,error,sizeof(error)));
    for(unsigned pass=0;pass<2;pass++){
        MeleeWebFontAtlas* owner=melee_web_font_atlas_register(bytes,MELEE_WEB_FONT_ATLAS_BYTES,error,sizeof(error));assert(owner);
        unsigned char* data=melee_web_font_atlas_data();assert(!((uintptr_t)data&31)&&data!=bytes);
        assert(!memcmp(bytes,data,MELEE_WEB_FONT_ATLAS_BYTES));
        bytes[0]^=1;assert(bytes[0]!=data[0]);bytes[0]^=1;
        assert(!melee_web_font_atlas_register(bytes,MELEE_WEB_FONT_ATLAS_BYTES,error,sizeof(error)));
        assert(melee_web_font_atlas_close(owner,error,sizeof(error)));
    }
    free(bytes);puts("Owned original font byte preservation/alignment/restart passed");return 0;
}
